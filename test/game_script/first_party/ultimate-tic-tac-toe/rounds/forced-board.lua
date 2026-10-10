-- A move sends the opponent to the board of its cell, a tap in another board is refused, and the legal reply is played.
-- Seat 1 wins board 6 with its move in cell 6 (a won target: the opponent's choice is free, and plays board 7), and a
-- later move into cell 6 points at that won board: a tap in it is refused and a move in another open board is played.
-- The rows come from tools/make_rounds.py (seed 3497): {b, c, played}.
local taps = require("taps")

local rows = {
  {6, 9, true}, {8, 5, false}, {9, 6, true}, {6, 3, true}, {3, 6, true}, {6, 6, true}, {7, 2, true},
  {2, 6, true}, {6, 5, false}, {8, 3, true},
}

local steps, played = {}, 0
for _, row in ipairs(rows) do
  local seat = played % 2 + 1
  if row[3] then
    steps[#steps + 1] = taps.step(seat, row[1], row[2])
    played = played + 1
  else
    steps[#steps + 1] = taps.step(seat, row[1], row[2], { move = false, shows = "Play in the highlighted board" })
  end
end

return { mode = "pass", steps = steps, unfinished = true }
