-- The ring at its fullest: a fill, then 90 note edits (the ring holds 48), so the fill's copy is in the snapshot until
-- the fill is the step that drops out, and a ring that did not drop its oldest would pass 700 B. The games check
-- measures every snapshot against 700 B.
local taps = require("taps")
local rules = require("rules")
local grid = require("grid")

return {
  mode = "solo",
  settings = { level = "Medium" },
  seed = 26,
  steps = function(state)
    rules.ring(state)
    local steps = taps.menu_row({}, 2)
    taps.rail(steps, 1, { move = false })
    local c, d = taps.empties(state)[1], nil
    for k = 1, 9 do
      if d == nil and grid.candidates(state.v)[c] & (1 << k - 1) ~= 0 then d = k end
    end
    for _ = 1, 90 do taps.note(steps, c, d) end
    return steps
  end,
  unfinished = true,
}
