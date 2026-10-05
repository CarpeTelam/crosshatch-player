-- HINT on a wrong digit selects that cell and says "Wrong digit"; writing the right digit over it goes on.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 27,
  steps = function(state)
    rules.hint(state)
    local answer, c = taps.answer(state), taps.empties(state)[3]
    local steps = taps.write({}, c, taps.wrong_digit(state, c))
    taps.menu_row(steps, 1, { shows = "Wrong digit" })
    taps.key(steps, tonumber(answer:sub(c, c)))
    return steps
  end,
  unfinished = true,
}
