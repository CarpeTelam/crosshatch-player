-- Notes as dots (the default): NOTES on, marks in three cells, the focus on a digit, a mark toggled off again.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 21,
  steps = function(state)
    rules.taps(state)
    rules.frame(state, false)
    rules.look(state)
    local steps = taps.rail({}, 1, { move = false })
    taps.notes_three(steps, state)
    return steps
  end,
  unfinished = true,
}
