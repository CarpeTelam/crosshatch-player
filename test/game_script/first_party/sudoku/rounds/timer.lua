-- TIMER: the play time in the header, whole minutes, and its toggle in the MENU. The pins (interaction.timer) read the
-- header, the timer calls and the MENU's rows through a ch.timer double, and they pin firing and re-arming; a round
-- delivers no timer event. This round pins the header text at a moved clock and the TIMER row on the real draw path:
-- the clock moves 61 seconds, the tap's own draw shows "1 min", and TIMER goes off and on from the MENU's row 7.
local taps = require("taps")
local interaction = require("interaction")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 32,
  steps = function(state)
    interaction.timer(state)
    local steps = taps.cell({}, taps.empties(state)[1], { wait = 61000, move = false, shows = "1 min" })
    taps.menu_row(steps, 7, { move = false, shows = "TIMER: OFF" })
    taps.menu(steps, 7, { move = false, shows = "TIMER: ON" })
    taps.menu(steps, 9, { move = false, shows = "Sudoku - Easy" })
    return steps
  end,
  unfinished = true,
}
