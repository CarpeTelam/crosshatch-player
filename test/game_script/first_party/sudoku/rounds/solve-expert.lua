-- Solves an Expert grid: the steps write the answer of the dealt grid, found with the game's own solver, into every empty
-- cell. The clock moves 83 seconds before the last digit, so the end screen's best time (the only one, kept for the
-- band) is 1:23.
local taps = require("taps")
local drawn = require("drawn")

return {
  mode = "solo",
  settings = { level = "Expert" },
  seed = 14,
  steps = function(state)
    drawn.frame(state, false)
    return taps.solve_best(state)
  end,
  winners = { 1 },
}
