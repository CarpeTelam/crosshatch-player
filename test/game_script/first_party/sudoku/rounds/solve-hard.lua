-- Solves a Hard grid: the steps write the answer of the dealt grid, found with the game's own solver, into every empty
-- cell. The clock moves 83 seconds before the last digit, so the end screen's best time (the only one, kept for the
-- band) is 1:23.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Hard" },
  seed = 13,
  steps = function(state)
    rules.frame(state, true)
    local steps = taps.solve({}, state, { wait = 83000 })
    steps[#steps].shows = "Best 1:23"
    return steps
  end,
  winners = { 1 },
}
