#pragma once

// The numbers the games check holds a game to, in one place: where the host puts its own end-of-round dialog and the
// hidden pass Result banner on a game's canvas, and the limits of the VMs that run only the check's own code. The
// harness reads them (RoundPlayer's end-frame pin, ScriptVm's limits) and a check VM sees the same values as the
// Lua global `host` (ScriptVm.h), so a game's checks (battleship/checks.lua, sudoku/checks.lua, the UTTT end-frame
// pin) and the harness cannot disagree about where the host draws.
//
// A test double, and it says so: the bounds are a copy of the host's layout (GameMatchActivity's dialog and banner,
// which this host cannot draw), measured once on the simulator's screenshots, so a change to the host's dialog is a
// change here. They hold for the 788-tall canvases the check plays (CANVAS_HEIGHT), the X4 Pro's 466 x 788 and the
// Sticky's 474 x 788: both insets start 9 px down, so a panel y is a canvas y plus 9, on either. The dialog is
// measured in English `tr()` text; a longer translation could move its top edge.

#include <ArenaAllocator.h>

#include <cstddef>
#include <cstdint>

namespace games_check::host {

// The canvas height the dialog and banner bounds below hold for. A canvas of another height has other bounds.
inline constexpr int CANVAS_HEIGHT = 788;

// The end-of-round dialog (Play again, Leave), which the host draws over a game's last frame once the round is over.
// Measured on `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-sudoku-screenshots/solved.png`
// and `story-battleship-screenshots/over-menu.png` (480 x 800 panel screenshots, bezel top inset 9): the dialog's
// border is a 2 px line, drawn at panel rows 268-269 at its top and 535-536 at its bottom, found by scanning the
// image's rows for a dark run of 360 px (x 60 to 420), so the dialog covers canvas rows 259 up to and including 527.
// A game's text must start above DIALOG_TOP (or below DIALOG_BOTTOM) and, to be seen, end at or above it.
inline constexpr int DIALOG_TOP = 259;
// One past the dialog's last row: the first canvas row below it.
inline constexpr int DIALOG_BOTTOM = 528;

// The hidden pass Result banner ("Tap to pass to player N"), drawn over the mover's frame after a move that passes the
// turn. Measured on `story-battleship-screenshots/result-hit.png` the same way: its border's first row is panel row
// 649 (rows 649-650 at its top, 779-780 at its bottom), canvas row 640. The Battleship story's plan and the
// cross-story review quoted "about 649", which is the panel row; the canvas row is 9 less.
inline constexpr int BANNER_TOP = 640;

// The limits of a VM that runs only check code (VmLimits::check()): a round file and its `steps(state)`, a game's
// checks.lua, and the clash probe. The device's are 256 KiB of Lua heap (GameScript::LUA_HEAP_BYTES) and 2,000,000
// instructions a call (CallGuard::INSTRUCTION_BUDGET); a check VM shares them with no game, so a check that is
// heavier than a game may be does not fault at a cliff a few KB or a few percent away (the cross-story review, row 4:
// Sudoku's checks VM faulted at a heap cap 3.8 KB lower, its batches peaked at 1.7 to 1.8 M of 2 M instructions).
// These are well above the device's and nothing measures them: 4 times the heap, 8 times the instructions. A game's
// own cost is still measured against the device's through `within_device_budget` and the played game's own VM.
inline constexpr size_t CHECK_LUA_HEAP_BYTES = 1024 * 1024;
// Lua's region of a check VM's arena: the device's region-to-cap ratio (ArenaAllocator.h measured 448 KiB for 256
// KiB, 1.75 times), so the cap, not the region, ends a heap bomb.
inline constexpr size_t CHECK_LUA_REGION_BYTES =
    CHECK_LUA_HEAP_BYTES / GameScript::LUA_HEAP_BYTES * GameScript::LUA_REGION_BYTES;
static_assert(CHECK_LUA_HEAP_BYTES % GameScript::LUA_HEAP_BYTES == 0,
              "the check heap is a whole multiple of the device's");
inline constexpr uint32_t CHECK_INSTRUCTION_BUDGET = 16000000;

}  // namespace games_check::host
