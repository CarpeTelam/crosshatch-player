-- Notes as digits: NOTES AS switched in the MENU, then marks in three cells, the focus on a digit, a mark toggled off.
local taps = require("taps")
local drawn = require("drawn")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 22,
  steps = function(state)
    drawn.frame(state, true)
    drawn.look(state)
    local steps = taps.menu_row({}, 6, { move = false, shows = "NOTES AS: DIGITS" })
    taps.menu(steps, 8, { move = false })
    taps.rail(steps, 1, { move = false })
    taps.notes_three(steps, state)
    return steps
  end,
  unfinished = true,
}
