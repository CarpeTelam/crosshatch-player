-- CHECK strikes a wrong digit and says how many; it moves nothing. The next edit ends it, and CHECK then says all correct.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 29,
  steps = function(state)
    rules.clash(state)
    local answer, c = taps.answer(state), taps.empties(state)[2]
    local steps = taps.write({}, c, taps.wrong_digit(state, c))
    taps.menu_row(steps, 3, { move = false, shows = "1 wrong digit" })
    taps.write(steps, c, tonumber(answer:sub(c, c)))
    taps.menu_row(steps, 3, { move = false, shows = "All correct" })
    return steps
  end,
  unfinished = true,
}
