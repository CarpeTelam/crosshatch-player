-- Notes as digits (the default, from an empty store): the MENU reads NOTES AS: DIGITS, then NOTES on, marks in three
-- cells, the focus on a digit, a mark toggled off.
local taps = require("taps")
local drawn = require("drawn")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 22,
  steps = function(state)
    drawn.frame(state, true)
    drawn.look(state)
    local steps = taps.rail({}, 4, { move = false, shows = "NOTES AS: DIGITS" })
    taps.menu(steps, 9, { move = false })
    taps.rail(steps, 1, { move = false })
    taps.notes_three(steps, state)
    return steps
  end,
  unfinished = true,
}
