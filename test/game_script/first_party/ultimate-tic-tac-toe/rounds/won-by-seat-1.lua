-- Seat 1 (X) wins three small boards in a line.
-- The moves come from tools/make_rounds.py (seed 2423, 29 moves), which has its own rules.
local taps = require("taps")

local moves = {
  {4, 2}, {2, 1}, {1, 3}, {3, 5}, {5, 8}, {8, 6}, {6, 5}, {5, 4}, {4, 3}, {3, 9}, {9, 9}, {9, 2}, {2, 2},
  {2, 4}, {4, 1}, {1, 4}, {5, 5}, {5, 6}, {6, 6}, {6, 1}, {1, 1}, {1, 9}, {9, 7}, {7, 5}, {5, 2}, {2, 5},
  {2, 8}, {8, 4}, {6, 4},
}

local steps = taps.steps(moves)
-- The last frame says how the round ended.
steps[#steps].shows = "Player 1 (X) wins"

return { mode = "pass", steps = steps, winners = { 1 } }
