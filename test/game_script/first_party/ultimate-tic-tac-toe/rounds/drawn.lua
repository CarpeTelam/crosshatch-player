-- No line of small boards is left for either seat while a small board is still open: a draw.
-- The moves come from tools/make_rounds.py (seed 703, 51 moves), which has its own rules.
local taps = require("taps")

local moves = {
  {1, 7}, {7, 6}, {6, 3}, {3, 8}, {8, 4}, {4, 7}, {7, 2}, {2, 6}, {6, 2}, {2, 1}, {1, 8}, {8, 5}, {5, 9},
  {9, 6}, {6, 6}, {6, 4}, {4, 5}, {5, 5}, {5, 2}, {2, 7}, {7, 4}, {4, 9}, {9, 4}, {4, 2}, {2, 3}, {3, 7},
  {7, 1}, {1, 1}, {1, 9}, {9, 7}, {7, 7}, {9, 3}, {3, 1}, {8, 6}, {6, 1}, {4, 6}, {3, 4}, {4, 8}, {8, 2},
  {2, 8}, {8, 9}, {9, 9}, {5, 4}, {5, 8}, {8, 7}, {3, 5}, {5, 3}, {3, 9}, {2, 2}, {2, 4}, {8, 1},
}

local steps = taps.steps(moves)
-- The last frame says how the round ended.
steps[#steps].shows = "Draw"

return { mode = "pass", steps = steps, winners = {} }
