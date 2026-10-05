-- Notes as dots (the default): NOTES on, marks in three cells, the focus on a digit, a mark toggled off again.
local taps = require("taps")
local rules = require("rules")
local grid = require("grid")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 21,
  steps = function(state)
    rules.taps(state)
    rules.frame(state, false)
    local steps = taps.rail({}, 1, { move = false })
    local cand, empties, first = grid.candidates(state.v), taps.empties(state), nil
    for i = 1, 3 do
      local c, n = empties[i], 0
      for d = 1, 9 do
        if cand[c] & (1 << d - 1) ~= 0 and n < 2 then
          n = n + 1
          first = first or { c, d }
          taps.note(steps, c, d)
        end
      end
    end
    taps.key(steps, first[2], { move = false })
    taps.note(steps, first[1], first[2])
    return steps
  end,
  unfinished = true,
}
