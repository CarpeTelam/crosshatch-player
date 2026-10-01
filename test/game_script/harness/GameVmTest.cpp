#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ArenaSize.h"
#include "FrameReplay.h"
#include "GameAssets.h"
#include "GameViewport.h"
#include "MatchStore.h"
#include "MatchSupport.h"
#include "Session.h"

// The real GameVM, its task, and its clock on host threads (screen_stubs/FakeRtos.h), running the
// fixture games from the fake card: the task's lifecycle, input and the timer poll, the stale timer
// event, the watchdog's per-call timing, abandon, the front frame and the identical-frame skip, and
// the failure the VM reports. Entry 4 of epic-install-and-launcher; the match on top of it is
// GameMatchTest. Each test names the deferred-work item it pins.

namespace {

using match::installFixture;
using match::installGame;
using match::waitFor;

GameCore::GameEvent tapAt(const int x, const int y) {
  GameCore::GameEvent event;
  event.kind = GameCore::EventKind::Tap;
  event.x = static_cast<int16_t>(x);
  event.y = static_cast<int16_t>(y);
  return event;
}

class GameVmTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    fakearena::reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;
    replay.loadFonts(*renderer);
    viewport = GameViewport::forRenderer(*renderer);
  }

  void TearDown() override {
    fakertos::release();  // a test that failed while holding the task must not leave it held
    if (vm && !vm->stop(5000)) {
      ADD_FAILURE() << "the VM would not stop; leaking it rather than freeing memory its task may use";
      (void)vm.release();
      expectCleanPsram = false;
    }
    vm.reset();  // the task's writes stop before the fakes are checked
    store.reset();
    fakearena::reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;
    ScreenTest::TearDown();
  }

  // The store, the assets, and the VM of game `id` (its folder is on the card), not started.
  bool prepare(const std::string& id) {
    store = std::make_unique<MatchStore>();
    if (!store->allocate(id.c_str(), static_cast<uint32_t>(fakertos::S().nowMs.load()))) return false;
    GameAssets assets;
    if (assets.load(id.c_str(), store->saves(), store->slot()) != GameAssets::LoadResult::Ok) return false;
    vm = GameVM::create(std::move(assets), viewport, replay, id.c_str(), store->slot(), GameCore::Roster::solo());
    return vm != nullptr;
  }

  bool startAndWaitFirstFrame() {
    return vm->start() && waitFor([&] { return vm->frameGen() >= 1; });
  }

  // Posts a tap and waits until the VM has published the frame after it.
  bool tapAndWaitFrame(const int x, const int y) {
    const uint32_t before = vm->frameGen();
    vm->postInput(tapAt(x, y));
    return waitFor([&] { return vm->frameGen() > before; });
  }

  GameVM::HostFailureTexts texts() const {
    GameVM::HostFailureTexts value;
    value.outOfMemory = "the OOM text";
    value.notLoaded = "the not-loaded text";
    return value;
  }

  FrameReplay replay;
  GameViewport viewport;
  std::unique_ptr<MatchStore> store;  // declared before vm: outlives the task that posts to its slot
  std::unique_ptr<GameVM> vm;
};

// ---- the task's lifecycle (the tracer's deferred item: create, start, input then draw, quit and join) ----

TEST_F(GameVmTest, TheTaskRunsSetupAndTheFirstDrawThenStopsWhenCancelled) {
  installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  // Before start(): no frame, and nothing to draw.
  EXPECT_EQ(vm->frameGen(), 0u);
  EXPECT_FALSE(vm->drawFront(*renderer, viewport, replay));
  EXPECT_TRUE(renderer->calls.empty());
  ASSERT_TRUE(startAndWaitFirstFrame());
  EXPECT_EQ(vm->roundsStarted(), 1u);
  EXPECT_EQ(vm->roundsEnded(), 0u);
  EXPECT_FALSE(vm->finished());
  EXPECT_EQ(vm->failure(), GameVM::Failure::None);  // the guard: a running VM has no failure to read
  EXPECT_TRUE(vm->drawFront(*renderer, viewport, replay));
  EXPECT_GT(renderer->count(GfxRenderer::Kind::FillRect) + renderer->count(GfxRenderer::Kind::DrawText), 0u);
  EXPECT_TRUE(vm->stop(5000));
  EXPECT_TRUE(vm->finished());
  EXPECT_FALSE(vm->failed());
  EXPECT_TRUE(logHas("VM stopped"));
}

TEST_F(GameVmTest, JoinIsTrueForAVmThatNeverStartedAndFalseWhileTheTaskRuns) {
  installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  EXPECT_TRUE(vm->join(0));  // no task: nothing to wait for
  ASSERT_TRUE(startAndWaitFirstFrame());
  EXPECT_FALSE(vm->join(0));
  EXPECT_TRUE(vm->stop(5000));
}

TEST_F(GameVmTest, AnInputEventReachesLuaAndItsFrameIsTheNextOne) {
  installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(tapAndWaitFrame(100, 200));  // below the banner: a move, so the square is drawn there
  renderer->forgetAll();
  ASSERT_TRUE(vm->drawFront(*renderer, viewport, replay));
  // The game draws a 40 x 40 square centred on the tap, in canvas pixels; the canvas is at (3, 6).
  EXPECT_EQ(renderer->pixel(3 + 100, 6 + 200), GfxRenderer::PixelBlack);
  EXPECT_EQ(renderer->pixel(3 + 100 - 19, 6 + 200 - 19), GfxRenderer::PixelBlack);
  EXPECT_EQ(renderer->pixel(3 + 100 + 30, 6 + 200), GfxRenderer::PixelWhite);
  EXPECT_EQ(vm->frameGen(), 2u);
}

TEST_F(GameVmTest, NothingIsNotifiedToATaskThatHasEnded) {
  installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(vm->stop(5000));
  ASSERT_TRUE(fakertos::waitNoTasks());
  // On the device the task has deleted itself: a notify to it would touch freed memory.
  vm->postInput(tapAt(1, 1));
  vm->cancel();
  vm->playAgain();
  vm->pollTimer();
  EXPECT_EQ(fakertos::S().deadNotifies.load(), 0);
}

TEST_F(GameVmTest, ATaskThatCannotBeCreatedLeavesNoTaskToNotifyOrJoin) {
  installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  fakertos::S().failNextCreate = true;
  EXPECT_FALSE(vm->start());
  EXPECT_TRUE(logHas("Cannot create the GameVM task"));
  // taskAlive was cleared again, so nothing is notified to the task that never was.
  vm->postInput(tapAt(1, 1));
  vm->cancel();
  EXPECT_EQ(fakertos::S().deadNotifies.load(), 0);
  EXPECT_TRUE(vm->join(0));
  EXPECT_EQ(fakertos::S().tasksCreated.load(), 0);
}

// ---- pollTimer and the stale timer (the ch.timer deferred item) ----

TEST_F(GameVmTest, PollTimerPostsTheTimerEventOnceItIsDue) {
  installFixture("timer");
  ASSERT_TRUE(prepare("timer"));  // setup arms a 3000 ms timer at 1000 ms
  ASSERT_TRUE(startAndWaitFirstFrame());
  fakertos::advance(2999);
  vm->pollTimer();
  // A tap's event is queued behind any the poll wrongly posted, so its frame is preceded by that
  // event's log line. The tap also restarts the timer, 3000 ms from now.
  ASSERT_TRUE(tapAndWaitFrame(100, 200));
  EXPECT_EQ(fakelog::countLines("tick at"), 0u) << "a timer 1 ms short of due delivered an event";
  // The tap re-armed the timer at 3999 ms for 6999 ms: exactly due now.
  fakertos::advance(3000);
  vm->pollTimer();
  ASSERT_TRUE(waitFor([&] { return vm->frameGen() >= 3; }));
  EXPECT_EQ(fakelog::countLines("tick at"), 1u);
  // The event was taken: a second poll finds nothing due (the game re-armed 3000 ms ahead), so
  // the next tap's event is the only one before its frame.
  vm->pollTimer();
  ASSERT_TRUE(tapAndWaitFrame(100, 200));
  EXPECT_EQ(fakelog::countLines("tick at"), 1u);
}

TEST_F(GameVmTest, ATimerEventPolledBeforeTheGameRearmsItIsDropped) {
  installFixture("timer");
  ASSERT_TRUE(prepare("timer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  // Hold the VM inside a tap's input, before the game re-arms its timer (ch.timer.after reads the
  // clock first). The timer set up at 1000 ms falls due meanwhile, and the loop task polls it.
  fakertos::arm();
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::advance(3000);
  vm->pollTimer();  // queues the Timer event of the timer as it stands: due
  fakertos::release();
  // The tap re-arms the timer (3000 ms ahead of now), so the queued event is stale. The frame
  // after a second tap comes after that event in the queue.
  ASSERT_TRUE(waitFor([&] { return vm->frameGen() >= 2; }));
  ASSERT_TRUE(tapAndWaitFrame(100, 200));
  EXPECT_EQ(vm->frameGen(), 3u);
  EXPECT_EQ(fakelog::countLines("tick at"), 0u) << "the stale timer event reached the game";
}

// ---- the watchdog's timing of one call (the GameVM::run deferred item) ----

TEST_F(GameVmTest, RunningForTimesEachCallFromTheFirstPollThatSawIt) {
  installFixture("timer");
  ASSERT_TRUE(prepare("timer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor([&] { return !vm->busy(); }));
  EXPECT_EQ(vm->runningForMs(5000), 0u);  // idle
  EXPECT_EQ(vm->runningForMs(9000), 0u);  // and idle still, however long since the last call
  // A call held inside Lua: timed from the poll that first saw it, not from the clock's zero.
  fakertos::arm();
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_TRUE(vm->busy());
  EXPECT_EQ(vm->runningForMs(10000), 0u);
  EXPECT_EQ(vm->runningForMs(12500), 2500u);
  fakertos::release();
  ASSERT_TRUE(waitFor([&] { return vm->frameGen() >= 2 && !vm->busy(); }));
  EXPECT_EQ(vm->runningForMs(13000), 0u);
  // The next call is timed on its own: it starts again at zero.
  fakertos::arm();
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  EXPECT_EQ(vm->runningForMs(13500), 0u);
  EXPECT_EQ(vm->runningForMs(13900), 400u);
  fakertos::release();
  ASSERT_TRUE(waitFor([&] { return vm->frameGen() >= 3; }));
}

// ---- abandon (the ch.timer deferred item, and the sandbox item's device band, as far as a host reaches) ----

TEST_F(GameVmTest, AbandonFreesAVmStuckOutsideALockedBinding) {
  installFixture("timer");
  ASSERT_TRUE(prepare("timer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  const size_t blocksBefore = fakepsram::liveBlocks;  // the store's, the assets', the frames', the arena
  ASSERT_GE(blocksBefore, 4u);
  fakertos::arm();  // the tap's ch.timer.after reads the clock: held there, inside Lua
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  vm->cancel();
  EXPECT_FALSE(vm->join(50));  // stuck: the cancel flag is read at the next hook event
  EXPECT_TRUE(GameVM::abandon(std::move(vm)));
  // The arena, the frames, and the sources are freed; the store slot is the match's and stays.
  EXPECT_EQ(fakepsram::liveBlocks, 1u);
  EXPECT_EQ(fakertos::S().liveTasks.load(), 0);  // the task was deleted
  EXPECT_TRUE(logHas("Abandoned the stuck VM"));
  fakertos::release();  // a deleted task does not run again: the gate is no longer what holds it
}

TEST_F(GameVmTest, AbandonLeavesAVmStuckInsideALockedBindingAndSaysSo) {
  installGame("logger", match::LOGGING_GAME);
  ASSERT_TRUE(prepare("logger"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  expectCleanPsram = false;          // the leak is the point: the VM keeps its arena, and the match keeps the slot
  fakertos::arm(fakertos::At::Log);  // ch.log writes its line inside a locked binding: held there
  vm->postInput(tapAt(100, 200));
  ASSERT_TRUE(fakertos::waitParked());
  vm->cancel();
  EXPECT_FALSE(vm->join(50));
  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(GameVM::abandon(std::move(vm)));
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_GE(waited.count(), static_cast<long>(GameVM::ABANDON_WAIT_MS) - 50) << "gave up before the wait was over";
  EXPECT_TRUE(logHas("VM stuck and not safely deletable"));
  EXPECT_GE(fakepsram::liveBlocks, 4u);  // all of it leaked
  // The leaked task finishes once its call returns: the cancel flag ends it at the next hook.
  fakertos::release();
  EXPECT_TRUE(fakertos::waitNoTasks());
}

TEST_F(GameVmTest, AbandonDeletesAVmSpinningInLuaThatNeverReturns) {
  installGame("spin", match::SPIN_GAME);
  ASSERT_TRUE(prepare("spin"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  vm->postInput(tapAt(100, 200));  // the call spins: 2M Lua instructions of budget, each pass reading the clock
  ASSERT_TRUE(waitFor([&] { return vm->busy(); }));
  // No cancel: the suspend has to find a task that is running, and stop it at its next clock read.
  EXPECT_TRUE(GameVM::abandon(std::move(vm)));
  EXPECT_TRUE(logHas("Abandoned the stuck VM")) << "the task ended on its own instead";
  EXPECT_EQ(fakepsram::liveBlocks, 1u);
}

TEST_F(GameVmTest, AbandonLeavesAnIdleVmAloneForItIsNotInsideLua) {
  installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(waitFor([&] { return !vm->busy(); }));
  GameVM* raw = vm.get();
  // A VM waiting for input is suspended cleanly, but a task outside Lua is not deleted: it may be
  // between a lock and its release. Abandon gives up after its wait.
  EXPECT_FALSE(GameVM::abandon(std::move(vm)));
  EXPECT_TRUE(logHas("VM stuck and not safely deletable"));
  // The test knows it is only idle: it stops it and frees it.
  raw->cancel();
  ASSERT_TRUE(raw->join(5000));
  ASSERT_TRUE(fakertos::waitNoTasks());
  delete raw;
}

TEST_F(GameVmTest, AbandonOfAVmThatEndedAfterAllDeletesItAndFreesEverything) {
  installFixture("tracer");
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  ASSERT_TRUE(vm->stop(5000));
  ASSERT_TRUE(fakertos::waitNoTasks());
  EXPECT_TRUE(GameVM::abandon(std::move(vm)));
  EXPECT_EQ(fakepsram::liveBlocks, 1u);  // only the store's; the VM's blocks went with the object
  EXPECT_TRUE(GameVM::abandon(nullptr));
}

// ---- the front frame (the framereplay deferred item: the identical-frame skip) ----

TEST_F(GameVmTest, DrawFrontSkipsAFrameIdenticalToTheOneOnScreenUntilForced) {
  installGame("still", match::STILL_GAME);
  ASSERT_TRUE(prepare("still"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  EXPECT_TRUE(vm->drawFront(*renderer, viewport, replay));
  EXPECT_EQ(replay.refreshMode(), HalDisplay::FULL_REFRESH);  // the first frame is a full refresh
  ASSERT_TRUE(tapAndWaitFrame(100, 200));                     // every input redraws: the same frame again
  renderer->forgetAll();
  EXPECT_FALSE(vm->drawFront(*renderer, viewport, replay));
  EXPECT_TRUE(renderer->calls.empty()) << "an identical frame was drawn again";
  replay.forceFull();
  EXPECT_TRUE(vm->drawFront(*renderer, viewport, replay));
  EXPECT_FALSE(renderer->calls.empty());
}

// ---- the failure the VM reports (## 3.10: GameVM::failure's call site) ----

TEST_F(GameVmTest, AScriptErrorIsAScriptFailureThatStartedTheGame) {
  installGame("boom", "local game = {}\nfunction game.setup() error('boom') end\nreturn game\n");
  ASSERT_TRUE(prepare("boom"));
  ASSERT_TRUE(vm->start());
  ASSERT_TRUE(waitFor([&] { return vm->finished(); }));
  EXPECT_TRUE(vm->failed());
  EXPECT_EQ(vm->failure(), GameVM::Failure::Script);
  EXPECT_FALSE(vm->failedToStart());  // the script's own error says it stopped, not that it could not start
  EXPECT_NE(std::string(vm->errorMessage()).find("boom"), std::string::npos);
  EXPECT_EQ(std::string(vm->failureDetail(texts())), vm->errorMessage());
}

TEST_F(GameVmTest, ASessionThatDoesNotFitIsNoSessionAndCouldNotStart) {
  installFixture("tracer");
  fakearena::reserveBytes = 0;  // nothing left for the Session, which the VM allocates first
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(vm->start());
  ASSERT_TRUE(waitFor([&] { return vm->finished(); }));
  EXPECT_TRUE(vm->failed());
  EXPECT_EQ(vm->failure(), GameVM::Failure::NoSession);
  EXPECT_TRUE(vm->failedToStart());
  EXPECT_STREQ(vm->errorMessage(), "not enough memory");
  // The host's own words, never the English message.
  EXPECT_STREQ(vm->failureDetail(texts()), "the OOM text");
}

TEST_F(GameVmTest, ScratchThatDoesNotFitIsAnOutOfMemoryHostFailure) {
  installFixture("tracer");
  fakearena::reserveBytes = sizeof(GameCore::Session) + 64;  // the Session fits; the codec scratch does not
  ASSERT_TRUE(prepare("tracer"));
  ASSERT_TRUE(vm->start());
  ASSERT_TRUE(waitFor([&] { return vm->finished(); }));
  EXPECT_EQ(vm->failure(), GameVM::Failure::OutOfMemory);
  EXPECT_TRUE(vm->failedToStart());
  EXPECT_STREQ(vm->failureDetail(texts()), "the OOM text");
}

// ---- a canvas with the viewport's size and the replay's text metrics (the ch.gfx deferred item) ----

TEST_F(GameVmTest, TheGameSeesTheViewportAsItsScreenAndMeasuresTextWithTheFonts) {
  installGame("probe", R"(
local game = {}
function game.setup(ctx)
  ch.log("screen", ch.screen.w, ch.screen.h, "abc small", ch.text_width("abc", "small"), "abc large",
         ch.text_width("abc", "large"))
  return {}
end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev) end
return game
)");
  ASSERT_TRUE(prepare("probe"));
  ASSERT_TRUE(startAndWaitFirstFrame());
  // 474 x 788 of the 480 x 800 screen (width first), and 3 glyphs of the double's advances.
  EXPECT_TRUE(logHas("screen\t474\t788\tabc small\t" + std::to_string(3 * match::SMALL_ADVANCE) + "\tabc large\t" +
                     std::to_string(3 * match::LARGE_ADVANCE)));
}

// ---- the fake clock moves with the waits the firmware makes (screen_stubs/FakeRtos.h) ----

TEST_F(GameVmTest, ADelayMovesTheFakeClockByWhatItWaited) {
  const uint64_t start = fakertos::S().nowMs.load();
  vTaskDelay(pdMS_TO_TICKS(7));
  EXPECT_EQ(fakertos::S().nowMs.load(), start + 7);
  EXPECT_EQ(xTaskGetTickCount(), static_cast<TickType_t>(start + 7));
  delay(3);
  EXPECT_EQ(millis(), static_cast<unsigned long>(start + 10));
}

}  // namespace
