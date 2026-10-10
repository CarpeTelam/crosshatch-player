-- The question button opens the HOW TO PLAY page and the next tap closes it, in placement and in firing; neither tap
-- is a move, and play goes on after each. The round does not finish.
local taps = require("taps")

local SHIPS = { { 1, 1, "H" }, { 2, 1, "H" }, { 3, 1, "H" }, { 4, 1, "H" }, { 5, 1, "H" } }
local ox, oy = taps.off_target()
local hx, hy = taps.help()

local steps = {}
steps[#steps + 1] = taps.at(1, hx, hy, { move = false, shows = "HOW TO PLAY" })
steps[#steps + 1] = taps.at(1, ox, oy, { move = false, shows = "Player 1: place ships" })
taps.append(steps, (taps.place(1, SHIPS, false)))
steps[#steps + 1] = taps.press(1, "ready")
taps.append(steps, (taps.place(2, SHIPS, false)))
steps[#steps + 1] = taps.press(2, "ready")
-- Firing: seat 1 reads the page, closes it, and fires; seat 2 reads it again.
steps[#steps + 1] = taps.at(1, hx, hy, { move = false, shows = "HOW TO PLAY" })
steps[#steps + 1] = taps.at(1, ox, oy, { move = false, shows = "Player 1: fire!" })
steps[#steps + 1] = taps.shoot(1, 1, 1, { shows = "Hit!" })
steps[#steps + 1] = taps.at(2, hx, hy, { move = false, shows = "HOW TO PLAY" })
steps[#steps + 1] = taps.at(2, ox, oy, { move = false, shows = "Player 2: fire!" })
steps[#steps + 1] = taps.shoot(2, 10, 10, { shows = "Miss" })

return { mode = "pass", steps = steps, unfinished = true }
