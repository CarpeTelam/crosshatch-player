#pragma once

// Plays one Round headlessly over a game's own LuaGame, a Session, and the production round loop (MatchRounds): a fresh
// LuaGame per round, SeededRandom(seed) as its HostPorts::random, a clock the steps advance, the 474 x 788 device
// canvas, and the round's settings (or the manifest's defaults) as ctx.settings. Solo and open pass rounds use
// MatchRounds' start and step, as GameVM does. A hidden pass round follows GameVM::stepHandOff (spine AD-21, D2 of the
// games check's plan):
//
//   - nothing is drawn after the round begins (the hand-off screen), then the turn seat is shown (seatShown(Playing));
//   - a step is delivered to the shown seat only: a step naming another seat is a failure (the device reads no input
//     for any other seat);
//   - after a move with !over and a turn that is not the mover's, the mover is shown (Result), then the hand-off (no
//     seat), then the new turn seat; a move that ends the round is never a turn change, and once over seat 0 is shown.
//
// A test double of GameVM's hand-off, and it says so: it is more permissive in one way (no timer hold, no queued
// events, no late-timer drop, no touch tagged with a frame) and stricter in none, and GamesCheckFlowTest pins that its
// draw and input sequence on pass-hidden equals GameVM's for the same four taps. `drawEveryLocalSeat` (on by default)
// is what GameVM never does and the check's reason to exist (R10): after the round begins and after every step, every
// seat this device plays is drawn (and seat 0, once a pass round is over), so a frame that faults or fills up is found
// whichever seat the device would have shown.
//
// After the round begins and after every step the snapshot is measured against R9's 700 B (half of
// GameCore::SNAPSHOT_BYTES), every frame's text commands are read, and `ver` is checked against the step (+1 for a
// move, unchanged for `move = false`). The first failure stops the round and names it, its step, and what happened.

#include <GameImages.h>
#include <GameSources.h>
#include <Session.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "RoundFile.h"

namespace games_check {

// R9: a game's snapshots stay under half of GameCore::SNAPSHOT_BYTES (the Session's hard limit), leaving a game room to
// grow.
inline constexpr size_t SNAPSHOT_CHECK_BYTES = 700;
static_assert(SNAPSHOT_CHECK_BYTES * 2 == GameCore::SNAPSHOT_BYTES, "R9: the check's limit is half the Session's");

// One game as the check plays it: the installer's modules and images (GameAssets, not the source folder) and the facts
// a round is read against.
struct GameUnderCheck {
  const GameScript::GameSources* sources = nullptr;
  const GameCore::GameImages* images = nullptr;
  GameFacts facts;
};

struct PlayOptions {
  bool drawEveryLocalSeat = true;
};

// The text commands of one published frame.
struct DrawnFrame {
  uint8_t seat = 0;
  std::vector<std::string> texts;
};

struct RoundReport {
  // "round '<name>', step <n>: ..." (step 0 is the round's beginning); empty for a round that played as its file says.
  std::vector<std::string> failures;
  // Every ch.log and print line, in order.
  std::vector<std::string> log;
  // Every frame drawn, in order.
  std::vector<DrawnFrame> frames;
  // The largest snapshot seen (the one that failed, when one did), and where the round ended.
  size_t maxSnapshotBytes = 0;
  uint32_t ver = 0;
  bool over = false;
  uint16_t winners = 0;

  bool ok() const { return failures.empty(); }
};

// Plays `round` (its `steps` function, if any, is called once after the round begins). Never throws; a game's fault,
// a Lua error, or a rule broken is a failure in the report.
RoundReport playRound(Round& round, const GameUnderCheck& game, const PlayOptions& options = {});

}  // namespace games_check
