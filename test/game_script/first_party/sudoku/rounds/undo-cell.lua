-- UNDO takes back one cell at a time, and says so when nothing is left.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 24,
  steps = function(state)
    rules.undo_cell(state)
    local answer, empties = taps.answer(state), taps.empties(state)
    local a, b = empties[1], empties[2]
    local steps = taps.write({}, a, tonumber(answer:sub(a, a)))
    taps.write(steps, b, taps.wrong_digit(state, b))
    taps.rail(steps, 3)
    taps.rail(steps, 3)
    taps.rail(steps, 3, { move = false, shows = "Nothing to undo" })
    return steps
  end,
  unfinished = true,
}
