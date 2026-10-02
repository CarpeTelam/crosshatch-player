#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ArenaSize.h"
#include "FrameReplay.h"
#include "GameAssets.h"
#include "GameHash.h"
#include "GameSaveStore.h"
#include "GameViewport.h"
#include "MatchStore.h"
#include "MatchSupport.h"
#include "Session.h"
#include "activities/games/GameMatchActivity.h"

// resume.bin and resuming a solo match, entry 11 of epic-install-and-launcher: the real GameVM,
// GameSaveStore, and GameMatchActivity over the screen doubles. The fixtures here shadow the ones in
// GameVmTest.cpp and GameMatchTest.cpp, whose fixtures sit in anonymous namespaces.
//
// Time is the fake clock (screen_stubs/FakeRtos.h): a wait moves it by what it waited, and a Ticker
// thread moves it further, so a wait that counts polls instead of time is told from one that counts it.

namespace {

using match::installGame;
using match::waitFor;
using Button = MappedInputManager::Button;
using harness::Bytes;

constexpr int CANVAS_X = 3;
constexpr int CANVAS_Y = 6;

const uint8_t HASH_A[GamePkg::HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1};
const uint8_t HASH_B[GamePkg::HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf2};

// A game of `goal` taps that logs its setup and every draw, so a resumed match shows which snapshot
// it drew and whether setup ran.
std::string countingGame(const int goal, const bool storing = false) {
  return std::string(R"(
local game = {}
local GOAL = )") +
         std::to_string(goal) + R"(
function game.setup(ctx)
  ch.log("setup ran")
  return { taps = 0 }
end
function game.status(state)
  if state.taps >= GOAL then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
)" + std::string(storing ? "  ch.store.set({ taps = state.taps })\n" : "") +
         R"(  return state
end
function game.draw(state, seat, ui)
  ch.log("draw", state.taps)
  ch.gfx.clear("white")
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";
}

// A game that commits its first tap and errors on the second.
const char* const ERROR_GAME = R"(
local game = {}
function game.setup(ctx) return { n = 0 } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move)
  state.n = state.n + 1
  return state
end
function game.draw(state, seat, ui)
  ch.log("draw", state.n)
  ch.gfx.clear("white")
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.time.ms()
    if state.n >= 1 then error("boom") end
    return { tap = true }
  end
end
return game
)";

// A game whose draw, after a committed move, reads the clock (where a test's gate can hold it) and then
// spins reading it until the run is cancelled.
const char* const CANCEL_GAME = R"(
local game = {}
function game.setup(ctx) return { taps = 0 } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  return state
end
function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  if state.taps > 0 then
    ch.time.ms()
    while true do ch.time.ms() end
  end
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

// Games whose status or draw, for a snapshot after the first move, reads the clock (where a test's gate
// can hold it) and then spins reading it until the run is cancelled.
const char* const STATUS_SPIN_GAME = R"(
local game = {}
function game.setup(ctx) return { taps = 0 } end
function game.status(state)
  if state.taps > 0 then
    ch.time.ms()
    while true do ch.time.ms() end
  end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  return state
end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

// The first tap ends the round; the draw of that snapshot spins.
const char* const OVER_DRAW_SPIN_GAME = R"(
local game = {}
function game.setup(ctx) return { taps = 0 } end
function game.status(state)
  if state.taps >= 1 then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  return state
end
function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  if state.taps > 0 then
    ch.time.ms()
    while true do ch.time.ms() end
  end
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

// The first tap ends round 1; the status of round 2's first snapshot spins.
const char* const RESTART_STATUS_SPIN_GAME = R"(
local game = {}
local round = 0
function game.setup(ctx)
  round = round + 1
  return { round = round, taps = 0 }
end
function game.status(state)
  if state.round >= 2 then
    ch.time.ms()
    while true do ch.time.ms() end
  end
  if state.taps >= 1 then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  return state
end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

// {n = k}: the codec bytes ERROR_GAME's snapshot has.
Bytes nSnapshotOf(const uint8_t n) {
  return Bytes{0x06, 0x00, 0x01, 0x05, 0x01, 'n', 0x03, static_cast<uint8_t>(n << 1)};
}

// {taps = n}: the codec bytes the counting game's snapshot has.
Bytes snapshotOf(const uint8_t taps) {
  return Bytes{0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, static_cast<uint8_t>(taps << 1)};
}

// resume.bin as GameSaveStore lays it out for the package `hash`.
Bytes resumeBytes(const Bytes& snapshot, const uint16_t ver, const uint8_t (&hash)[GamePkg::HASH_BYTES] = HASH_A) {
  Bytes out = {'C', 'H', 'R', 'S', 1, 1};
  out.insert(out.end(), hash, hash + GamePkg::HASH_BYTES);
  out.push_back(0);
  out.push_back(1);
  out.push_back(static_cast<uint8_t>(ver & 0xFF));
  out.push_back(static_cast<uint8_t>(ver >> 8));
  out.insert(out.end(), snapshot.begin(), snapshot.end());
  return out;
}

std::string resumePath(const std::string& id) { return "/.games-data/" + id + "/resume.bin"; }
std::string resumeTmpPath(const std::string& id) { return resumePath(id) + ".tmp"; }

void installPkg(const std::string& id, const uint8_t (&hash)[GamePkg::HASH_BYTES] = HASH_A) {
  char bytes[GamePkg::FILE_BYTES];
  GamePkg::formatPkg(hash, bytes);
  fakesd::addFile("/.games/" + id + "/.pkg", std::string(bytes, sizeof(bytes)));
}

// Moves the fake clock by `stepMs` every real millisecond while it lives: time that passes while
// the code under test waits on its own polls.
class Ticker {
 public:
  explicit Ticker(const uint64_t stepMs) {
    thread = std::thread([this, stepMs] {
      while (!stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        fakertos::advance(stepMs);
      }
    });
  }
  ~Ticker() {
    stop.store(true);
    thread.join();
  }

 private:
  std::atomic<bool> stop{false};
  std::thread thread;
};

uint64_t clockMs() { return fakertos::S().nowMs.load(); }

// ---- the match ----

class ResumeMatchTest : public match::ScreenTest {
 protected:
  void TearDown() override {
    fakertos::release();  // a test that failed while holding the VM must not leave it held
    fakearena::reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;
    if (activity) {
      if (!exited) activityManager.exitHolding(*activity);
      activityManager.destroyHolding(activity);
    }
    ScreenTest::TearDown();
  }

  void enter(const std::string& id, const GameMatchActivity::Start start = GameMatchActivity::Start::New) {
    gameId = id;
    exited = false;
    activity =
        std::make_unique<GameMatchActivity>(*renderer, *input, match::manifestOf(id), GameCore::Roster::solo(), start);
    firstFramePending = true;
    activity->onEnter();
  }

  // The screen goes (sleep, or any Replace): the manager's exit under the lock, then the destructor.
  void sleep() {
    exited = true;
    activityManager.exitHolding(*activity);
    ASSERT_TRUE(fakertos::waitNoTasks(1000));
    activityManager.destroyHolding(activity);
  }

  void frame() {
    activity->loop();
    input->clear();
  }
  bool pump(const std::function<bool()>& done, const int timeoutMs = 10000) {
    return waitFor(
        [&] {
          frame();
          return done();
        },
        timeoutMs);
  }
  void showFrame() {
    ASSERT_TRUE(pump([&] { return activityManager.updateRequested(); }));
    activity->render(RenderLock(*activity));
    activityManager.markRendered();
    firstFramePending = false;
  }
  // A tap at canvas point (x, y). The match drops taps until the round's first frame is drawn (retro deferral e3r-2),
  // as a finger cannot be aimed at a frame that is not on the panel: so the first tap after enter() waits for the
  // frame the match asks for (the VM publishes it on its own task, and the loop asks a pass after) and draws it, as the
  // render task would. showFrame() does the same on request and clears the wait.
  void tapCanvas(const int x, const int y) {
    if (firstFramePending) {
      pump([&] { return activityManager.updateRequested(); }, 5000);
      showFrame();
    }
    input->tap(CANVAS_X + x, CANVAS_Y + y);
  }

  bool fileIs(const Bytes& expected) const { return fakesd::bytesOf(resumePath(gameId)) == expected; }
  // Loops until resume.bin holds `expected`.
  bool pumpToFile(const Bytes& expected) {
    return pump([&] { return fileIs(expected); });
  }

  std::string state() const {
    std::string found = "Starting";
    const std::string prefix = "INF GAME: " + gameId + ": ";
    for (const std::string& line : fakelog::snapshot()) {
      if (line.rfind(prefix, 0) != 0) continue;
      const size_t arrow = line.find(" -> ");
      const size_t on = line.find(" on ", arrow);
      if (arrow == std::string::npos || on == std::string::npos) continue;
      found = line.substr(arrow + 4, on - arrow - 4);
    }
    return found;
  }

  size_t tmpOpens() const { return fakesd::countOps("open " + resumeTmpPath(gameId)); }

  // Loops, moving the clock past the write backoff each pass, until a write of the game's resume.bin.tmp has failed.
  // The over snapshot of the finished round can still be pending when Play again comes (its delete failed), and the
  // failed delete arms the backoff again, so the rematch's snapshot is written on the first pass after the interval.
  bool pumpUntilTheRematchsFirstWriteFails(const std::string& id) {
    return pump([&] {
      fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
      return logHas("cannot write " + resumeTmpPath(id));
    });
  }

  std::unique_ptr<GameMatchActivity> activity;
  std::string gameId;
  bool firstFramePending = false;  // enter() ran and no frame has been drawn since
  bool exited = false;
};

TEST_F(ResumeMatchTest, EachCommittedSnapshotIsWrittenOnTheNextLoopPass) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  EXPECT_FALSE(fakesd::has(resumeTmpPath("cnt")));
  EXPECT_EQ(GameSaveStore::peek("cnt", HASH_A), GameSaveStore::SaveState::Valid);

  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(1), 2)));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(2), 3)));
  // Through the tmp and a rename, once per snapshot.
  EXPECT_GE(fakesd::countOps("rename " + resumeTmpPath("cnt") + " " + resumePath("cnt")), 3u);
}

TEST_F(ResumeMatchTest, NothingCommittedMeansNothingIsWrittenAgain) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  const size_t opens = tmpOpens();
  for (int i = 0; i < 20; ++i) frame();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS * 2);
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_EQ(tmpOpens(), opens) << "no new snapshot, so no new write, however long it has been";
}

TEST_F(ResumeMatchTest, WithoutAPkgTheMatchPlaysAndWritesNothing) {
  installGame("cnt", countingGame(5));
  enter("cnt");
  EXPECT_TRUE(logHas("cnt: no valid .pkg; no resume.bin"));
  showFrame();
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(waitFor([&] {
    frame();
    return logHas("draw\t1");
  }));
  for (int i = 0; i < 20; ++i) frame();
  sleep();
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));
  EXPECT_FALSE(fakesd::has(resumeTmpPath("cnt")));
  EXPECT_EQ(fakesd::countOps("open " + resumePath("cnt")), 0u);
  EXPECT_EQ(tmpOpens(), 0u);
}

// Retro deferral 4.12 (ADV1 of the independent review): Continue was offered for this package's save, so a .pkg that
// will not read at match entry is a fault, not "an unpackaged game". The match must stop, not play new: a new match
// without the package hash cannot tell the save from any file, and used to delete it at Over.
TEST_F(ResumeMatchTest, AContinueWhosePkgWillNotReadStopsInTheErrorViewAndKeepsTheSave) {
  installGame("cnt", countingGame(1));
  fakesd::addFile(resumePath("cnt"), resumeBytes(snapshotOf(0), 3));
  enter("cnt", GameMatchActivity::Start::Resume);
  EXPECT_EQ(state(), "Error");
  EXPECT_TRUE(logHas("cannot read .pkg; not starting a new match over a save"));
  EXPECT_TRUE(logHas(tr(STR_GAMES_RESUME_FAILED)));
  for (int i = 0; i < 30; ++i) frame();
  EXPECT_FALSE(logHas("setup ran")) << "no game ran";
  EXPECT_EQ(fakesd::bytesOf(resumePath("cnt")), resumeBytes(snapshotOf(0), 3));
  sleep();
  EXPECT_EQ(fakesd::bytesOf(resumePath("cnt")), resumeBytes(snapshotOf(0), 3)) << "and the forced exit keeps it";
}

// A new match of a game without a valid .pkg plays and writes nothing, and at Over it deletes nothing either: a file
// there is not known to be this package's.
TEST_F(ResumeMatchTest, ANewMatchWithoutAPkgLeavesAResumeBinAloneAtOver) {
  installGame("cnt", countingGame(1));
  fakesd::addFile(resumePath("cnt"), resumeBytes(snapshotOf(0), 3));
  enter("cnt");
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_EQ(fakesd::bytesOf(resumePath("cnt")), resumeBytes(snapshotOf(0), 3));
  sleep();
  EXPECT_EQ(fakesd::bytesOf(resumePath("cnt")), resumeBytes(snapshotOf(0), 3));
}

TEST_F(ResumeMatchTest, TheEndOfARoundDeletesTheSaveAndNoOverSnapshotIsEverWritten) {
  installGame("cnt", countingGame(2));
  installPkg("cnt");
  enter("cnt");
  std::set<Bytes> seen;
  auto watch = [&] { seen.insert(fakesd::bytesOf(resumePath("cnt"))); };
  ASSERT_TRUE(pump([&] {
    watch();
    return fileIs(resumeBytes(snapshotOf(0), 1));
  }));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] {
    watch();
    return fileIs(resumeBytes(snapshotOf(1), 2));
  }));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] {
    watch();
    return state() == "Over";
  }));
  watch();
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));
  EXPECT_FALSE(fakesd::has(resumeTmpPath("cnt")));
  EXPECT_EQ(GameSaveStore::peek("cnt", HASH_A), GameSaveStore::SaveState::None);
  for (int i = 0; i < 20; ++i) {
    frame();
    watch();
  }
  EXPECT_EQ(seen.count(resumeBytes(snapshotOf(2), 3)), 0u) << "the round's last snapshot was written";
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));

  // The forced exit in the end-of-round menu writes nothing either.
  sleep();
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));
  EXPECT_FALSE(fakesd::has(resumeTmpPath("cnt")));
}

TEST_F(ResumeMatchTest, PlayAgainWritesTheNewRoundsSnapshotsAgain) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));

  input->click(Button::Confirm);  // Play again is the first option
  frame();
  EXPECT_EQ(state(), "Playing");
  // ver keeps counting: setup 1, the tap 2, and the new round's setup 3.
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 3)));
}

TEST_F(ResumeMatchTest, ARoundThatEndsWhilePausedDeletesTheSaveAndIsOverAfterResume) {
  installGame("cnt", countingGame(2));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(1), 2)));
  tapCanvas(50, 50);
  frame();  // the second tap is posted; the round may end at any moment
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  // Paused: the loop passes flush, and the over snapshot deletes the save instead of replacing it.
  ASSERT_TRUE(pump([&] { return !fakesd::has(resumePath("cnt")); }));
  EXPECT_EQ(state(), "Paused");
  EXPECT_FALSE(fakesd::has(resumeTmpPath("cnt")));
  input->click(Button::Back);  // Resume
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));
}

TEST_F(ResumeMatchTest, LeavingWritesTheLastSnapshotAndKeepsIt) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  // The write of the next snapshot fails, so the loop leaves it pending.
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(waitFor([&] {
    frame();
    return logHas("cannot write " + resumeTmpPath("cnt"));
  }));
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(0), 1))) << "a failed write leaves the previous save";
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // the failed write is not retried before this
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  // Leave: the second option, by keys.
  input->press(Button::NavNext);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "Leaving");
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(1), 2))) << "Leave writes what the loop could not";
  EXPECT_EQ(GameSaveStore::peek("cnt", HASH_A), GameSaveStore::SaveState::Valid) << "and Leave keeps the save";
}

TEST_F(ResumeMatchTest, TheForcedExitWritesTheSnapshotALoopWriteFailedOnceTheIntervalHasPassed) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(waitFor([&] {
    frame();
    return logHas("cannot write " + resumeTmpPath("cnt"));
  }));
  // Throttled: passes within FLUSH_INTERVAL_MS make no attempt.
  const size_t attempts = tmpOpens();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS - 100);
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_EQ(tmpOpens(), attempts) << "a retry before the interval";
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(0), 1)));

  // The card recovers, and the interval has passed by the time the device sleeps.
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(200);
  exited = true;
  activityManager.exitHolding(*activity);
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "onExit took the RenderLock the manager holds (12cc816)";
  EXPECT_EQ(state(), "Leaving");
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(1), 2))) << "the forced exit writes what the loop could not";
  EXPECT_EQ(activityManager.asks.goToGames, 0);
}

TEST_F(ResumeMatchTest, TheForcedExitWritesTheResumeBeforeTheStore) {
  installGame("cnt", countingGame(5, true));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(waitFor([&] {
    frame();
    return logHas("cannot write " + resumeTmpPath("cnt"));
  }));
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // no loop pass since: the store is dirty, not yet due
  ASSERT_FALSE(fakesd::has("/.games-data/cnt/store.bin"));
  exited = true;
  activityManager.exitHolding(*activity);

  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(1), 2)));
  EXPECT_TRUE(fakesd::has("/.games-data/cnt/store.bin"));
  const auto& ops = fakesd::sim().ops;
  const std::string resumeRename = "rename " + resumeTmpPath("cnt") + " " + resumePath("cnt");
  const std::string storeRename = "rename /.games-data/cnt/store.bin.tmp /.games-data/cnt/store.bin";
  const auto resumeAt = std::find(ops.rbegin(), ops.rend(), resumeRename);
  const auto storeAt = std::find(ops.rbegin(), ops.rend(), storeRename);
  ASSERT_NE(resumeAt, ops.rend());
  ASSERT_NE(storeAt, ops.rend());
  // Reverse iterators: the later op is nearer rbegin.
  EXPECT_GT(resumeAt - ops.rbegin(), storeAt - ops.rbegin()) << "the store was written before the resume snapshot";
  EXPECT_FALSE(logHas("skipped"));
}

TEST_F(ResumeMatchTest, AWriteThatFailedJustBeforeSleepIsNotRetriedAtTheForcedExit) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(waitFor([&] {
    frame();
    return logHas("cannot write " + resumeTmpPath("cnt"));
  }));
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(100);
  const size_t opens = tmpOpens();
  sleep();
  EXPECT_EQ(tmpOpens(), opens) << "a card that failed 100 ms ago was asked again";
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(0), 1)));
}

// A pending resume write, a pending delete, and a dirty store, each started past the deadline.
TEST_F(ResumeMatchTest, PastTheDeadlineTheForcedExitSkipsTheResumeWriteAndTheStoreFlushAndLogsThem) {
  installGame("cnt", countingGame(5, true));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(waitFor([&] {
    frame();
    return logHas("cannot write " + resumeTmpPath("cnt"));
  }));
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
  // The stop starts by waking the VM from the loop task: the card is slow from there on.
  bool slow = false;
  fakertos::S().onLoopNotify = [&] {
    if (!slow) fakertos::advance(GameMatchActivity::FORCED_EXIT_DEADLINE_MS + 100);
    slow = true;
  };
  exited = true;
  activityManager.exitHolding(*activity);
  EXPECT_TRUE(logHas("skipped the resume write"));
  EXPECT_TRUE(logHas("skipped the ch.store flush"));
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(0), 1))) << "the resume write started past the deadline";
  EXPECT_FALSE(fakesd::has("/.games-data/cnt/store.bin"));
}

TEST_F(ResumeMatchTest, PastTheDeadlineTheForcedExitSkipsTheResumeDeleteRetryAndLogsIt) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  fakesd::sim().failRemove.clear();
  bool slow = false;
  fakertos::S().onLoopNotify = [&] {
    if (!slow) fakertos::advance(GameMatchActivity::FORCED_EXIT_DEADLINE_MS + 100);
    slow = true;
  };
  exited = true;
  activityManager.exitHolding(*activity);
  EXPECT_TRUE(logHas("skipped the resume.bin delete"));
  EXPECT_TRUE(fakesd::has(resumePath("cnt")));
}

TEST_F(ResumeMatchTest, TheDeleteRetryWaitsTheIntervalBetweenTriesButTheForcedExitDoesNot) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  const std::string removeOp = "remove " + resumePath("cnt");
  const size_t atOver = fakesd::countOps(removeOp);
  EXPECT_GE(atOver, 1u);
  for (int i = 0; i < 30; ++i) frame();
  EXPECT_EQ(fakesd::countOps(removeOp), atOver) << "retried within the interval";
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
  frame();
  EXPECT_EQ(fakesd::countOps(removeOp), atOver + 1);
  frame();
  EXPECT_EQ(fakesd::countOps(removeOp), atOver + 1) << "and then not again at once";
  sleep();  // the forced exit tries regardless of the last try
  EXPECT_EQ(fakesd::countOps(removeOp), atOver + 2);
}

TEST_F(ResumeMatchTest, AVmThatEndsWithinTheAbandonWaitHasItsLastSnapshotWritten) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  ASSERT_TRUE(waitFor(match::roundStarted));
  // The tap commits ver 2 and the VM is held in its draw, inside ch.log: it publishes nothing yet, so
  // the join times out and the flush after it finds nothing pending.
  fakertos::arm(fakertos::At::Log);
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  // The abandon starts; the call returns and the step publishes and ends while it waits.
  fakelog::hook() = [](const std::string& line) {
    if (line.find("did not stop within") != std::string::npos) fakertos::release();
  };
  exited = true;
  activityManager.exitHolding(*activity);
  fakelog::hook() = nullptr;
  EXPECT_FALSE(logHas("VM stuck")) << "the task ended after all";
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(1), 2))) << "the snapshot it published on the way out was dropped";
  EXPECT_TRUE(fakertos::waitNoTasks());
}

TEST_F(ResumeMatchTest, ASleepingMatchResumesFromTheSameSnapshotWithoutSetupOrARewrite) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  for (int taps = 1; taps <= 2; ++taps) {
    tapCanvas(50, 50);
    frame();
    ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(static_cast<uint8_t>(taps)), static_cast<uint16_t>(taps + 1))));
  }
  sleep();
  const Bytes saved = resumeBytes(snapshotOf(2), 3);
  ASSERT_TRUE(fileIs(saved));

  fakelog::clearLines();
  const size_t writesBefore = fakesd::countOps("write ");
  enter("cnt", GameMatchActivity::Start::Resume);
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(pump([&] { return logHas("Round started at ver 3"); }));
  EXPECT_TRUE(logHas("Resuming at ver 3"));
  EXPECT_TRUE(logHas("draw\t2")) << "the saved snapshot is what is drawn";
  EXPECT_FALSE(logHas("setup ran")) << "setup does not run";
  showFrame();
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_TRUE(fileIs(saved)) << "the file is unchanged";
  EXPECT_EQ(fakesd::countOps("write "), writesBefore) << "and nothing was written for the restored ver";

  // The next tap advances from the restored ver.
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(3), 4)));
  EXPECT_TRUE(logHas("draw\t3"));
  EXPECT_FALSE(logHas("setup ran"));
}

TEST_F(ResumeMatchTest, AResumedMatchThatPlaysOnToTheEndDeletesTheSave) {
  installGame("cnt", countingGame(3));
  installPkg("cnt");
  fakesd::addFile(resumePath("cnt"), resumeBytes(snapshotOf(2), 3));
  enter("cnt", GameMatchActivity::Start::Resume);
  showFrame();
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));
}

TEST_F(ResumeMatchTest, ASaveOfAnotherPackageIsNotResumedAndANewMatchStarts) {
  installGame("cnt", countingGame(5));
  installPkg("cnt", HASH_B);
  fakesd::addFile(resumePath("cnt"), resumeBytes(snapshotOf(2), 3, HASH_A));
  enter("cnt", GameMatchActivity::Start::Resume);
  EXPECT_TRUE(logHas("discarded " + resumePath("cnt") + ": other package"));
  EXPECT_TRUE(logHas("no usable resume.bin; starting a new match"));
  ASSERT_TRUE(pump([&] { return logHas("setup ran"); }));
  EXPECT_EQ(state(), "Playing");
  // The new match's first snapshot replaces the obsolete save, for the package now installed.
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1, HASH_B)));
}

// Retro deferral 4.12 (ADV2): a save the card would not read may be a good one, so a new match must not start over
// it (its first snapshot would replace the file). The match stops in the error view, the file untouched, and Continue
// works again once the card reads.
TEST_F(ResumeMatchTest, AnUnreadableSaveStopsInTheErrorViewAndIsNeverOverwritten) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  fakesd::addFile(resumePath("cnt"), resumeBytes(snapshotOf(2), 3));
  fakesd::sim().failReadAt[resumePath("cnt")] = 0;
  enter("cnt", GameMatchActivity::Start::Resume);
  EXPECT_TRUE(logHas("could not read " + resumePath("cnt") + ": cannot read; the file is kept"));
  EXPECT_TRUE(logHas("resume.bin could not be read; not starting a new match over it"));
  EXPECT_EQ(state(), "Error");
  EXPECT_TRUE(logHas(tr(STR_GAMES_RESUME_FAILED)));
  for (int i = 0; i < 30; ++i) frame();
  EXPECT_FALSE(logHas("setup ran")) << "no game ran";
  EXPECT_EQ(fakesd::bytesOf(resumePath("cnt")), resumeBytes(snapshotOf(2), 3)) << "the save is as it was";
  EXPECT_EQ(tmpOpens(), 0u) << "and nothing was written beside it";

  // Back leaves; the save is still there, and with the card reading again Continue resumes it.
  input->click(Button::Back);
  frame();
  sleep();
  fakesd::sim().failReadAt.clear();
  fakelog::clearLines();
  enter("cnt", GameMatchActivity::Start::Resume);
  ASSERT_TRUE(pump([&] { return logHas("draw\t2"); }));
  EXPECT_FALSE(logHas("setup ran"));
  EXPECT_EQ(state(), "Playing");
}

TEST_F(ResumeMatchTest, ASaveThatCannotBeOpenedIsKeptAndItsMatchStopsInTheErrorView) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  fakesd::addFile(resumePath("cnt"), resumeBytes(snapshotOf(2), 3));
  fakesd::sim().failOpen.insert(resumePath("cnt"));
  enter("cnt", GameMatchActivity::Start::Resume);
  EXPECT_EQ(state(), "Error");
  EXPECT_FALSE(logHas("setup ran"));
  fakesd::sim().failOpen.clear();
  EXPECT_EQ(fakesd::bytesOf(resumePath("cnt")), resumeBytes(snapshotOf(2), 3));
}

TEST_F(ResumeMatchTest, StartResumeWithNoSaveStartsANewMatch) {
  installGame("cnt", countingGame(5));
  installPkg("cnt");
  enter("cnt", GameMatchActivity::Start::Resume);
  EXPECT_TRUE(logHas("no usable resume.bin; starting a new match"));
  ASSERT_TRUE(pump([&] { return logHas("setup ran"); }));
}

TEST_F(ResumeMatchTest, ADeleteTheCardRefusesAtOverIsRetriedUntilTheFileIsGone) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_TRUE(logHas("cannot delete " + resumePath("cnt")));
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_TRUE(fakesd::has(resumePath("cnt"))) << "the card still refuses";
  fakesd::sim().failRemove.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // the retry waits this long after a failed try
  ASSERT_TRUE(pump([&] { return !fakesd::has(resumePath("cnt")); })) << "the retry never came";
  EXPECT_EQ(GameSaveStore::peek("cnt", HASH_A), GameSaveStore::SaveState::None);
}

TEST_F(ResumeMatchTest, ADeleteTheCardRefusedAtOverIsRetriedAtTheForcedExit) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  ASSERT_TRUE(fakesd::has(resumePath("cnt")));
  fakesd::sim().failRemove.clear();
  sleep();  // no loop pass since the card recovered
  EXPECT_FALSE(fakesd::has(resumePath("cnt")));
  EXPECT_FALSE(fakesd::has(resumeTmpPath("cnt")));
}

TEST_F(ResumeMatchTest, PlayAgainAfterAFailedDeleteIsNotDeletedUnderTheNewRound) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  input->click(Button::Confirm);  // Play again, the card still refusing the delete
  frame();
  ASSERT_EQ(state(), "Playing");
  fakesd::sim().failRemove.clear();
  // The failed delete armed the write backoff (FLUSH_INTERVAL_MS) and the host clock does not move by itself: Play
  // again forgets it, so the new round's first snapshot is written at once.
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 3)));
  for (int i = 0; i < 20; ++i) frame();
  // The pause menu's loop passes are where a pending delete would retry.
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(0), 3))) << "the retry deleted the new round's save";
}

// Play again used to forget an Over delete that failed, before the new round had written anything. If that round's
// first write also failed (or its setup errored) and the player left, the finished round's save stayed and Continue
// offered it.
TEST_F(ResumeMatchTest, ALeaveBeforeTheRematchsFirstWriteStillRemovesTheFinishedRoundsSave) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  ASSERT_TRUE(fakesd::has(resumePath("cnt"))) << "the finished round's save, which the card would not delete";

  fakesd::sim().failOpenWrite.insert(resumeTmpPath("cnt"));  // and the rematch's writes fail too
  input->click(Button::Confirm);                             // Play again
  frame();
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(pumpUntilTheRematchsFirstWriteFails("cnt")) << "the rematch's first write";
  EXPECT_TRUE(fakesd::has(resumePath("cnt")));

  fakesd::sim().failRemove.clear();  // the card can delete now; the write backoff has not passed, so no write is tried
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  input->press(Button::NavNext);
  frame();
  input->click(Button::Confirm);  // Leave
  frame();
  EXPECT_EQ(state(), "Leaving");
  EXPECT_FALSE(fakesd::has(resumePath("cnt"))) << "the finished round's save survived the Leave";
  EXPECT_EQ(GameSaveStore::peek("cnt", HASH_A), GameSaveStore::SaveState::None) << "Continue would offer it";
}

// The finished round's file can go before the rematch's write is done with it: the Over delete succeeds once the card
// recovers, or the write removes resume.bin and then fails to rename the new snapshot into place. Either way the new
// round's snapshot waits in resume.bin.tmp, and a delete that was still pending must not take it (a Leave inside the
// write backoff would). Which of the two happens depends on whether the loop takes the over snapshot before the VM
// publishes the new round's (a race this test does not fix); GameSaveStoreTest pins the counter for the second.
TEST_F(ResumeMatchTest, ARematchSnapshotWaitingInTheTmpFileSurvivesALeaveAfterAFailedOverDelete) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  ASSERT_TRUE(fakesd::has(resumePath("cnt")));

  input->click(Button::Confirm);  // Play again
  frame();
  ASSERT_EQ(state(), "Playing");
  fakesd::sim().failRemove.clear();                       // the card recovers ...
  fakesd::sim().failRename.insert(resumeTmpPath("cnt"));  // ... but will not move the new snapshot into place
  ASSERT_TRUE(pump([&] {
    fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
    return logHas("cannot rename " + resumeTmpPath("cnt"));
  }));
  EXPECT_FALSE(fakesd::has(resumePath("cnt"))) << "the finished round's file is gone";
  EXPECT_TRUE(fakesd::has(resumeTmpPath("cnt"))) << "and the new snapshot waits in the tmp file";

  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  input->press(Button::NavNext);
  frame();
  input->click(Button::Confirm);  // Leave
  frame();
  EXPECT_EQ(state(), "Leaving");
  EXPECT_TRUE(fakesd::has(resumeTmpPath("cnt"))) << "the pending delete took the new round's save";
  EXPECT_EQ(fakesd::bytesOf(resumeTmpPath("cnt")), resumeBytes(snapshotOf(0), 3));
  EXPECT_EQ(GameSaveStore::peek("cnt", HASH_A), GameSaveStore::SaveState::Valid) << "Continue finds it";
}

TEST_F(ResumeMatchTest, ARematchWhoseSetupErrorsStillRemovesTheFinishedRoundsSaveAtLeave) {
  // Round 1 ends normally; round 2's setup errors. The error view's Back is Leave.
  installGame("erg", R"(
local game = {}
local rounds = 0
function game.setup(ctx)
  rounds = rounds + 1
  if rounds > 1 then error("no second round") end
  return { taps = 0 }
end
function game.status(state)
  if state.taps >= 1 then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  return state
end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)");
  installPkg("erg");
  enter("erg");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("erg"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  ASSERT_TRUE(fakesd::has(resumePath("erg")));

  input->click(Button::Confirm);  // Play again: round 2's setup errors
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Error"; }));
  fakesd::sim().failRemove.clear();
  input->click(Button::Back);  // the error view's one control
  frame();
  EXPECT_EQ(state(), "Leaving");
  EXPECT_FALSE(fakesd::has(resumePath("erg")));
}

// The pending delete is not given up when the new round starts: it keeps retrying while the player plays, and stops
// for good once the round's first snapshot is written (PlayAgainAfterAFailedDeleteIsNotDeletedUnderTheNewRound).
TEST_F(ResumeMatchTest, TheOverDeleteKeepsRetryingInTheNewRoundUntilItsFirstWrite) {
  installGame("cnt", countingGame(1));
  installPkg("cnt");
  enter("cnt");
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 1)));
  fakesd::sim().failRemove.insert(resumePath("cnt"));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("cnt"));
  input->click(Button::Confirm);  // Play again, the card refusing the delete and the rematch's writes
  frame();
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(pumpUntilTheRematchsFirstWriteFails("cnt"));

  fakesd::sim().failRemove.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // the retry waits this long after a failed try
  ASSERT_TRUE(pump([&] { return !fakesd::has(resumePath("cnt")); })) << "no retry in Playing";

  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // and the write backoff passes
  ASSERT_TRUE(pumpToFile(resumeBytes(snapshotOf(0), 3))) << "the rematch's first snapshot";
  const size_t removes = fakesd::countOps("remove " + resumePath("cnt"));
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_EQ(fakesd::countOps("remove " + resumePath("cnt")), removes) << "no delete once the round has written";
  EXPECT_TRUE(fileIs(resumeBytes(snapshotOf(0), 3)));
}

// The Error view: a game commits a move (its write fails, so the snapshot is pending) and then errors.
class ResumeErrorTest : public ResumeMatchTest {
 protected:
  void reachErrorWithASnapshotPending() {
    installGame("err", ERROR_GAME);
    installPkg("err");
    enter("err");
    ASSERT_TRUE(pumpToFile(resumeBytes(nSnapshotOf(0), 1)));
    fakertos::arm();  // each tap's input reads the clock first, where the gate holds it
    tapCanvas(50, 50);
    frame();
    ASSERT_TRUE(fakertos::waitParked());
    fakertos::pass();  // tap 1 finishes: ver 2 is committed
    ASSERT_TRUE(waitFor([&] { return logHas("draw\t1"); }));
    // No loop pass has run since: ver 2 is unwritten, and its write is going to fail.
    fakesd::sim().failOpenWrite.insert(resumeTmpPath("err"));
    tapCanvas(50, 50);
    frame();
    ASSERT_TRUE(pump([&] { return logHas("cannot write " + resumeTmpPath("err")); }));
    ASSERT_TRUE(fakertos::waitParked());  // tap 2 is held in its input
    fakertos::release();                  // and now errors
    ASSERT_TRUE(pump([&] { return state() == "Error"; }));
    fakesd::sim().failOpenWrite.clear();
  }
};

TEST_F(ResumeErrorTest, LeavingFromTheErrorViewWritesNothing) {
  reachErrorWithASnapshotPending();
  ASSERT_FALSE(HasFatalFailure());
  const size_t opens = tmpOpens();
  input->click(Button::Back);
  frame();
  EXPECT_EQ(state(), "Leaving");
  EXPECT_TRUE(fileIs(resumeBytes(nSnapshotOf(0), 1))) << "the pending snapshot was written after the error";
  EXPECT_EQ(tmpOpens(), opens);
}

TEST_F(ResumeErrorTest, TheForcedExitFromTheErrorViewWritesNothing) {
  reachErrorWithASnapshotPending();
  ASSERT_FALSE(HasFatalFailure());
  const size_t opens = tmpOpens();
  sleep();
  EXPECT_TRUE(fileIs(resumeBytes(nSnapshotOf(0), 1))) << "the pending snapshot was written after the error";
  EXPECT_EQ(tmpOpens(), opens);
}

TEST_F(ResumeMatchTest, TheForcedExitOfAVmStuckInALockedBindingEndsWithinTheBoundAndStillWritesTheSnapshot) {
  // The activity's stop: join 500 ms, then abandon 500 ms, each late by at most one iteration; then the write.
  installGame("logger", match::LOGGING_GAME);
  installPkg("logger");
  // The loop's own write of the first snapshot fails, so the forced exit is what writes it.
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("logger"));
  enter("logger");
  ASSERT_TRUE(waitFor(match::roundStarted));
  showFrame();               // the tap below is dropped until the first frame is on the panel
  expectCleanPsram = false;  // the abandon leaks the VM and the store slot on purpose
  fakertos::arm(fakertos::At::Log);
  input->tap(CANVAS_X + 100, CANVAS_Y + 200);
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_TRUE(pump([&] { return logHas("cannot write " + resumeTmpPath("logger")); }));
  ASSERT_FALSE(fakesd::has(resumePath("logger")));
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // the failed write is not retried before this

  const uint64_t began = clockMs();
  exited = true;
  activityManager.exitHolding(*activity);
  const uint64_t elapsed = clockMs() - began;
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
  EXPECT_GE(elapsed, 2u * GameVM::ABANDON_WAIT_MS);
  // Two waits of 500 ms, and on the device one late iteration each: the join's 5 ms poll; the abandon's
  // poll and its settle of up to 10 ticks. docs/crosshatch/game-canvas.md records about 1,030 ms.
  EXPECT_LE(elapsed, 1030u);
  EXPECT_TRUE(logHas("did not stop within"));
  EXPECT_TRUE(logHas("VM stuck and not safely deletable"));
  // The snapshot committed before the call that hangs is written before the VM is let go.
  EXPECT_TRUE(fileIs(resumeBytes(Bytes{0x06, 0x00, 0x00}, 1)));
  fakertos::release();  // the leaked task ends at its next hook: the cancel flag is set
  EXPECT_TRUE(fakertos::waitNoTasks());
}

// ---- pass saves (epic-pass-and-play entry 9): pass-open and pass-hidden through the real match, VM, and store ----

// resume.bin's mode byte for a pass match (docs/crosshatch/formats.md): 0 solo, 1 pass.
constexpr uint8_t SAVED_PASS = 1;

// resume.bin of a pass match of `seats` seats, as GameSaveStore lays it out for the package `hash`.
Bytes passResumeBytes(const Bytes& snapshot, const uint16_t ver, const uint8_t seats,
                      const uint8_t (&hash)[GamePkg::HASH_BYTES] = HASH_A) {
  Bytes out = resumeBytes(snapshot, ver, hash);
  out[14] = SAVED_PASS;
  out[15] = seats;
  return out;
}

class PassResumeTest : public ResumeMatchTest {
 protected:
  void TearDown() override {
    renderer->onDisplay = nullptr;
    ResumeMatchTest::TearDown();
  }

  // Installs fixture `id` with HASH_A's .pkg and enters it as the launcher would, with its manifest: pass-open's (seats
  // 1..2, solo and pass) or, when `hidden`, pass-hidden's (seats 2..2, pass, hidden). `roster` is the caller's.
  void enterPass(const std::string& id, const bool hidden, const GameMatchActivity::Start start,
                 const GameCore::Roster& roster = GameCore::Roster::pass(2)) {
    match::installFixture(id);
    installPkg(id);
    GameCore::Manifest manifest = match::manifestOf(id);
    manifest.seatsMin = hidden ? 2 : 1;
    manifest.seatsMax = 2;
    manifest.modes =
        hidden ? GameCore::Manifest::MODE_PASS : GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS;
    manifest.hidden = hidden;
    enterWith(manifest, roster, start);
  }

  void enterWith(const GameCore::Manifest& manifest, const GameCore::Roster& roster,
                 const GameMatchActivity::Start start) {
    gameId = manifest.id;
    exited = false;
    activity = std::make_unique<GameMatchActivity>(*renderer, *input, manifest, roster, start);
    // A hidden match's first screen is the blank, never a frame: its tests draw each screen themselves.
    firstFramePending = !manifest.hidden;
    activity->onEnter();
  }

  // The game's resume.bin: its mode byte (14), seat count (15), and ver (16..17); -1 when there is no such file.
  int savedByte(const size_t at) const {
    const Bytes bytes = fakesd::bytesOf(resumePath(gameId));
    return bytes.size() > at ? bytes[at] : -1;
  }
  int savedMode() const { return savedByte(14); }
  int savedSeats() const { return savedByte(15); }
  int savedVer() const {
    const Bytes bytes = fakesd::bytesOf(resumePath(gameId));
    return bytes.size() > 17 ? bytes[16] | bytes[17] << 8 : -1;
  }
  // resume.bin holds a two-seat pass save at `ver`.
  bool savedPassAt(const int ver) const { return savedMode() == SAVED_PASS && savedSeats() == 2 && savedVer() == ver; }
  bool pumpToPassSave(const int ver) {
    return pump([&] { return savedPassAt(ver); });
  }

  // How many SD ops so far touched a resume file.
  static size_t resumeOps() {
    const auto& ops = fakesd::sim().ops;
    return static_cast<size_t>(std::count_if(
        ops.begin(), ops.end(), [](const std::string& op) { return op.find("resume.bin") != std::string::npos; }));
  }

  // pass-open: the canvas point at the middle of cell `cell` (1..9, row by row: 140 px squares from (27, 200)).
  void tapCell(const int cell) {
    tapCanvas(27 + (cell - 1) % 3 * 140 + 70, 200 + (cell - 1) / 3 * 140 + 70);
    frame();
    renderer->forget();
    showFrame();
  }
  // pass-open: X takes 1, 4, and 7 while O takes 2 and 3; the round ends.
  void playOpenToOver() {
    for (const int cell : {1, 2, 4, 3}) {
      ASSERT_NO_FATAL_FAILURE(tapCell(cell));
    }
    tapCanvas(27 + 70, 200 + 2 * 140 + 70);
    frame();
    ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  }
  bool drew(const std::string& text) const {
    const std::vector<std::string> texts = match::drawnTexts(*renderer);
    return std::find(texts.begin(), texts.end(), text) != texts.end();
  }

  // pass-hidden: the screen the match asked for is drawn; then a tap on its one target passes the device on: the
  // hand-off screen's "I'm ready" button (its middle is the screen's), or Result's banner at the bottom
  // (HiddenPassTest.TheReadyButtonIsMidScreenAndTheBannerAtTheBottom pins both places).
  void tapScreen() {
    input->tap(240, state() == "Result" ? 740 : 400);
    frame();
  }
  const GfxRenderer::Shown& lastPush() const { return renderer->shown.back(); }
  static bool holds(const GfxRenderer::Shown& push, const std::string& part) {
    return std::any_of(push.texts.begin(), push.texts.end(),
                       [&](const std::string& text) { return text.find(part) != std::string::npos; });
  }
  // pass-hidden from the start or a resume: the hand-off screen drawn, naming `seat` (0: unchecked), its button, and
  // the turn seat's frame drawn. The hand-off screen waits for the VM to name the round's first turn seat, so a render
  // before that pushes nothing and the loop asks again.
  void passTheBlank(const int seat = 0) {
    ASSERT_EQ(state(), "HandOff");
    const size_t pushes = renderer->shown.size();
    showFrame();
    if (renderer->shown.size() == pushes) showFrame();
    ASSERT_EQ(renderer->shown.size(), pushes + 1) << "no hand-off screen was pushed";
    EXPECT_EQ(lastPush().mode, HalDisplay::FULL_REFRESH);
    ASSERT_EQ(lastPush().texts.size(), 2u) << (lastPush().texts.empty() ? "no text" : lastPush().texts.front());
    if (seat > 0) EXPECT_EQ(lastPush().texts[0], "Player " + std::to_string(seat) + "'s turn");
    EXPECT_EQ(lastPush().texts[1], tr(STR_GAMES_READY));
    tapScreen();
    ASSERT_EQ(state(), "Playing");
    showFrame();
  }
  // pass-hidden: seat 1's frame is on the panel; its move passes the turn to seat 2 (Result).
  void moveToResult() {
    input->tap(CANVAS_X + 100, CANVAS_Y + 300);
    frame();
    ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  }
};

TEST_F(PassResumeTest, ANewOpenPassMatchWritesEachSnapshotAsAPassSaveAndOverDeletesIt) {
  enterPass("pass-open", false, GameMatchActivity::Start::New);
  ASSERT_TRUE(pumpToPassSave(1)) << "setup's snapshot, mode 1 n 2";
  ASSERT_NO_FATAL_FAILURE(tapCell(1));
  ASSERT_TRUE(pumpToPassSave(2));
  EXPECT_TRUE(drew("Player 2 (O) to move"));
  ASSERT_NO_FATAL_FAILURE(tapCell(2));
  ASSERT_TRUE(pumpToPassSave(3));
  for (const int cell : {4, 3}) {
    ASSERT_NO_FATAL_FAILURE(tapCell(cell));
  }
  tapCanvas(27 + 70, 200 + 2 * 140 + 70);  // X completes 1-4-7
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_FALSE(fakesd::has(resumePath("pass-open")));
  EXPECT_FALSE(fakesd::has(resumeTmpPath("pass-open")));
  sleep();
  EXPECT_FALSE(fakesd::has(resumePath("pass-open"))) << "the forced exit in the end-of-round menu writes nothing";
}

TEST_F(PassResumeTest, LeavingAPassMatchKeepsItsPassSave) {
  enterPass("pass-open", false, GameMatchActivity::Start::New);
  ASSERT_NO_FATAL_FAILURE(tapCell(1));
  ASSERT_TRUE(pumpToPassSave(2));
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  input->press(Button::NavNext);  // Leave: the second option, by keys
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "Leaving");
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  EXPECT_TRUE(savedPassAt(2)) << "Leave keeps the pass save";
}

TEST_F(PassResumeTest, ADeleteTheCardRefusesAtAPassMatchsOverIsRetriedAsInSolo) {
  enterPass("pass-open", false, GameMatchActivity::Start::New);
  ASSERT_TRUE(pumpToPassSave(1));
  fakesd::sim().failRemove.insert(resumePath("pass-open"));
  ASSERT_NO_FATAL_FAILURE(playOpenToOver());
  EXPECT_TRUE(logHas("cannot delete " + resumePath("pass-open")));
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_TRUE(fakesd::has(resumePath("pass-open"))) << "the card still refuses";
  fakesd::sim().failRemove.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // the retry waits this long after a failed try
  ASSERT_TRUE(pump([&] { return !fakesd::has(resumePath("pass-open")); })) << "the retry never came";
}

TEST_F(PassResumeTest, AHiddenPassMatchWritesItsSnapshotsInHandOffAndResult) {
  enterPass("pass-hidden", true, GameMatchActivity::Start::New);
  ASSERT_EQ(state(), "HandOff");
  ASSERT_TRUE(pumpToPassSave(1)) << "setup's snapshot, written on the blank";
  EXPECT_EQ(state(), "HandOff");
  ASSERT_NO_FATAL_FAILURE(passTheBlank(1));
  EXPECT_TRUE(holds(lastPush(), "Player 1's secret: apple"));
  ASSERT_NO_FATAL_FAILURE(moveToResult());
  ASSERT_TRUE(pumpToPassSave(2)) << "the move's snapshot, written in Result";
  EXPECT_EQ(state(), "Result");
}

// The hidden match's write fails in Result and succeeds on the blank after the backoff.
TEST_F(PassResumeTest, AHiddenPassMatchsWriteThatFailedInResultIsWrittenOnTheBlank) {
  enterPass("pass-hidden", true, GameMatchActivity::Start::New);
  ASSERT_TRUE(pumpToPassSave(1));
  ASSERT_NO_FATAL_FAILURE(passTheBlank(1));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("pass-hidden"));
  ASSERT_NO_FATAL_FAILURE(moveToResult());
  ASSERT_TRUE(pump([&] { return logHas("cannot write " + resumeTmpPath("pass-hidden")); }));
  showFrame();  // Result's banner, which the tap below passes
  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
  ASSERT_TRUE(pumpToPassSave(2));
  EXPECT_EQ(state(), "HandOff");
}

// The save's roster wins over the caller's: a solo caller's Continue on a hidden pass save shows the blank first, and
// its tap shows seat 2 at the saved move.
TEST_F(PassResumeTest, AHiddenPassSaveResumesOnTheBlankAndItsTapShowsTheSavedTurnSeat) {
  enterPass("pass-hidden", true, GameMatchActivity::Start::New);
  ASSERT_NO_FATAL_FAILURE(passTheBlank(1));
  ASSERT_NO_FATAL_FAILURE(moveToResult());
  ASSERT_TRUE(pumpToPassSave(2));
  sleep();
  ASSERT_TRUE(savedPassAt(2));
  const Bytes saved = fakesd::bytesOf(resumePath("pass-hidden"));

  fakelog::clearLines();
  renderer->shown.clear();
  enterPass("pass-hidden", true, GameMatchActivity::Start::Resume, GameCore::Roster::solo());
  EXPECT_TRUE(logHas("pass-hidden: resuming the save's roster: pass, 2 seat(s)"));
  EXPECT_TRUE(logHas("pass-hidden: Starting -> HandOff on Started"));
  ASSERT_TRUE(pump([&] { return logHas("Resuming at ver 2"); }));
  for (int i = 0; i < 20; ++i) frame();
  // Every seat's draws, the saved turn seat's (2) included: none before the blank's tap.
  EXPECT_EQ(fakelog::countLines("draw for seat "), 0u) << "a seat was drawn before the blank's tap";
  ASSERT_NO_FATAL_FAILURE(passTheBlank(2));
  EXPECT_TRUE(holds(lastPush(), "Player 2's secret: river"));
  EXPECT_TRUE(holds(lastPush(), "Moves: 1")) << "the saved snapshot, not setup's";
  for (const GfxRenderer::Shown& push : renderer->shown) EXPECT_FALSE(holds(push, "apple"));
  // After the tap, exactly one draw, seat 2's: any draw before the blank's tap would make the count more than one.
  EXPECT_EQ(fakelog::countLines("draw for seat 2"), 1u);
  EXPECT_EQ(fakelog::countLines("draw for seat "), 1u) << "another seat was drawn, or seat 2 before the tap";
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_EQ(fakesd::bytesOf(resumePath("pass-hidden")), saved) << "the restored snapshot is not written again";
}

TEST_F(PassResumeTest, AnOpenPassSaveResumesInPlayOnTheSavedTurnSeatsFrame) {
  enterPass("pass-open", false, GameMatchActivity::Start::New);
  ASSERT_NO_FATAL_FAILURE(tapCell(1));
  ASSERT_TRUE(pumpToPassSave(2));
  sleep();
  ASSERT_TRUE(savedPassAt(2));

  fakelog::clearLines();
  enterPass("pass-open", false, GameMatchActivity::Start::Resume, GameCore::Roster::solo());
  EXPECT_TRUE(logHas("pass-open: resuming the save's roster: pass, 2 seat(s)"));
  EXPECT_EQ(state(), "Playing");
  renderer->forget();
  showFrame();
  EXPECT_TRUE(drew("Player 2 (O) to move")) << "seat 2's frame, at the saved board";
  EXPECT_TRUE(logHas("Resuming at ver 2"));
  // Seat 2 moves: the resumed match goes on writing a two-seat pass save.
  ASSERT_NO_FATAL_FAILURE(tapCell(2));
  EXPECT_TRUE(logHas("apply seat 2 cell 2"));
  ASSERT_TRUE(pumpToPassSave(3));
  EXPECT_TRUE(drew("Player 1 (X) to move"));
}

// Sleep in a hidden match's Result with the write failed: the forced exit writes the snapshot, and Continue resumes at
// it, on the blank.
TEST_F(PassResumeTest, AHiddenMatchSleptInResultContinuesAtTheSnapshotTheSleepWrote) {
  enterPass("pass-hidden", true, GameMatchActivity::Start::New);
  ASSERT_TRUE(pumpToPassSave(1));
  ASSERT_NO_FATAL_FAILURE(passTheBlank(1));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("pass-hidden"));
  ASSERT_NO_FATAL_FAILURE(moveToResult());
  ASSERT_TRUE(pump([&] { return logHas("cannot write " + resumeTmpPath("pass-hidden")); }));
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
  ASSERT_TRUE(savedPassAt(1));
  sleep();
  ASSERT_TRUE(savedPassAt(2)) << "the forced exit writes the move";

  fakelog::clearLines();
  enterPass("pass-hidden", true, GameMatchActivity::Start::Resume, GameCore::Roster::solo());
  ASSERT_NO_FATAL_FAILURE(passTheBlank(2));
  EXPECT_TRUE(holds(lastPush(), "Player 2's secret: river"));
  EXPECT_TRUE(holds(lastPush(), "Moves: 1"));
  EXPECT_TRUE(logHas("Resuming at ver 2"));
}

TEST_F(PassResumeTest, AnOpenMatchSleptMidMoveContinuesAtTheSnapshotTheSleepWrote) {
  enterPass("pass-open", false, GameMatchActivity::Start::New);
  ASSERT_TRUE(pumpToPassSave(1));
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("pass-open"));
  ASSERT_NO_FATAL_FAILURE(tapCell(1));
  ASSERT_TRUE(pump([&] { return logHas("cannot write " + resumeTmpPath("pass-open")); }));
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);
  ASSERT_TRUE(savedPassAt(1));
  sleep();
  ASSERT_TRUE(savedPassAt(2)) << "the forced exit writes the move";

  fakelog::clearLines();
  enterPass("pass-open", false, GameMatchActivity::Start::Resume);
  renderer->forget();
  showFrame();
  EXPECT_TRUE(drew("Player 2 (O) to move"));
  EXPECT_TRUE(logHas("Resuming at ver 2"));
}

// The reverse of the solo-caller cases: a solo save resumed by a caller that passes a pass roster plays solo, and its
// writes stay mode 0, n 1.
TEST_F(PassResumeTest, ASoloSaveResumedWithAPassCallersRosterPlaysSolo) {
  enterPass("pass-open", false, GameMatchActivity::Start::New, GameCore::Roster::solo());
  ASSERT_NO_FATAL_FAILURE(tapCell(1));
  ASSERT_TRUE(pump([&] { return savedMode() == 0 && savedSeats() == 1 && savedVer() == 2; }));
  sleep();

  fakelog::clearLines();
  enterPass("pass-open", false, GameMatchActivity::Start::Resume, GameCore::Roster::pass(2));
  EXPECT_TRUE(logHas("pass-open: resuming the save's roster: solo, 1 seat(s)"));
  EXPECT_EQ(state(), "Playing");
  renderer->forget();
  showFrame();
  EXPECT_TRUE(drew("Player 1 (O) to move")) << "solo: seat 1 places both marks";
  ASSERT_NO_FATAL_FAILURE(tapCell(2));
  EXPECT_TRUE(logHas("apply seat 1 cell 2"));
  ASSERT_TRUE(pump([&] { return savedVer() == 3; }));
  EXPECT_EQ(savedMode(), 0);
  EXPECT_EQ(savedSeats(), 1);
  // The VM's Session plays the solo roster: at the round's end `over` reaches seat 1 only (a pass roster would give
  // seat 2 one too).
  for (const int cell : {4, 3}) {
    ASSERT_NO_FATAL_FAILURE(tapCell(cell));
  }
  tapCanvas(27 + 70, 200 + 2 * 140 + 70);  // X completes 1-4-7
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_EQ(fakelog::countLines("over for seat 1"), 1u);
  EXPECT_EQ(fakelog::countLines("over for seat 2"), 0u);
}

// A hidden game that also offers solo: its solo save resumes in Playing, never on the blank, whatever the caller
// passes.
TEST_F(PassResumeTest, AHiddenGamesSoloSaveResumesInPlayingNotHandOff) {
  match::installFixture("pass-open");
  installPkg("pass-open");
  GameCore::Manifest manifest = match::manifestOf("pass-open");
  manifest.seatsMax = 2;
  manifest.modes = GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS;
  manifest.hidden = true;
  enterWith(manifest, GameCore::Roster::solo(), GameMatchActivity::Start::New);
  firstFramePending = true;
  ASSERT_EQ(state(), "Playing");
  ASSERT_NO_FATAL_FAILURE(tapCell(1));
  ASSERT_TRUE(pump([&] { return savedMode() == 0 && savedSeats() == 1 && savedVer() == 2; }));
  sleep();

  fakelog::clearLines();
  enterWith(manifest, GameCore::Roster::pass(2), GameMatchActivity::Start::Resume);
  EXPECT_TRUE(logHas("pass-open: resuming the save's roster: solo, 1 seat(s)"));
  EXPECT_EQ(state(), "Playing");
  EXPECT_TRUE(logHas("pass-open: Starting -> Playing on Started"));
  EXPECT_FALSE(logHas("-> HandOff"));
}

// A resumed hidden pass match played to its end: Over deletes the save, and Play again writes the new round's pass
// save from the blank.
TEST_F(PassResumeTest, AResumedHiddenMatchPlayedToOverDeletesItsSaveAndPlayAgainSavesFromTheBlank) {
  enterPass("pass-hidden", true, GameMatchActivity::Start::New);
  ASSERT_NO_FATAL_FAILURE(passTheBlank(1));
  ASSERT_NO_FATAL_FAILURE(moveToResult());
  ASSERT_TRUE(pumpToPassSave(2));
  sleep();

  fakelog::clearLines();
  enterPass("pass-hidden", true, GameMatchActivity::Start::Resume, GameCore::Roster::solo());
  // Seat 2 and then seat 1 move, each through Result and the blank; seat 2's next move is the fourth and ends the
  // round.
  for (int turn = 0; turn < 2; ++turn) {
    ASSERT_NO_FATAL_FAILURE(passTheBlank(2 - turn));
    ASSERT_NO_FATAL_FAILURE(moveToResult());
    showFrame();  // Result's banner, which the tap below passes
    tapScreen();
    ASSERT_EQ(state(), "HandOff");
  }
  ASSERT_NO_FATAL_FAILURE(passTheBlank(2));
  EXPECT_TRUE(holds(lastPush(), "Moves: 3"));
  input->tap(CANVAS_X + 100, CANVAS_Y + 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_FALSE(fakesd::has(resumePath("pass-hidden")));
  EXPECT_FALSE(fakesd::has(resumeTmpPath("pass-hidden")));

  input->click(Button::Confirm);  // Play again is the first option
  frame();
  EXPECT_EQ(state(), "HandOff");
  ASSERT_TRUE(pump([&] { return savedMode() == SAVED_PASS && savedSeats() == 2; })) << "the new round's save";
  EXPECT_EQ(state(), "HandOff");
}

// A save of this package this host cannot start (GameSaveStore's Unstartable: cross-story review rows 2 and 10) is not
// replaced by a new match: the match stops in the error view with its own reason, the file stays as it was, and Back
// from the error view (Leave) and the forced exit after it keep it too. The refusal reads the card with a buffer of its
// own, so the store's roster is never the save's.
TEST_F(PassResumeTest, ASaveThisHostCannotStartStopsInTheErrorViewWithItsOwnReasonAndIsKept) {
  installGame("trio", countingGame(5));
  installPkg("trio");
  GameCore::Manifest trio = match::manifestOf("trio");
  trio.seatsMin = 2;
  trio.seatsMax = 3;
  trio.modes = GameCore::Manifest::MODE_PASS;
  match::installFixture("pass-open");
  installPkg("pass-open");
  GameCore::Manifest openGame = match::manifestOf("pass-open");
  openGame.seatsMax = 2;
  openGame.modes = GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS;
  match::installFixture("pass-hidden");
  installPkg("pass-hidden");
  GameCore::Manifest hidden = match::manifestOf("pass-hidden");
  hidden.seatsMin = 2;
  hidden.seatsMax = 2;
  hidden.modes = GameCore::Manifest::MODE_PASS;
  hidden.hidden = true;
  Bytes unknownMode = passResumeBytes(snapshotOf(2), 3, 2);
  unknownMode[14] = 7;
  struct Case {
    const char* name;
    const GameCore::Manifest* game;
    Bytes file;
    const char* reason;  // what peek logs as kept
  };
  const Case cases[] = {
      {"n 3 of a 2..3 game on this two-seat host", &trio, passResumeBytes(snapshotOf(2), 3, 3), "seats not startable"},
      {"more seats than the game has", &openGame, passResumeBytes(snapshotOf(2), 3, 3), "seats not startable"},
      {"an unknown mode byte", &openGame, unknownMode, "unknown mode"},
      {"a solo save of a pass-only game", &hidden, resumeBytes(snapshotOf(2), 3), "mode not startable"},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    fakelog::clearLines();
    const std::string id = c.game->id;
    fakesd::addFile(resumePath(id), c.file);
    enterWith(*c.game, GameCore::Roster::pass(2), GameMatchActivity::Start::Resume);
    EXPECT_EQ(state(), "Error");
    EXPECT_TRUE(logHas(id + ": resume.bin is a save this host cannot start; not starting a new match over it"));
    EXPECT_TRUE(logHas(" is a save that cannot be resumed here: " + std::string(c.reason) + "; the file is kept"));
    EXPECT_TRUE(logHas(tr(STR_GAMES_RESUME_NOT_HERE)));
    EXPECT_FALSE(logHas(tr(STR_GAMES_RESUME_FAILED))) << "the error view says the save cannot be continued here";
    // The error view draws the whole reason, wrapped but never cut (fix review F13).
    if (screen::RecordingTarget::newest()) screen::RecordingTarget::newest()->forget();
    activity->render(RenderLock(*activity));
    activityManager.markRendered();
    ASSERT_NE(screen::RecordingTarget::newest(), nullptr);
    std::string drawn = screen::RecordingTarget::newest()->joined();
    std::replace(drawn.begin(), drawn.end(), '\n', ' ');
    EXPECT_NE(drawn.find(tr(STR_GAMES_RESUME_NOT_HERE)), std::string::npos) << "the reason is cut: " << drawn;
    for (int i = 0; i < 10; ++i) {
      input->tap(CANVAS_X + 50, CANVAS_Y + 50);
      frame();
    }
    EXPECT_FALSE(logHas("setup ran")) << "no game ran";
    EXPECT_FALSE(logHas("Round started"));
    EXPECT_FALSE(logHas("Resuming at ver"));
    EXPECT_EQ(fakesd::bytesOf(resumePath(id)), c.file);
    EXPECT_EQ(tmpOpens(), 0u);
    input->click(Button::Back);  // Leave from the error view
    frame();
    EXPECT_EQ(state(), "Leaving");
    EXPECT_EQ(fakesd::bytesOf(resumePath(id)), c.file) << "Back from the error view keeps it";
    sleep();
    EXPECT_EQ(fakesd::bytesOf(resumePath(id)), c.file) << "and the forced exit after it";
    EXPECT_EQ(tmpOpens(), 0u);
  }
}

// The refusal's second read (GameSaveStore::peekResume) does not see what the first saw: a card fault on that read
// (Unreadable), or a file rewritten between the two into a save that reads (Valid). Neither rules out a save, so the
// match refuses, "could not be checked again", and the file is kept through the match, Back, and the forced exit.
TEST_F(PassResumeTest, ASecondReadThatFaultsOrFindsAChangedSaveStopsInTheErrorViewAndKeepsIt) {
  const Bytes valid = passResumeBytes(snapshotOf(2), 3, 2);
  for (const bool faults : {true, false}) {
    SCOPED_TRACE(faults ? "the second read faults" : "the file changed between the reads");
    fakelog::clearLines();
    const std::string path = resumePath("pass-open");
    // Refused by the first read and kept: a save this host cannot start, or a malformed one (one seat in pass).
    fakesd::addFile(path, passResumeBytes(snapshotOf(2), 3, faults ? 3 : 1));
    fakesd::sim().opens.erase(path);
    fakesd::sim().onOpen = [&](const std::string& opened, const int count) {
      if (opened != path || count != 2) return;  // the first open is loadResume's, the second peekResume's
      if (faults) {
        fakesd::sim().failOpen.insert(path);
      } else {
        fakesd::addFile(path, valid);
      }
    };
    enterPass("pass-open", false, GameMatchActivity::Start::Resume);
    fakesd::sim().onOpen = nullptr;
    fakesd::sim().failOpen.clear();
    EXPECT_EQ(fakesd::sim().opens[path], 2);
    const Bytes saved = fakesd::bytesOf(path);
    EXPECT_EQ(state(), "Error");
    EXPECT_TRUE(logHas("pass-open: resume.bin could not be checked again; not starting a new match over it"));
    EXPECT_TRUE(logHas(tr(STR_GAMES_RESUME_FAILED)));
    EXPECT_FALSE(logHas("no usable resume.bin"));
    for (int i = 0; i < 10; ++i) {
      input->tap(CANVAS_X + 50, CANVAS_Y + 50);
      frame();
    }
    EXPECT_FALSE(logHas("Round started")) << "no game ran";
    EXPECT_FALSE(logHas("tap for seat"));
    EXPECT_EQ(fakesd::bytesOf(path), saved);
    EXPECT_EQ(tmpOpens(), 0u);
    input->click(Button::Back);  // Leave from the error view
    frame();
    EXPECT_EQ(state(), "Leaving");
    EXPECT_EQ(fakesd::bytesOf(path), saved) << "Back from the error view keeps it";
    sleep();
    EXPECT_EQ(fakesd::bytesOf(path), saved) << "and the forced exit after it";
    EXPECT_EQ(tmpOpens(), 0u);
  }
}

TEST_F(PassResumeTest, AnUnreadablePassSaveStopsInTheErrorViewAndIsKept) {
  const Bytes saved = passResumeBytes(snapshotOf(2), 3, 2);
  fakesd::addFile(resumePath("pass-open"), saved);
  fakesd::sim().failReadAt[resumePath("pass-open")] = 0;
  enterPass("pass-open", false, GameMatchActivity::Start::Resume);
  EXPECT_EQ(state(), "Error");
  EXPECT_TRUE(logHas("resume.bin could not be read; not starting a new match over it"));
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_FALSE(logHas("Round started"));
  fakesd::sim().failReadAt.clear();
  EXPECT_EQ(fakesd::bytesOf(resumePath("pass-open")), saved);
  sleep();
  EXPECT_EQ(fakesd::bytesOf(resumePath("pass-open")), saved);
}

// No usable save: none, another package's, and a malformed one (a seat count no pass save has) each start a new match
// with the caller's pass roster, whose first snapshot replaces the file. A save this host cannot start is refused
// instead (ASaveThisHostCannotStartStopsInTheErrorViewWithItsOwnReasonAndIsKept).
TEST_F(PassResumeTest, WithNoUsableSaveContinueStartsANewMatchWithTheCallersRoster) {
  struct Case {
    const char* name;
    Bytes file;  // empty: no file
    const char* logged;
  };
  const Case cases[] = {
      {"none", {}, nullptr},
      {"other package", passResumeBytes(snapshotOf(2), 3, 2, HASH_B), ": other package"},
      {"a pass save of one seat", passResumeBytes(snapshotOf(2), 3, 1), ": bad seat count"},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    fakelog::clearLines();
    fakesd::removeEntry(resumePath("pass-open"));
    if (!c.file.empty()) fakesd::addFile(resumePath("pass-open"), c.file);
    enterPass("pass-open", false, GameMatchActivity::Start::Resume);
    if (c.logged) {
      EXPECT_TRUE(logHas("discarded " + resumePath("pass-open") + c.logged));
    }
    EXPECT_TRUE(logHas("pass-open: no usable resume.bin; starting a new match"));
    EXPECT_FALSE(logHas("resuming the save's roster"));
    EXPECT_EQ(state(), "Playing");
    renderer->forget();
    showFrame();
    EXPECT_TRUE(drew("Player 1 (X) to move")) << "a new pass match";
    EXPECT_TRUE(pumpToPassSave(1)) << "its first snapshot, mode 1 n 2";
    sleep();
  }
}

// The forced exit of a hidden match in Playing with a snapshot whose write failed: the blank is pushed before any
// resume.bin op, and the snapshot is written after it (deferred-work ## 5.6).
TEST_F(PassResumeTest, AHiddenForcedExitPushesTheBlankBeforeAnyResumeOpAndThenWritesThePendingSnapshot) {
  fakesd::sim().failOpenWrite.insert(resumeTmpPath("pass-hidden"));
  enterPass("pass-hidden", true, GameMatchActivity::Start::New);
  ASSERT_TRUE(pump([&] { return logHas("cannot write " + resumeTmpPath("pass-hidden")); })) << "setup's snapshot";
  ASSERT_NO_FATAL_FAILURE(passTheBlank(1));
  EXPECT_TRUE(holds(lastPush(), "Player 1's secret: apple"));
  ASSERT_FALSE(fakesd::has(resumePath("pass-hidden")));
  fakesd::sim().failOpenWrite.clear();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS);  // no loop pass since: the write is pending and due

  const size_t opsBefore = resumeOps();
  size_t opsAtPush = 0;
  size_t pushes = 0;
  renderer->onDisplay = [&] {
    ++pushes;
    opsAtPush = resumeOps();
  };
  sleep();
  renderer->onDisplay = nullptr;
  ASSERT_EQ(pushes, 1u);
  EXPECT_TRUE(lastPush().texts.empty()) << "the push was the blank";
  EXPECT_EQ(lastPush().mode, HalDisplay::HALF_REFRESH);
  EXPECT_EQ(opsAtPush, opsBefore) << "a resume.bin op ran before the blank was pushed";
  EXPECT_GT(resumeOps(), opsBefore);
  EXPECT_TRUE(savedPassAt(1)) << "the pending snapshot was written after the blank";
}

// ---- the waits (R11): elapsed time, not polls ----

class ResumeVmTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    fakearena::reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;
    replay.loadFonts(*renderer);
    viewport = GameViewport::forRenderer(*renderer);
  }

  void TearDown() override {
    fakertos::release();
    if (vm && !vm->stop(5000)) {
      ADD_FAILURE() << "the VM would not stop; leaking it rather than freeing memory its task may use";
      (void)vm.release();
      expectCleanPsram = false;
    }
    vm.reset();
    store.reset();
    fakearena::reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;
    ScreenTest::TearDown();
  }

  bool prepare(const std::string& id) {
    store = std::make_unique<MatchStore>();
    if (!store->allocate(id.c_str(), static_cast<uint32_t>(clockMs()))) return false;
    GameAssets assets;
    if (assets.load(id.c_str(), store->saves(), store->slot()) != GameAssets::LoadResult::Ok) return false;
    vm = GameVM::create(std::move(assets), viewport, replay, id.c_str(), store->slot(), GameCore::Roster::solo());
    return vm != nullptr;
  }
  bool startAndWaitFirstFrame() {
    return vm->start() && waitFor([&] { return vm->frameGen() >= 1; });
  }

  static GameCore::GameEvent tapAt(const int x, const int y) {
    GameCore::GameEvent event;
    event.kind = GameCore::EventKind::Tap;
    event.x = static_cast<int16_t>(x);
    event.y = static_cast<int16_t>(y);
    return event;
  }

  // A VM held inside ch.log, a locked binding: it neither joins nor can abandon delete it.
  void stickInALockedBinding() {
    installGame("logger", match::LOGGING_GAME);
    ASSERT_TRUE(prepare("logger"));
    ASSERT_TRUE(startAndWaitFirstFrame());
    ASSERT_TRUE(waitFor(match::roundStarted));
    expectCleanPsram = false;  // the abandon leaks it on purpose
    fakertos::arm(fakertos::At::Log);
    vm->postInput(tapAt(100, 200));
    ASSERT_TRUE(fakertos::waitParked());
    vm->cancel();
  }

  void letTheLeakedTaskEnd() {
    fakertos::release();
    EXPECT_TRUE(fakertos::waitNoTasks());
  }

  FrameReplay replay;
  GameViewport viewport;
  std::unique_ptr<MatchStore> store;
  std::unique_ptr<GameVM> vm;
};

TEST_F(ResumeVmTest, JoinEndsFiveHundredMillisecondsOfClockAfterItBegan) {
  stickInALockedBinding();
  const uint64_t began = clockMs();
  EXPECT_FALSE(vm->join(500));
  const uint64_t elapsed = clockMs() - began;
  EXPECT_GE(elapsed, 500u);
  EXPECT_LE(elapsed, 500u + 10u) << "at most one poll late";
}

TEST_F(ResumeVmTest, JoinCountsElapsedTimeNotPollsWhenTheClockMovesFasterThanItsPolls) {
  stickInALockedBinding();
  const uint64_t began = clockMs();
  bool joined = true;
  {
    // 25 ms of clock per real millisecond: each 5 ms poll spans about 130 ms of it, so 500 ms are up in
    // about four polls. A join that counted its 100 polls would take about 12,500 fake ms; the limit
    // below is loose enough for a loaded host to overshoot by several polls.
    Ticker ticker(25);
    joined = vm->join(500);
  }
  const uint64_t elapsed = clockMs() - began;
  EXPECT_FALSE(joined);
  EXPECT_GE(elapsed, 500u);
  EXPECT_LT(elapsed, 6000u) << "the wait counted polls, not time";
}

TEST_F(ResumeVmTest, JoinReturnsAtOnceForATaskThatEndsBeforeItsTime) {
  installGame("tracer-lite", match::STILL_GAME);
  ASSERT_TRUE(prepare("tracer-lite"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  vm->cancel();
  const uint64_t began = clockMs();
  EXPECT_TRUE(vm->join(500));
  EXPECT_LT(clockMs() - began, 500u);
}

TEST_F(ResumeVmTest, AbandonEndsFiveHundredMillisecondsOfClockAfterItBegan) {
  stickInALockedBinding();
  EXPECT_FALSE(vm->join(50));
  const uint64_t began = clockMs();
  EXPECT_FALSE(GameVM::abandon(std::move(vm)));
  const uint64_t elapsed = clockMs() - began;
  EXPECT_GE(elapsed, static_cast<uint64_t>(GameVM::ABANDON_WAIT_MS));
  // One iteration late at most: the poll, and the settle of up to ten ticks.
  EXPECT_LE(elapsed, GameVM::ABANDON_WAIT_MS + 5u + 10u + 10u);
  EXPECT_TRUE(logHas("VM stuck and not safely deletable"));
  letTheLeakedTaskEnd();
}

TEST_F(ResumeVmTest, AbandonCountsElapsedTimeNotPollsWhenTheClockMovesFasterThanItsPolls) {
  stickInALockedBinding();
  EXPECT_FALSE(vm->join(50));
  const uint64_t began = clockMs();
  bool abandoned = true;
  {
    // As for join: a poll-counting abandon would take about 12,500 fake ms here.
    Ticker ticker(25);
    abandoned = GameVM::abandon(std::move(vm));
  }
  const uint64_t elapsed = clockMs() - began;
  EXPECT_FALSE(abandoned);
  EXPECT_GE(elapsed, 500u);
  EXPECT_LT(elapsed, 6000u) << "the wait counted polls, not time";
  letTheLeakedTaskEnd();
}

TEST_F(ResumeVmTest, ACancelledStepStillPublishesTheMoveItCommitted) {
  installGame("cancelling", CANCEL_GAME);
  ASSERT_TRUE(prepare("cancelling"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));
  std::vector<uint8_t> out(GameCore::SNAPSHOT_BYTES);
  SnapshotMailbox::Taken taken;
  ASSERT_TRUE(vm->committed().take(out, taken));
  ASSERT_EQ(taken.ver, 1u);

  fakertos::arm();  // the draw after the move reads the clock: held there, the move already committed
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  vm->cancel();
  fakertos::release();  // the draw spins on, and the next hook ends it Cancelled
  ASSERT_TRUE(vm->join(5000));
  EXPECT_FALSE(vm->failed());
  EXPECT_TRUE(logHas("VM cancelled"));
  ASSERT_TRUE(vm->committed().take(out, taken)) << "a Cancelled step dropped the snapshot it committed";
  EXPECT_EQ(taken.ver, 2u);
  EXPECT_FALSE(taken.over);
}

TEST_F(ResumeVmTest, AMoveWhoseStatusWasCancelledIsNotPublishedAndThePreviousSnapshotStays) {
  installGame("statusspin", STATUS_SPIN_GAME);
  ASSERT_TRUE(prepare("statusspin"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));
  fakertos::arm();  // the status of the snapshot after the move: the move is committed, its status is not
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  vm->cancel();
  fakertos::release();
  ASSERT_TRUE(vm->join(5000));
  std::vector<uint8_t> out(GameCore::SNAPSHOT_BYTES);
  SnapshotMailbox::Taken taken;
  ASSERT_TRUE(vm->committed().take(out, taken));
  EXPECT_EQ(taken.ver, 1u) << "the snapshot whose status never finished was published";
  EXPECT_FALSE(taken.over);
  EXPECT_FALSE(vm->committed().pending());
}

TEST_F(ResumeVmTest, ADrawCancelledAfterTheStatusSettledPublishesTheMoveWithItsOverFlag) {
  installGame("overspin", OVER_DRAW_SPIN_GAME);
  ASSERT_TRUE(prepare("overspin"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));
  fakertos::arm();
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  vm->cancel();
  fakertos::release();
  ASSERT_TRUE(vm->join(5000));
  std::vector<uint8_t> out(GameCore::SNAPSHOT_BYTES);
  SnapshotMailbox::Taken taken;
  ASSERT_TRUE(vm->committed().take(out, taken));
  EXPECT_EQ(taken.ver, 2u);
  EXPECT_TRUE(taken.over) << "the status of that snapshot had been computed";
}

TEST_F(ResumeVmTest, APlayAgainCancelledBeforeItsStatusPublishesNothingAndTheOverSnapshotIsUntouched) {
  installGame("restartspin", RESTART_STATUS_SPIN_GAME);
  ASSERT_TRUE(prepare("restartspin"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));
  std::vector<uint8_t> out(GameCore::SNAPSHOT_BYTES);
  SnapshotMailbox::Taken taken;
  ASSERT_TRUE(vm->committed().take(out, taken));  // ver 1
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(waitFor([&] { return vm->committed().pending(); }));
  ASSERT_TRUE(vm->committed().take(out, taken));
  ASSERT_EQ(taken.ver, 2u);
  ASSERT_TRUE(taken.over);

  fakertos::arm();  // round 2's setup has run and committed ver 3; its status is held
  vm->playAgain();
  ASSERT_TRUE(fakertos::waitParked());
  vm->cancel();
  fakertos::release();
  ASSERT_TRUE(vm->join(5000));
  EXPECT_FALSE(vm->committed().pending()) << "the new round's unfinished snapshot was published";
}

TEST_F(ResumeVmTest, JoinIsLateByAtMostOnePollForTimeoutsNoPollDividesInto) {
  stickInALockedBinding();
  for (const uint32_t timeout : {503u, 507u}) {
    const uint64_t began = clockMs();
    EXPECT_FALSE(vm->join(timeout));
    const uint64_t elapsed = clockMs() - began;
    EXPECT_GE(elapsed, timeout);
    EXPECT_LE(elapsed, timeout + GameVM::STOP_POLL_MS) << "more than one poll late";
  }
}

TEST_F(ResumeVmTest, AVmPublishesEachNewVerOnceAndNothingForARejectedMove) {
  match::installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));  // the publish comes before this line
  std::vector<uint8_t> out(GameCore::SNAPSHOT_BYTES);
  SnapshotMailbox::Taken taken;
  ASSERT_TRUE(vm->committed().take(out, taken));
  EXPECT_EQ(taken.ver, 1u);
  EXPECT_FALSE(taken.over);
  EXPECT_GT(taken.length, 0u);

  // A tap on the banner is rejected: the frame changes, the snapshot does not.
  const uint32_t before = vm->frameGen();
  vm->postInput(tapAt(100, 50));
  ASSERT_TRUE(waitFor([&] { return vm->frameGen() > before; }));
  EXPECT_FALSE(vm->committed().pending());

  const uint32_t beforeMove = vm->frameGen();
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(waitFor([&] { return vm->frameGen() > beforeMove; }));
  ASSERT_TRUE(waitFor([&] { return vm->committed().pending(); }));
  ASSERT_TRUE(vm->committed().take(out, taken));
  EXPECT_EQ(taken.ver, 2u);
}

TEST_F(ResumeVmTest, AVmSeededWithASnapshotRestoresItWithoutPublishingUntilItsNextMove) {
  match::installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));
  std::vector<uint8_t> first(GameCore::SNAPSHOT_BYTES);
  SnapshotMailbox::Taken taken;
  ASSERT_TRUE(vm->committed().take(first, taken));
  first.resize(taken.length);
  ASSERT_TRUE(vm->stop(5000));
  ASSERT_TRUE(fakertos::waitNoTasks());
  vm.reset();
  fakelog::clearLines();

  ASSERT_TRUE(prepare("tracer"));
  EXPECT_FALSE(vm->setResume({}, 3));
  EXPECT_FALSE(vm->setResume(std::vector<uint8_t>(GameCore::SNAPSHOT_BYTES + 1, 1), 3));
  ASSERT_TRUE(vm->setResume(first, 41));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));
  EXPECT_TRUE(logHas("Resuming at ver 41"));
  EXPECT_TRUE(logHas("Round started at ver 41"));
  EXPECT_FALSE(vm->committed().pending()) << "a restored snapshot is already saved";
  ASSERT_TRUE(vm->drawFront(*renderer, viewport, replay));

  const uint32_t before = vm->frameGen();
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(waitFor([&] { return vm->frameGen() > before; }));
  ASSERT_TRUE(waitFor([&] { return vm->committed().pending(); }));
  std::vector<uint8_t> out(GameCore::SNAPSHOT_BYTES);
  ASSERT_TRUE(vm->committed().take(out, taken));
  EXPECT_EQ(taken.ver, 42u);
}

}  // namespace
