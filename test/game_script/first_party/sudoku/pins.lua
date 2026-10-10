-- pins.lua: what the pin modules share (rules.lua, interaction.lua, drawn.lua, marks.lua): the dealt grid with nothing
-- written, a move applied, taps on the game's input, the clock a pin sets, a grid's answer, and the notation's digit
-- and unit arithmetic. A round loads only the modules whose pins its `steps` call (first_party/README.md, "The checks
-- VM heap"), so each module is a heap cost only its rounds pay; this one is the small part all of them need. A module
-- requires the game's own modules before it requires this one, so that none of them loads nested inside it.
local game = require("main")
local grid = require("grid")
local layout = require("layout")
local solver = require("solver")

-- The rounds' VM was within a few KB of the device's 256 KB heap and a frame's draw leaves tens of KB of garbage, so a round
-- could fault "not enough memory" (README, "The check VMs' limits"; a check VM has a 1 MB heap now, and this stays as built):
-- generational collection, with a full one at the start of the heavy pins (taps in interaction.lua, frame and look in
-- drawn.lua) and at the end of the last two.
collectgarbage("generational")

local CLUE, NOMARK, ZEROS = "Clues cannot be changed", "That mark is not possible", string.rep("\0", 162)

local function eq(got, want, what)
  if got ~= want then error((what or "") .. " got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- The dealt grid with nothing written: a state of the round's level and puzzle.
local function fresh(state) return { l = state.l, v = state.v, n = ZEROS, u = "", t = 0 } end

local function empties(s)
  local out = {}
  for c = 1, 81 do
    if s.v:byte(c) == 48 then out[#out + 1] = c end
  end
  return out
end

local solutions = {}
local function answer(s)
  local clues = s.v:gsub("%l", "0")
  solutions[clues] = solutions[clues] or assert(solver.answer(clues))
  return solutions[clues]
end

local function digit_at(s, c) return answer(s):byte(c) - 48 end

local function letter(d) return string.char(96 + d) end

-- A move applied (dt 0): the state, or an error.
local function mv(s, ...)
  local move = { ... }
  move[#move + 1] = 0
  return assert(game.apply(s, 1, move))
end

local function mask(s, c) return (string.unpack("<I2", s.n, 2 * c - 1)) end

-- A digit cell c can take.
local function candidate(s, c)
  local m = grid.candidates(s.v)[c]
  for d = 1, 9 do
    if m & (1 << d - 1) ~= 0 then return d end
  end
end

local function rejects(s, move, reason)
  local before = s.v .. s.n .. s.u .. (s.z or "") .. s.t .. tostring(s.h)
  local result, got = game.apply(s, 1, move)
  eq(result, nil)
  eq(got, reason)
  assert(#got <= 64)
  eq(s.v .. s.n .. s.u .. (s.z or "") .. s.t .. tostring(s.h), before)
end

-- Taps on the game's input, with ui kept by the caller; the first call starts ui.
local function tap(s, ui, x, y) return game.input(s, 1, ui, { kind = "tap", x = x, y = y }) end
local function on_cell(s, ui, c) return tap(s, ui, layout.centre(layout.cell_rect(c))) end
local function on_key(s, ui, d) return tap(s, ui, layout.centre(layout.key_rect(d))) end
local function on_rail(s, ui, i) return tap(s, ui, layout.centre(layout.rail_rect(i))) end
local function on_menu(s, ui, i)
  ui.panel = "menu"
  return tap(s, ui, layout.centre(layout.menu_rect(i)))
end

-- The digit of a cell's byte and whether it is a clue, from the notation alone ("1".."9" a clue, "a".."i" the
-- player's).
local function digit_of(b)
  if b >= 49 and b <= 57 then return b - 48, true end
  if b >= 97 and b <= 105 then return b - 96, false end
end

-- The row, column, and box (0 to 8 each) of cell c, by arithmetic: no grid.lua.
local function units_of(c)
  local row, col = (c - 1) // 9, (c - 1) % 9
  return row, col, row // 3 * 3 + col // 3
end

-- The clock a pin sets: with_clock runs f with ch.time.ms reading time.now, and puts the real one back after an error
-- too.
local time = { now = 0 }
local function with_clock(f)
  local real = ch.time
  ch.time = { ms = function() return time.now end }
  local ok, err = pcall(f)
  ch.time = real
  if not ok then error(err, 0) end
end

return {
  CLUE = CLUE,
  NOMARK = NOMARK,
  ZEROS = ZEROS,
  eq = eq,
  fresh = fresh,
  empties = empties,
  answer = answer,
  digit_at = digit_at,
  letter = letter,
  mv = mv,
  mask = mask,
  candidate = candidate,
  rejects = rejects,
  tap = tap,
  on_cell = on_cell,
  on_key = on_key,
  on_rail = on_rail,
  on_menu = on_menu,
  digit_of = digit_of,
  units_of = units_of,
  time = time,
  with_clock = with_clock,
}
