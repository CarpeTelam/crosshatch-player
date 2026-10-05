-- ERASE: a clue selected is refused with its message (no move), an own digit is erased and the cell stays selected, and
-- ERASE on an empty cell with no notes does nothing. A digit tapped with a clue selected moves the focus. The refused
-- ERASE comes after 5 seconds, and the solve that follows shows 0:05: a refused move's time is not lost.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 23,
  steps = function(state)
    rules.apply(state)
    rules.clock(state)
    local clue
    for c = 1, 81 do
      if state.v:sub(c, c) ~= "0" then
        clue = c
        break
      end
    end
    local answer, empties = taps.answer(state), taps.empties(state)
    local c = empties[1]
    local steps = taps.cell({}, clue, { move = false })
    taps.key(steps, 5, { move = false })
    taps.rail(steps, 2, { move = false, wait = 5000, shows = "Clues cannot be changed" })
    taps.write(steps, c, tonumber(answer:sub(c, c)))
    taps.cell(steps, c, { move = false })
    taps.rail(steps, 2)
    taps.rail(steps, 2, { move = false })
    taps.cell(steps, c, { move = false })
    taps.solve(steps, state)
    steps[#steps].shows = "Time 0:05"
    return steps
  end,
  winners = { 1 },
}
