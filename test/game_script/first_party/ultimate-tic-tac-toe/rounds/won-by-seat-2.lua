-- Seat 2 (O) wins three small boards in a line.
-- The moves come from tools/make_rounds.py (seed 2664, 32 moves), which has its own rules.
local taps = require("taps")

local moves = {
  {9, 7}, {7, 7}, {7, 8}, {8, 1}, {1, 3}, {3, 7}, {7, 3}, {3, 1}, {1, 1}, {1, 7}, {7, 6}, {6, 3}, {3, 5},
  {5, 7}, {7, 5}, {5, 4}, {4, 5}, {5, 1}, {1, 6}, {6, 1}, {1, 9}, {9, 6}, {6, 7}, {7, 9}, {9, 2}, {2, 1},
  {2, 7}, {7, 4}, {4, 1}, {7, 1}, {2, 5}, {3, 4},
}

local steps = taps.steps(moves)
-- The last frame says how the round ended.
steps[#steps].shows = "Player 2 (O) wins"

return { mode = "pass", steps = steps, winners = { 2 } }
