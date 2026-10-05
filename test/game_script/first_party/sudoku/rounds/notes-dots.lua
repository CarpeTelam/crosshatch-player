-- Notes as dots (the default): NOTES on, marks in three cells, the focus on a digit, a mark toggled off again.
local taps = require("taps")
local interaction = require("interaction")
local drawn = require("drawn")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 21,
  steps = function(state)
    interaction.taps(state)
    drawn.frame(state, false)
    drawn.look(state)
    local steps = taps.rail({}, 1, { move = false })
    taps.notes_three(steps, state)
    return steps
  end,
  unfinished = true,
}
