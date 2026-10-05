-- count_sudoku_costly.lua: the host instruction counts behind the packet's HINT and CHECK figures (epic-first-party-games
-- entry 5). Standard Lua 5.5.1 built from lib/lua/src, as test/game_script/first_party/sudoku/tools/make_bank.py builds it
-- (`cc -O2 -DLUA_USE_LINUX -DLUA_COMPAT_GLOBAL=0 -I lib/lua/src -include lib/lua/port/luai_throw.h`, every .c but luac.c).
--
--   lua count_sudoku_costly.lua <games/sudoku> [search]
--
-- Part 1 (always): on the grid sudoku-costly deals, the instructions (a count hook of 1) of the game's own `input` for a
-- tap on MENU's CHECK row on a fresh VM, then HINT, then HINT on another fresh VM (a resume), and of the solver's own
-- `answer` and `hint`. The first scope includes loading the solver module and the menu handling, so it reads a few hundred
-- instructions above the second.
-- A count hook of 1 counts Lua VM instructions only: time spent inside a C function (`table.sort`, `string.*`) is one
-- instruction here, so the packet's seconds are estimates. Expected output at `daedbeab` on a Lua 5.5.1 build:
--   first CHECK 258030, HINT after it 45186 (cell 41), first HINT 302708 (cell 41), table building 11023, answer 246397,
--   hint 44632, and with `search` costliest of 3000 symmetries 302052, the packet's GRID (about three minutes).
-- The menu rows are the game's own (`layout.menu_at`): 1 is HINT and 3 is CHECK, asserted below by the messages.
-- Part 2 (with `search`): the symmetry of bank puzzle 59 (Expert, `0034ee8363e5`) out of 3,000 (math.randomseed(99)) whose
-- solver `answer` plus `hint` plus the one-time table building (11,023) costs most; the packet's GRID is that grid.
local dir = arg[1] or "games/sudoku"
package.path = dir .. "/?.lua;" .. package.path
local GRID = "400007000015000720000010003006700080050001009200005060004050008600080900009600040"
local count = 0
local function hook() count = count + 1 end
local function measure(f, ...)
  count = 0
  debug.sethook(hook, "", 1)
  local a, b = f(...)
  debug.sethook()
  return count, a, b
end

ch = { screen = { w = 466, h = 788 }, log = print, time = { ms = function() return 0 end },
  store = { get = function() return {} end, set = function() end } }

local function fresh_game()
  for _, name in ipairs({ "main", "solver", "view", "layout", "grid", "board", "help", "puzzles" }) do package.loaded[name] = nil end
  local game = require("main")
  local state = { l = 4, v = GRID, n = string.rep("\0", 162), u = "", t = 0 }
  local ui = {}
  game.input(state, 1, ui, { kind = "timer" }) -- starts ui
  return game, state, ui, require("layout")
end

local function tap_menu(game, state, ui, layout, row)
  ui.panel = "menu"
  for x = 0, 465, 3 do
    for y = 0, 787, 3 do
      if layout.menu_at(x, y) == row then
        return measure(game.input, state, 1, ui, { kind = "tap", x = x, y = y })
      end
    end
  end
end

local game, state, ui, layout = fresh_game()
local MENU_HINT, MENU_CHECK = 1, 3
print("first CHECK on a fresh VM", (tap_menu(game, state, ui, layout, MENU_CHECK)), ui.msg)
assert(ui.msg == "All correct", "row 3 is no longer CHECK")
print("HINT after it", (tap_menu(game, state, ui, layout, MENU_HINT)), ui.sel, ui.msg)
assert(ui.sel, "row 1 is no longer HINT")
game, state, ui, layout = fresh_game()
print("first HINT on a fresh VM", (tap_menu(game, state, ui, layout, MENU_HINT)), ui.sel, ui.msg)

-- The solver's one-time table building is the first grade call minus a second, equal call, as grade.lua measures it.
package.loaded["solver"] = nil -- a fresh load, so its tables are not built yet
local solver = require("solver")
local blank = string.rep("0", 81)
local first = measure(solver.grade, blank)
local second = measure(solver.grade, blank)
local build = first - second
local a = measure(solver.answer, GRID)
local h = measure(solver.hint, GRID)
print("solver: table building " .. build .. ", answer " .. a .. ", hint " .. h .. ", sum " .. (build + a + h))

if arg[2] == "search" then
  local grid, puzzles = require("grid"), require("puzzles")
  local _, digits = puzzles.get(4, 59)
  math.randomseed(99)
  local worst, worst_grid = 0
  for _ = 1, 3000 do
    local v = grid.symmetry(digits, math.random)
    local total = measure(solver.answer, v) + measure(solver.hint, v) + build
    if total > worst then worst, worst_grid = total, v end
  end
  print("costliest of 3000 symmetries: " .. worst, worst_grid, worst_grid == GRID and "(the packet's GRID)" or "(NOT the packet's GRID)")
end
