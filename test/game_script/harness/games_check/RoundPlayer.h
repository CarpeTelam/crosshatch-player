#pragma once

// Plays one Round headlessly over a game's own LuaGame, a Session, and the production round loop (MatchRounds): a fresh
// LuaGame per round, SeededRandom(seed) as its HostPorts::random, a clock the steps advance, the device canvas
// (GameUnderCheck::canvas, which the check sets to each device's in turn; it has no default), and the round's settings
// (or the manifest's defaults) as ctx.settings. The played game's VM has the device's heap cap and instruction budget
// (VmLimits::device()), never the check's larger ones. Solo and open pass rounds use MatchRounds' start and
// step, as GameVM does. A hidden pass round follows GameVM::stepHandOff (spine AD-21, D2 of the games check's plan):
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
//
// Also checked, each with its reason where it is made (RoundPlayer.cpp):
//   - a tap off the canvas under check fails its step (the device drops such a tap);
//   - once the round is over, the frame the device shows (seat 1 in solo, seat 0 in pass) starts no text inside the
//     host's end-of-round dialog on a 788-tall canvas (HostBounds.h);
//   - `shows` in a hidden round is met by a frame the flow itself drew, never by drawEveryLocalSeat's;
//   - the heap margin: a round that plays clean at the device's Lua heap cap is played again with the cap lowered by
//     HEAP_MARGIN_BYTES, and a fault there fails the round (PlayOptions::heapMargin);
//   - the restore probe (PlayOptions::restoreProbe): the Continue seam, in a second VM beside the live one.

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

// How far under the device's Lua heap cap (GameScript::LUA_HEAP_BYTES) a played game must stay. The cap counts garbage
// and a first-fit region, so a peak is not a number the arena reports (its peakBytes() counts block headers, 16 B on
// the host against 8 B on the device, and lib/ is not this check's to extend): the margin is stated exactly as "the
// round still plays with the cap this much lower". 16 KiB, so a game keeps room for a longer string or a larger
// state, the way R9's 700 B keeps a snapshot room to grow.
inline constexpr size_t HEAP_MARGIN_BYTES = 16 * 1024;

// One game as the check plays it: the installer's modules and images (GameAssets, not the source folder) and the facts
// a round is read against.
struct GameUnderCheck {
  // `ch.screen` for the round: the Sticky's 474 x 788 or the X4 Pro's 466 x 788, named by every caller.
  explicit GameUnderCheck(const CanvasSize canvasUnderCheck) : canvas(canvasUnderCheck) {}

  const GameScript::GameSources* sources = nullptr;
  const GameCore::GameImages* images = nullptr;
  GameFacts facts;
  CanvasSize canvas;
};

struct PlayOptions {
  bool drawEveryLocalSeat = true;
  // Play the round again with the Lua heap cap lowered by HEAP_MARGIN_BYTES after it plays clean (the margin gate).
  bool heapMargin = true;
  // Before each step, and once after the last, restore the current snapshot into a fresh game in a second VM, start
  // it, and draw every local seat: the Continue seam, where the device builds a new VM, restores the snapshot, and
  // starts every seat's `ui` empty (GameVM::run). It proves restore, start and draw do not fault or raise from a
  // snapshot with an empty `ui`; it does not prove that the round's later taps mean the same there (they were written
  // against the `ui` the live VM built), so the live VM plays on and the restored one is dropped.
  bool restoreProbe = false;
};

// The text commands of one published frame, and where each starts (the y of the command, the box's top).
struct DrawnFrame {
  uint8_t seat = 0;
  std::vector<std::string> texts;
  std::vector<int> tops;  // tops[i] is texts[i]'s y
  // Drawn by drawEveryLocalSeat's sweep, which the device never does, and not by the flow itself.
  bool sweep = false;
};

struct RoundReport {
  // "round '<name>', step <n>: ..." (step 0 is the round's beginning); empty for a round that played as its file says.
  std::vector<std::string> failures;
  // Every ch.log and print line, in order.
  std::vector<std::string> log;
  // With PlayOptions::restoreProbe: the ch.log lines of the last restored game (its start and its draws), which
  // GamesCheckFlowTest compares with what GameVM's resume of the same snapshot logs.
  std::vector<std::string> restoreLog;
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
