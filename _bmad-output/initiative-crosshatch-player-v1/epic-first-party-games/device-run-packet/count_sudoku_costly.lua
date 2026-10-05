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
print("first CHECK on a fresh VM", (tap_menu(game, state, ui, layout, 3)), ui.msg)
print("HINT after it", (tap_menu(game, state, ui, layout, 1)), ui.sel, ui.msg)
game, state, ui, layout = fresh_game()
print("first HINT on a fresh VM", (tap_menu(game, state, ui, layout, 1)), ui.sel, ui.msg)

local solver = require("solver")
local build = measure(solver.grade, string.rep("0", 81))
solver.grade(string.rep("0", 81))
local a = measure(solver.answer, GRID)
local h = measure(solver.hint, GRID)
print("solver: table building " .. 11023 .. " (first grade call " .. build .. "), answer " .. a .. ", hint " .. h)

if arg[2] == "search" then
  local grid, puzzles = require("grid"), require("puzzles")
  local _, digits = puzzles.get(4, 59)
  math.randomseed(99)
  local worst, worst_grid = 0
  for _ = 1, 3000 do
    local v = grid.symmetry(digits, math.random)
    local total = measure(solver.answer, v) + measure(solver.hint, v) + 11023
    if total > worst then worst, worst_grid = total, v end
  end
  print("costliest of 3000 symmetries: " .. worst, worst_grid, worst_grid == GRID and "(the packet's GRID)" or "(NOT the packet's GRID)")
end
