#include "RoundPlayer.h"

#include <GameEvent.h>
#include <GameInput.h>
#include <LuaGame.h>
#include <MatchRounds.h>
#include <Memory.h>
#include <SeatShown.h>

#include <new>

#include "GamesCheckRig.h"

namespace games_check {

namespace {

using GameCore::MatchState;
using GameCore::Outcome;

// One round being played. Heap-allocated: it holds the input queue, the Session's neighbours, and the report.
class Player {
 public:
  Player(Round& round, const GameUnderCheck& game, const PlayOptions& options)
      : round(round), game(game), options(options) {}
  ~Player() {
    if (lua) lua->close();
    if (rig && session) rig->arena().destroy(session);
  }

  RoundReport play() {
    if (prepare()) {
      run();
    }
    if (rig) report.log = rig->log().lines;
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
                              (step == 0 ? std::string("begin") : "step " + std::to_string(step)) + ": " + message);
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
    rig = GamesCheckRig::create(round.seed, game.canvas);
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

  // Draws `seat` (0: the frame for everyone) and keeps its text commands.
  bool draw(const int step, const uint8_t seat) {
    const Outcome outcome = rounds->draw(seat);
    if (outcome != Outcome::Ok) {
      fail(step, "draw for seat " + std::to_string(seat) + " failed: " + lua->errorMessage());
      return false;
    }
    keepFrame(seat);
    return true;
  }

  // The text commands of the frame published last, kept as `seat`'s.
  void keepFrame(const uint8_t seat) {
    DrawnFrame frame;
    frame.seat = seat;
    rig->frames().readFront([&frame](const GameScript::DisplayList& list) {
      auto reader = list.reader();
      GameScript::DrawCommand command;
      while (reader.next(command)) {
        if (command.op == GameScript::Op::Text && command.text)
          frame.texts.emplace_back(command.text, command.textLength);
      }
    });
    report.frames.push_back(std::move(frame));
  }

  // The seat the hidden flow shows while a turn is on (seatShown's Playing): the turn seat, seat 0 once over.
  uint8_t playingSeat() const { return GameCore::seatShown(MatchState::Playing, roster, session->status()); }

  bool drawEveryLocalSeat(const int step) {
    for (uint8_t seat = 1; seat <= roster.seats; ++seat) {
      if (roster.isLocal(seat) && !draw(step, seat)) return false;
    }
    // Seat 0, the frame for everyone, is a pass round's only: a solo game is never asked to draw it.
    if (session->status().over && roster.localSeatCount() > 1) return draw(step, 0);
    return true;
  }

  bool begin() {
    const Outcome began = rounds->begin(*session);
    if (began != Outcome::Ok) {
      fail(0, std::string("setup or status failed: ") + lua->errorMessage());
      return false;
    }
    if (!measureSnapshot(0)) return false;
    if (round.stepsFromFunction) {
      std::string error;
      if (!resolveSteps(round, session->snapshot(), game.facts, error)) {
        fail(0, error);
        return false;
      }
    }
    // The hidden flow shows the hand-off screen first (no frame), then takes the turn seat; the others show their seat.
    if (hidden) {
      const uint8_t seat = playingSeat();
      if (seat != GameCore::NO_SEAT && !draw(0, seat)) return false;
    } else {
      const uint8_t seat = rounds->shownSeat();
      if (seat != GameCore::NO_SEAT && !draw(0, seat)) return false;
    }
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

  // `step.shows` is in a text command of `step.seat`'s first frame drawn after the step.
  bool shows(const int number, const Step& step, const size_t framesBefore) {
    for (size_t i = framesBefore; i < report.frames.size(); ++i) {
      const DrawnFrame& frame = report.frames[i];
      if (frame.seat != step.seat) continue;
      for (const std::string& text : frame.texts) {
        if (text.find(step.shows) != std::string::npos) return true;
      }
      std::string held;
      for (const std::string& text : frame.texts) held += (held.empty() ? "" : " | ") + ("'" + text + "'");
      fail(number, "seat " + std::to_string(step.seat) + "'s next frame does not show '" + step.shows + "'; it shows " +
                       (held.empty() ? std::string("no text") : held));
      return false;
    }
    fail(number, "no frame was drawn for seat " + std::to_string(step.seat) + " after the step, so '" + step.shows +
                     "' cannot be shown");
    return false;
  }

  void run() {
    if (!begin()) return;
    for (size_t i = 0; i < round.steps.size(); ++i) {
      if (!step(static_cast<int>(i + 1), round.steps[i])) return;
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
  RoundReport report;
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
  auto player = makeUniqueNoThrow<Player>(round, game, options);
  if (!player) {
    RoundReport report;
    report.failures.push_back("round '" + round.name + "': out of memory");
    return report;
  }
  return player->play();
}

}  // namespace games_check
