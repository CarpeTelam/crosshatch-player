-- taps: rounds read as move lists. A move is {b, c}: cell c (1..9, row by row) of small board b (1..9, row by row),
-- as the game's own moves are; this module turns one into the canvas pixel a player taps, through the game's board
-- module and the canvas size, so a round has no pixel in it.
local board = require("board")

local taps = {}

local function layout() return board.layout(ch.screen.w, ch.screen.h) end

-- The canvas centre x, y of cell c of small board b.
function taps.tap(b, c)
  local row = ((b - 1) // 3) * 3 + (c - 1) // 3 + 1
  local col = ((b - 1) % 3) * 3 + (c - 1) % 3 + 1
  local x, y, w, h = board.cell_rect(layout(), row, col)
  return x + w // 2, y + h // 2
end

-- The centre x, y of the question button, in the 466 x 788 box (board.origin).
function taps.help()
  local ox, oy = board.origin(ch.screen.w, ch.screen.h)
  return ox + board.W - 40, oy + 48
end

-- A canvas point outside the 9 x 9 grid (below it) and clear of the question button.
function taps.off_grid()
  local L = layout()
  return L.x + L.size // 2, L.y + L.size + 40
end

-- One step: seat taps cell c of board b; `extra` adds its keys (move = false, shows = "...").
function taps.step(seat, b, c, extra)
  local x, y = taps.tap(b, c)
  local step = { seat = seat, x = x, y = y }
  for key, value in pairs(extra or {}) do step[key] = value end
  return step
end

-- The steps of a move list {{b, c}, ...}: seat 1 plays the first move and the seats alternate.
function taps.steps(moves)
  local steps = {}
  for i, move in ipairs(moves) do steps[i] = taps.step((i - 1) % 2 + 1, move[1], move[2]) end
  return steps
end

return taps
