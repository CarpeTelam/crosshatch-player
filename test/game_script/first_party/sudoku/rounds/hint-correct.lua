-- HINT on a grid with no wrong digit names the rule the ladder needs for the next cell, with the unit of an only-cell hint, and the solve that follows sets
-- no best time.
local taps = require("taps")
local solver = require("solver")

local UNITS = { "row", "column", "box" }

return {
  mode = "solo",
  settings = { level = "Medium" },
  seed = 28,
  steps = function(state)
    local cell, _, rung, unit = solver.hint(state.v)
    assert(unit, "this seed's grid has no hidden-single hint, so no unit to show")
    local steps = taps.menu_row({}, 1, { shows = solver.name(rung) .. " (" .. UNITS[unit // 9 + 1] .. ")" })
    taps.cell(steps, cell, { move = false }) -- HINT selected it: this tap lets go, and the solve starts clean
    taps.solve(steps, state, { wait = 5000 })
    steps[#steps].shows = "No best time after a hint"
    return steps
  end,
  winners = { 1 },
}
