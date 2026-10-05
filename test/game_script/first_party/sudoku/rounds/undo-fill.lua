-- FILL NOTES is one step in the ring: a write, a fill, a write, then UNDO takes back the write, the whole fill, and the
-- write before the fill, and a fourth UNDO finds nothing.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 25,
  steps = function(state)
    rules.undo_fill(state)
    local answer, empties = taps.answer(state), taps.empties(state)
    local a, b = empties[1], empties[2]
    local steps = taps.write({}, a, tonumber(answer:sub(a, a)))
    taps.menu_row(steps, 2)
    taps.write(steps, b, tonumber(answer:sub(b, b)))
    taps.rail(steps, 3)
    taps.rail(steps, 3)
    taps.rail(steps, 3)
    taps.rail(steps, 3, { move = false, shows = "Nothing to undo" })
    return steps
  end,
  unfinished = true,
}
