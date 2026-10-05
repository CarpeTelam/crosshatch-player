-- The MENU's three toggles, each shown in its row: SHOW REMAINING and SHADE PEERS off, NOTES AS digits, and back.
local taps = require("taps")
local rules = require("rules")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 30,
  steps = function(state)
    rules.store(state)
    rules.reset(state)
    local steps = taps.menu_row({}, 4, { move = false, shows = "SHOW REMAINING: OFF" })
    taps.menu(steps, 5, { move = false, shows = "SHADE PEERS: OFF" })
    taps.menu(steps, 6, { move = false, shows = "NOTES AS: DIGITS" })
    taps.menu(steps, 6, { move = false, shows = "NOTES AS: DOTS" })
    taps.menu(steps, 5, { move = false, shows = "SHADE PEERS: ON" })
    taps.menu(steps, 4, { move = false, shows = "SHOW REMAINING: ON" })
    taps.menu(steps, 8, { move = false, shows = "Sudoku - Easy" })
    return steps
  end,
  unfinished = true,
}
