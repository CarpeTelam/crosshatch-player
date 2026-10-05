#pragma once

// The games check's glue to the installer: what a game folder's package does on the fake SD card, and the three things
// the check runs on what the installer wrote (GameAssets, not the source folder, so it plays what ships):
//
//   checkPackage   the real packer's package installs with the real installer, the registry lists it whole, its
//                  hash is the packer's, and main.lua's load nests no deeper than the device takes
//   runGameChecks  the companion folder's checks.lua (C2)
//   playRounds     every rounds/<name>.lua of the companion folder (C1), each over RoundPlayer
//
// Parametric in the roots, so GamesCheckTest runs it over games/ and test/game_script/first_party/ and the engine tests
// over scratch trees. The packages come from harness/pack_games.py (`<packed>/<id>.chgame`, `.hash`, or `.packerror`).
// Every function starts from an empty fake card and installs the package again, so none depends on another's run.
//
// Linked with game_installer_src (the packer's package goes through GamePackageInstaller) and game_harness_src
// (GameAssets, MatchStore), over the harness doubles: the fake card, the PSRAM stub, and the captured log.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "RoundPlayer.h"

namespace games_check {

// The three folders a check reads: the games root (`<games>/<id>/`), the companion root
// (`<companion>/<id>/{rounds/*.lua, checks.lua, top-level modules}`), and where pack_games.py wrote the packages.
//
// `canvas` is `ch.screen` for the rounds and the game's own checks: the check names no game, so it plays every game on
// each canvas it is given (GamesCheckTest: the Sticky's 474 x 788 and the X4 Pro's 466 x 788).
struct Roots {
  std::string games;
  std::string companion;
  std::string packed;
  CanvasSize canvas = CANVAS_474;
};

// What a check found: failures end a test red, notes (a skipped mode, a count) are logged and never fail it.
struct Report {
  std::vector<std::string> failures;
  std::vector<std::string> notes;

  bool ok() const { return failures.empty(); }
  void fail(std::string message) { failures.push_back(std::move(message)); }
  void note(std::string message) { notes.push_back(std::move(message)); }
  // Failures and notes, one per line, for a test's failure message and log.
  std::string text() const;
};

// The id's package installs and loads: no `.packerror` (else its text, the packer's stderr, is the failure), the
// installer reports one game installed, the registry lists it with a manifest that passes this host's check, and the
// hash it read back is the one the packer printed, and main.lua's load nests no module loads beyond MAX_LOAD_NESTING
// (ScriptVm.h: the double of the device's parser-headroom refusal, one rule for every game). The module-loading probe
// runs at the root's canvas (roots.canvas), so a load that reads ch.screen sees the canvas under check.
Report checkPackage(const Roots& roots, const std::string& id);

// The game's own checks (C2): `<companion>/<id>/checks.lua`, a module in the game's sandbox (math.random seeded 1) that
// returns a non-empty list of {name, run}. Loading it is one guarded call and each run() another, each with a fresh
// 2,000,000-instruction budget. A check that raises fails by name and the rest run; a guard fault fails that check and
// stops the game's remaining checks, counted; a missing or empty list fails. No checks.lua: none run (a note).
Report runGameChecks(const Roots& roots, const std::string& id);

// Every round of `<companion>/<id>/rounds/*.lua`, played in the modes the manifest declares and this host can start;
// each other declared mode (`nearby`) is a note ("skipped"), never a failure. No rounds (the folder missing or empty)
// is a failure, and a failing round does not stop the ones after it. `details`, when given, receives each round's whole
// report (its frames and log) by round name, for the engine tests; `options` are RoundPlayer's.
using RoundDetails = std::vector<std::pair<std::string, RoundReport>>;
Report playRounds(const Roots& roots, const std::string& id, RoundDetails* details = nullptr,
                  const PlayOptions& options = {});

// A companion folder `<companion>/<id>/` needs `<games>/<id>/`: a failure when there is none.
Report companionHasGame(const Roots& roots, const std::string& id);

// A gtest parameter name for the id at `index` in its list: every character that is not a letter or a digit becomes `_`
// and the index is appended, so a folder name with a `.`, a space, or only a `-` against `_` from another id still
// gives a valid name, unique in its suite (gtest aborts at registration on an invalid or repeated one).
std::string testNameOf(const std::string& id, size_t index);

}  // namespace games_check
