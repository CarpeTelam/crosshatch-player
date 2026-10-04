#pragma once

// A round file (rounds/<name>.lua of a game's companion folder) read into a Round: one scripted playthrough of a game
// in one mode, with the winners it must end with. The file is a chunk run in a ScriptVm (the game's sandbox;
// `ch.screen` is the 474 x 788 device canvas; `require` finds the game's modules and the companion folder's top-level
// .lua files) that returns
//
//   { mode = "pass", settings = { level = "Easy" }, seed = 1,
//     steps = { { seat = 1, x = 120, y = 300 }, ... },      -- or: steps = function(state) return { ... } end
//     winners = { 1 } }                                     -- or: unfinished = true
//
// Each step is a tap `{seat, x, y, wait?, move?, shows?}`: `wait` milliseconds pass on the clock (ch.time.ms) before
// the tap and deliver no timer event; `move = false` says the tap changes nothing (the session's `ver` stays); `shows`
// is text the seat's next frame must hold. A `steps` function is called once, after the round begins, with the initial
// state decoded from the session's snapshot, under its own instruction budget in the round's VM, and returns the list.
//
// Every key is checked: an unknown key, a wrong type, an empty `steps`, both or neither of `winners` and `unfinished`,
// a `seed` that is not an integer in 0..4294967295, a mode the manifest does not declare (or that is not solo or
// pass), or a setting id or value the manifest lacks is an error that names the key.

#include <Manifest.h>
#include <Roster.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ScriptVm.h"

namespace games_check {

// What a round file is checked against: the game's installed manifest, its settings, the seats a pass match has on this
// host, and the modes this host can start (Manifest::check's CheckResult::modes).
struct GameFacts {
  const GameCore::Manifest* manifest = nullptr;
  const GameCore::ManifestSettings* settings = nullptr;
  int32_t hostMaxSeats = 2;
  uint8_t hostModes = 0;

  // The seats a match of `mode` has: 1 for solo, passSeats() for pass (0 when no pass match fits).
  uint8_t seatsOf(GameCore::Mode mode) const;
};

struct Step {
  int seat = 0;
  int x = 0;
  int y = 0;
  uint32_t waitMs = 0;
  bool moves = true;  // false: `move = false`, the tap must leave `ver` as it was
  bool hasShows = false;
  std::string shows;
};

struct Round {
  Round() = default;
  Round(const Round&) = delete;
  Round& operator=(const Round&) = delete;
  Round(Round&&) = default;
  Round& operator=(Round&&) = default;
  ~Round();

  std::string name;  // the file's name without ".lua"
  GameCore::Mode mode = GameCore::Mode::Solo;
  // The settings the round chose, as id and value; the manifest's defaults fill the rest.
  std::vector<std::pair<std::string, std::string>> settings;
  uint32_t seed = 1;
  // The list `steps` gave; empty while a `steps` function has not run (stepsFromFunction).
  std::vector<Step> steps;
  bool stepsFromFunction = false;
  bool unfinished = false;
  uint16_t winners = 0;  // bit seat - 1; no bit with `winners = {}` is a draw

  // The round's VM, alive until `steps` ran (and a `steps` function's reference in it).
  std::unique_ptr<OwnedVm> vm;
  int stepsRef = ScriptVm::NO_REF;

  // "rounds/<name>.lua", the name error messages carry.
  std::string file() const { return "rounds/" + name + ".lua"; }
};

// Runs `text` (rounds/<name>.lua) in `vm` and reads the table it returns into `out`, which keeps `vm`. False, with
// `error` naming the file and the key, for a file that raises, faults, or breaks a rule above.
bool loadRound(std::unique_ptr<OwnedVm> vm, const std::string& name, const std::string& text, const GameFacts& facts,
               Round& out, std::string& error);

// For a round whose `steps` is a function: calls it with the state `initialState` encodes and reads the list it returns
// into round.steps. True at once for a round with a list. False, with `error`, when the call raises or faults, or
// returns something that is no valid list of steps.
bool resolveSteps(Round& round, std::span<const uint8_t> initialState, const GameFacts& facts, std::string& error);

// A set of winners as text for a message: "{1,2}", "{}" for a draw.
std::string winnersText(uint16_t winners);

}  // namespace games_check
