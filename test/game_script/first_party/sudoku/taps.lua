-- taps: rounds read as lists of what a player does. Every tap is computed from the game's own layout module, so a round
-- has no pixel in it. A function here appends steps to `list` (a rounds `steps` list) and returns it. A tap that
-- changes nothing says `move = false` (pass it in `extra`, as the cell and menu taps below do); `extra` also takes
-- `wait` and `shows`.
local layout = require("layout")
local grid = require("grid")
local solver = require("solver")

local taps = {}

local function add(list, x, y, extra)
  local step = { seat = 1, x = x, y = y }
  for key, value in pairs(extra or {}) do step[key] = value end
  list[#list + 1] = step
  return list
end

-- A tap on cell c, pad key d, rail button i (1 NOTES, 2 ERASE, 3 UNDO, 4 MENU), or MENU row i (1 HINT, 2 FILL NOTES,
-- 3 CHECK, 4 SHOW REMAINING, 5 SHADE PEERS, 6 NOTES AS, 7 HOW TO PLAY, 8 CLOSE).
function taps.cell(list, c, extra)
  local x, y = layout.centre(layout.cell_rect(c))
  return add(list, x, y, extra)
end

function taps.key(list, d, extra)
  local x, y = layout.centre(layout.key_rect(d))
  return add(list, x, y, extra)
end

function taps.rail(list, i, extra)
  local x, y = layout.centre(layout.rail_rect(i))
  return add(list, x, y, extra)
end

function taps.menu(list, i, extra)
  local x, y = layout.centre(layout.menu_rect(i))
  return add(list, x, y, extra)
end

-- Opens the MENU panel and taps row i; the row's own step takes `extra`.
function taps.menu_row(list, i, extra)
  taps.rail(list, 4, { move = false })
  return taps.menu(list, i, extra)
end

-- Selects cell c (no move), then taps digit d: a write (or a clear), which moves.
function taps.write(list, c, d, extra)
  taps.cell(list, c, { move = false })
  return taps.key(list, d, extra)
end

-- Selects cell c with NOTES on, then taps digit d: a note, which moves. NOTES must be on already.
function taps.note(list, c, d, extra) return taps.write(list, c, d, extra) end

-- The clues of a state (its player's digits left out), and the answer to them.
function taps.clues(state) return (state.v:gsub("%l", "0")) end
function taps.answer(state) return assert(solver.answer(taps.clues(state))) end

-- The cells of the state that are empty, in order.
function taps.empties(state)
  local out = {}
  for c = 1, 81 do
    if state.v:sub(c, c) == "0" then out[#out + 1] = c end
  end
  return out
end

-- Writes the answer into every empty cell of the state but the last `leave` (0 for all). opts.wait: the clock moves that
-- many milliseconds before the last digit tap. Returns the list.
function taps.solve(list, state, opts)
  opts = opts or {}
  local answer, empties = taps.answer(state), taps.empties(state)
  local last = #empties - (opts.leave or 0)
  for i = 1, last do
    local c = empties[i]
    taps.write(list, c, tonumber(answer:sub(c, c)), i == last and { wait = opts.wait } or nil)
  end
  return list
end

-- A digit (1..9) a cell cannot hold in the answer, and a digit a peer of the cell does not rule out when there is one
-- (the wrong digit a round writes is a legal-looking one).
function taps.wrong_digit(state, c)
  local answer = taps.answer(state)
  local right = tonumber(answer:sub(c, c))
  for d = 1, 9 do
    if d ~= right and not grid.peer_has(state.v, c, d) then return d end
  end
  return right % 9 + 1
end

return taps
