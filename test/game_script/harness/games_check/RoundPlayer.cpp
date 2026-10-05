#include "RoundPlayer.h"

#include <GameEvent.h>
#include <GameInput.h>
#include <LuaGame.h>
#include <MatchRounds.h>
#include <Memory.h>
#include <SeatShown.h>

#include <new>
#include <optional>
#include <string>
#include <utility>

#include "GamesCheckRig.h"
#include "HostBounds.h"

namespace games_check {

namespace {

using GameCore::MatchState;
using GameCore::Outcome;

// One round being played. Heap-allocated: it holds the input queue, the Session's neighbours, and the report.
class Player {
 public:
  // `limits` bound the played game's VM; `note` is appended to every failure (the margin gate says which cap it played
  // at); `resolve` is false for the margin play, which reuses the steps the first play resolved from `steps(state)`.
  Player(Round& round, const GameUnderCheck& game, const PlayOptions& options, const VmLimits& limits, std::string note,
         const bool resolve)
      : round(round), game(game), options(options), limits(limits), note(std::move(note)), resolve(resolve) {}
  ~Player() {
    if (lua) lua->close();
    if (rig && session) rig->arena().destroy(session);
  }

  RoundReport play() {
    if (prepare()) {
      run();
    }
    if (rig) report.log = rig->log().lines;
    report.restoreLog = std::move(restoreLog);
    if (session) {
      report.ver = session->ver();
      report.over = session->status().over;
      report.winners = session->status().winners;
    }
    return std::move(report);
  }

 private:
  // The messages every failure starts with.
  void fail(const int step, const std::string& message) {
    report.failures.push_back("round '" + round.name + "', " +
                              (step == 0 ? std::string("begin") : "step " + std::to_string(step)) + ": " + message +
                              note);
  }

  bool prepare() {
    const GameCore::Manifest& manifest = *game.facts.manifest;
    hidden = manifest.hidden && round.mode == GameCore::Mode::Pass;
    if (round.mode == GameCore::Mode::Pass) {
      const uint8_t seats = game.facts.seatsOf(GameCore::Mode::Pass);
      if (seats == 0) {
        fail(0, "no pass match of this game fits this host");
        return false;
      }
      roster = GameCore::Roster::pass(seats);
    } else {
      roster = GameCore::Roster::solo();
    }
    rig = GamesCheckRig::create(round.seed, game.canvas, limits);
    if (!rig) {
      fail(0, "out of memory");
      return false;
    }
    lua = makeUniqueNoThrow<GameScript::LuaGame>(rig->arena(), rig->frames(), *game.sources, rig->ports(),
                                                 rig->canvas(), *game.images);
    if (!lua) {
      fail(0, "out of memory");
      return false;
    }
    lua->setSettings(settingValues());
    // The Session sits in the arena's reserve, taken before the Lua state, as GameVM::run does.
    session = rig->arena().create<GameCore::Session>(roster, *lua);
    rounds = makeUniqueNoThrow<GameScript::MatchRounds>(lua->timer(), queue);
    if (!session || !rounds) {
      fail(0, "out of memory");
      return false;
    }
    const Outcome loaded = lua->load();
    if (loaded != Outcome::Ok) {
      fail(0, std::string("the game did not load: ") + lua->errorMessage());
      return false;
    }
    return true;
  }

  // ctx.settings: what the round chose, else each declared setting's default.
  GameCore::SettingValues settingValues() const {
    uint8_t chosen[GameCore::ManifestSettings::MAX_SETTINGS] = {};
    const GameCore::ManifestSettings* declared = game.facts.settings;
    if (!declared) return GameCore::SettingValues{};
    for (uint8_t i = 0; i < declared->count; ++i) chosen[i] = declared->settings[i].defaultIndex;
    for (const auto& [id, value] : round.settings) {
      const int at = declared->indexOf(id);
      if (at < 0) continue;
      const int index = declared->settings[at].indexOf(value);
      if (index >= 0) chosen[at] = static_cast<uint8_t>(index);
    }
    return declared->valuesAt(chosen);
  }

  bool measureSnapshot(const int step) {
    const size_t bytes = session->snapshot().size();
    if (bytes > report.maxSnapshotBytes) report.maxSnapshotBytes = bytes;
    if (bytes <= SNAPSHOT_CHECK_BYTES) return true;
    fail(step, "the snapshot is " + std::to_string(bytes) + " B, over the " + std::to_string(SNAPSHOT_CHECK_BYTES) +
                   " B a game keeps to");
    return false;
  }

  // Draws `seat` (0: the frame for everyone) and keeps its text commands. `sweep` marks a frame of
  // drawEveryLocalSeat, which the flow itself never draws.
  bool draw(const int step, const uint8_t seat, const bool sweep = false) {
    const Outcome outcome = rounds->draw(seat);
    if (outcome != Outcome::Ok) {
      fail(step, "draw for seat " + std::to_string(seat) + " failed: " + lua->errorMessage());
      return false;
    }
    keepFrame(seat, sweep);
    return true;
  }

  // The text commands of the frame published last, kept as `seat`'s.
  void keepFrame(const uint8_t seat, const bool sweep = false) {
    DrawnFrame frame;
    frame.seat = seat;
    frame.sweep = sweep;
    rig->frames().readFront([&frame](const GameScript::DisplayList& list) {
      auto reader = list.reader();
      GameScript::DrawCommand command;
      while (reader.next(command)) {
        if (command.op == GameScript::Op::Text && command.text) {
          frame.texts.emplace_back(command.text, command.textLength);
          frame.tops.push_back(command.y);
        }
      }
    });
    report.frames.push_back(std::move(frame));
  }

  // The seat the hidden flow shows while a turn is on (seatShown's Playing): the turn seat, seat 0 once over.
  uint8_t playingSeat() const { return GameCore::seatShown(MatchState::Playing, roster, session->status()); }

  bool drawEveryLocalSeat(const int step) {
    for (uint8_t seat = 1; seat <= roster.seats; ++seat) {
      if (roster.isLocal(seat) && !draw(step, seat, true)) return false;
    }
    // Seat 0, the frame for everyone, is a pass round's only: a solo game is never asked to draw it.
    if (session->status().over && roster.localSeatCount() > 1) return draw(step, 0, true);
    return true;
  }

  // Once the round is over the host draws its end-of-round dialog over the frame the device shows (seat 1 in solo, seat
  // 0 in pass), so a text that starts inside the dialog's band is hidden by it: the cross-story review found Sudoku's
  // difficulty name there, and no check could see it. Only the frames the flow drew count (a sweep frame is never on
  // the device), and only on the 788-tall canvases the bounds were measured on (HostBounds.h). The box's height is a
  // device font metric this host lacks, so the pin is on where a text starts; a game's own checks pin the whole box.
  bool endFrameClear(const int step, const size_t from) {
    if (!session->status().over || game.canvas.height != host::CANVAS_HEIGHT) return true;
    const uint8_t shown = hidden ? playingSeat() : rounds->shownSeat();
    for (size_t i = from; i < report.frames.size(); ++i) {
      const DrawnFrame& frame = report.frames[i];
      if (frame.sweep || frame.seat != shown) continue;
      for (size_t at = 0; at < frame.texts.size(); ++at) {
        if (frame.tops[at] < host::DIALOG_TOP || frame.tops[at] >= host::DIALOG_BOTTOM) continue;
        fail(step, "the round is over, and seat " + std::to_string(shown) + "'s frame starts the text '" +
                       frame.texts[at] + "' at y " + std::to_string(frame.tops[at]) +
                       ", inside the host's end-of-round dialog (y " + std::to_string(host::DIALOG_TOP) + " up to " +
                       std::to_string(host::DIALOG_BOTTOM) +
                       " on a 788-tall canvas, HostBounds.h), which draws over it: the player never sees it");
        return false;
      }
    }
    return true;
  }

  // Where a restored game stopped: the phase and what it said.
  struct ProbeFault {
    std::string phase;
    std::string message;
  };

  // A new LuaGame, Session and MatchRounds in a rig of their own, built as GameVM::run builds them for Continue, given
  // this round's current snapshot and ver, started (Session::start takes the restored state: no `setup`), and drawn
  // for every local seat (and seat 0 once a pass round is over, as drawEveryLocalSeat does). Empty when all of it ran.
  std::optional<ProbeFault> restoreAndDraw() {
    const auto stopped = [](std::string phase, std::string message) {
      return std::optional<ProbeFault>(ProbeFault{std::move(phase), std::move(message)});
    };
    auto probeRig = GamesCheckRig::create(round.seed, game.canvas, limits);
    if (!probeRig) return stopped("setup", "out of memory");
    auto probeLua = makeUniqueNoThrow<GameScript::LuaGame>(probeRig->arena(), probeRig->frames(), *game.sources,
                                                           probeRig->ports(), probeRig->canvas(), *game.images);
    if (!probeLua) return stopped("setup", "out of memory");
    probeLua->setSettings(settingValues());
    GameCore::Session* probeSession = probeRig->arena().create<GameCore::Session>(roster, *probeLua);
    GameScript::InputQueue probeQueue;
    auto probeRounds = makeUniqueNoThrow<GameScript::MatchRounds>(probeLua->timer(), probeQueue);
    if (!probeSession || !probeRounds) {
      probeLua->close();
      if (probeSession) probeRig->arena().destroy(probeSession);
      return stopped("setup", "out of memory");
    }
    std::optional<ProbeFault> fault;
    if (probeLua->load() != Outcome::Ok) {
      fault = stopped("load", probeLua->errorMessage());
    } else if (!probeSession->restore(session->snapshot(), session->ver())) {
      fault = stopped("restore", "the Session refused the snapshot");
    } else if (probeRounds->begin(*probeSession) != Outcome::Ok) {
      fault = stopped("start", probeLua->errorMessage());
    } else {
      for (uint8_t seat = 1; !fault && seat <= roster.seats; ++seat) {
        if (roster.isLocal(seat) && probeRounds->draw(seat) != Outcome::Ok) {
          fault = stopped("draw for seat " + std::to_string(seat), probeLua->errorMessage());
        }
      }
      if (!fault && probeSession->status().over && roster.localSeatCount() > 1 && probeRounds->draw(0) != Outcome::Ok) {
        fault = stopped("draw for seat 0", probeLua->errorMessage());
      }
    }
    restoreLog = probeRig->log().lines;
    probeLua->close();
    probeRig->arena().destroy(probeSession);
    return fault;
  }

  // The Continue seam (PlayOptions::restoreProbe): a failure of restoreAndDraw fails the round at `step`, naming the
  // phase. The probe's game is dropped either way; the live one plays on.
  bool restoredGameDraws(const int step, const char* when) {
    const std::optional<ProbeFault> fault = restoreAndDraw();
    if (!fault) return true;
    fail(step, std::string("restoring the snapshot into a new game ") + when +
                   " (as Continue does: a new VM, `restore`, `start`, every seat's `ui` empty) failed at " +
                   fault->phase + ": " + fault->message);
    return false;
  }

  bool begin() {
    const Outcome began = rounds->begin(*session);
    if (began != Outcome::Ok) {
      fail(0, std::string("setup or status failed: ") + lua->errorMessage());
      return false;
    }
    if (!measureSnapshot(0)) return false;
    if (round.stepsFromFunction && resolve) {
      std::string error;
      if (!resolveSteps(round, session->snapshot(), game.facts, error)) {
        fail(0, error);
        return false;
      }
    }
    // The hidden flow shows the hand-off screen first (no frame), then takes the turn seat; the others show their seat.
    const size_t framesBefore = report.frames.size();
    const uint8_t first = hidden ? playingSeat() : rounds->shownSeat();
    if (first != GameCore::NO_SEAT && !draw(0, first)) return false;
    if (!endFrameClear(0, framesBefore)) return false;
    return !options.drawEveryLocalSeat || drawEveryLocalSeat(0);
  }

  // The seat the device shows, so the one whose input it reads.
  uint8_t shownSeat() const { return hidden ? playingSeat() : rounds->shownSeat(); }

  bool step(const int number, const Step& step) {
    // Over, the device reads no input for any seat (seat 0 is shown, in pass; in solo the seat stays 1 but the
    // end-of-round menu is up), so no step follows the end of the round, whatever seat or `move` it names.
    if (session->status().over) {
      fail(number, "the round is already over (winners " + winnersText(session->status().winners) +
                       "); a step cannot follow its end");
      return false;
    }
    const uint8_t shown = shownSeat();
    if (shown != step.seat) {
      fail(number, "the step is for seat " + std::to_string(step.seat) + ", but the device shows seat " +
                       std::to_string(shown) + " and reads input for that seat only");
      return false;
    }
    // The device drops a tap that starts off the canvas (GameTouch::toEvent), so a round that taps there tests a path
    // no player can take: x or y negative, or at or over the canvas's size (x 466 to 473 exists only on the Sticky's).
    if (step.x < 0 || step.y < 0 || step.x >= game.canvas.width || step.y >= game.canvas.height) {
      fail(number, "the tap (" + std::to_string(step.x) + ", " + std::to_string(step.y) + ") is off the " +
                       std::to_string(game.canvas.width) + " x " + std::to_string(game.canvas.height) +
                       " canvas under check, and the device drops a tap that starts there");
      return false;
    }
    const uint32_t verBefore = session->ver();
    rig->clock().advance(step.waitMs);
    GameCore::GameEvent event;
    event.kind = GameCore::EventKind::Tap;
    event.x = static_cast<int16_t>(step.x);
    event.y = static_cast<int16_t>(step.y);
    const size_t framesBefore = report.frames.size();
    if (hidden) {
      if (!stepHidden(number, event, shown)) return false;
    } else {
      const Outcome outcome = rounds->step(event);
      if (outcome != Outcome::Ok) {
        fail(number, std::string("input, apply, status, or draw failed: ") + lua->errorMessage());
        return false;
      }
      // MatchRounds::step drew the seat shown after the move; keep that frame.
      keepFrame(rounds->shownSeat());
    }
    const uint32_t verAfter = session->ver();
    const uint32_t expected = step.moves ? verBefore + 1 : verBefore;
    if (verAfter != expected) {
      fail(number, step.moves ? "the tap did not move (ver is " + std::to_string(verAfter) + ", expected " +
                                    std::to_string(expected) + "); a tap that changes nothing says `move = false`"
                              : "the tap moved (ver is " + std::to_string(verAfter) + ", expected " +
                                    std::to_string(expected) + "), but the step says `move = false`");
      return false;
    }
    if (!measureSnapshot(number)) return false;
    if (!endFrameClear(number, framesBefore)) return false;
    if (options.drawEveryLocalSeat && !drawEveryLocalSeat(number)) return false;
    return !step.hasShows || shows(number, step, framesBefore);
  }

  // One tap in a hidden pass round (GameVM::stepHandOff).
  bool stepHidden(const int number, const GameCore::GameEvent& event, const uint8_t seat) {
    const Outcome outcome = rounds->play(event, seat);
    if (outcome != Outcome::Ok) {
      fail(number, std::string("input, apply, or status failed: ") + lua->errorMessage());
      return false;
    }
    const GameCore::Status& status = session->status();
    // A move that ends the round is RoundOver's (seat 0 is shown), never a turn change.
    const bool passed = !status.over && status.turn != seat;
    if (!passed) {
      const uint8_t next = playingSeat();
      return next == GameCore::NO_SEAT || draw(number, next);
    }
    // The mover's own frame (Result), then the hand-off screen (no seat), then the seat that takes the device.
    if (!draw(number, GameCore::seatShown(MatchState::Result, roster, status, seat))) return false;
    const uint8_t next = playingSeat();
    return next == GameCore::NO_SEAT || draw(number, next);
  }

  // `step.shows` is in a text command of `step.seat`'s first frame drawn after the step. In a hidden round only a frame
  // the flow itself drew counts: a sweep frame is a seat the device would not show at that moment, so it could meet
  // `shows` with text the player never sees (open and solo rounds read MatchRounds::step's frame first, which is the
  // flow's own, so the rule changes nothing there). A hidden step that ends the round shows seat 0 and no other seat
  // (a move that ends the round is never a turn change), so it is that frame, the end screen the player sees, which
  // `shows` reads.
  bool shows(const int number, const Step& step, const size_t framesBefore) {
    const int wanted = hidden && session->status().over ? 0 : step.seat;
    for (size_t i = framesBefore; i < report.frames.size(); ++i) {
      const DrawnFrame& frame = report.frames[i];
      if (frame.seat != wanted || (hidden && frame.sweep)) continue;
      for (const std::string& text : frame.texts) {
        if (text.find(step.shows) != std::string::npos) return true;
      }
      std::string held;
      for (const std::string& text : frame.texts) held += (held.empty() ? "" : " | ") + ("'" + text + "'");
      fail(number, "seat " + std::to_string(wanted) + "'s next frame does not show '" + step.shows + "'; it shows " +
                       (held.empty() ? std::string("no text") : held));
      return false;
    }
    fail(number, "no frame was drawn for seat " + std::to_string(wanted) + " after the step, so '" + step.shows +
                     "' cannot be shown");
    return false;
  }

  void run() {
    if (!begin()) return;
    for (size_t i = 0; i < round.steps.size(); ++i) {
      const int number = static_cast<int>(i + 1);
      if (options.restoreProbe && !restoredGameDraws(number, "before this step")) return;
      if (!step(number, round.steps[i])) return;
    }
    if (options.restoreProbe && !restoredGameDraws(static_cast<int>(round.steps.size()),
                                                   round.steps.empty() ? "after begin" : "after the last step")) {
      return;
    }
    finish();
  }

  void finish() {
    const GameCore::Status& status = session->status();
    const int last = static_cast<int>(round.steps.size());
    if (round.unfinished) {
      if (status.over) {
        fail(last,
             "the round is over (winners " + winnersText(status.winners) + "), but the round file says unfinished");
      }
      return;
    }
    if (!status.over) {
      fail(last, "the round is not over (seat " + std::to_string(status.turn) +
                     " to move), but the round file expects winners " + winnersText(round.winners));
    } else if (status.winners != round.winners) {
      fail(last, "expected winners " + winnersText(round.winners) + ", got " + winnersText(status.winners));
    }
  }

  Round& round;
  const GameUnderCheck& game;
  const PlayOptions options;
  const VmLimits limits;
  const std::string note;
  const bool resolve;
  RoundReport report;
  std::vector<std::string> restoreLog;  // the last restored game's log (restoreAndDraw)
  GameCore::Roster roster = GameCore::Roster::solo();
  bool hidden = false;
  std::unique_ptr<GamesCheckRig> rig;
  std::unique_ptr<GameScript::LuaGame> lua;
  GameScript::InputQueue queue;
  std::unique_ptr<GameScript::MatchRounds> rounds;
  GameCore::Session* session = nullptr;
};

}  // namespace

RoundReport playRound(Round& round, const GameUnderCheck& game, const PlayOptions& options) {
  auto player = makeUniqueNoThrow<Player>(round, game, options, VmLimits::device(), std::string(), true);
  if (!player) {
    RoundReport report;
    report.failures.push_back("round '" + round.name + "': out of memory");
    return report;
  }
  RoundReport report = player->play();
  player.reset();
  if (!report.ok() || !options.heapMargin) return report;
  // The margin gate: the round played clean at the device's cap; play it again with the cap HEAP_MARGIN_BYTES lower.
  // The only exact statement of "this much room under the cap" (HEAP_MARGIN_BYTES). Not the restore probe again, and
  // `steps(state)` is not resolved twice: the first play left the round its steps.
  PlayOptions again = options;
  again.restoreProbe = false;
  const VmLimits lowered = VmLimits::deviceLess(HEAP_MARGIN_BYTES);
  const std::string note = " [heap margin: the round plays at the device's " +
                           std::to_string(GameScript::LUA_HEAP_BYTES) + " B Lua heap cap, but at " +
                           std::to_string(lowered.luaHeapBytes) + " B, " + std::to_string(HEAP_MARGIN_BYTES) +
                           " B lower, it fails: the game has less than the margin of room under the cap]";
  auto second = makeUniqueNoThrow<Player>(round, game, again, lowered, note, false);
  if (!second) {
    report.failures.push_back("round '" + round.name + "': out of memory (heap margin play)");
    return report;
  }
  RoundReport lowerPlay = second->play();
  for (std::string& failure : lowerPlay.failures) report.failures.push_back(std::move(failure));
  return report;
}

}  // namespace games_check
