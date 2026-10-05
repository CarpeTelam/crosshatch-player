-- Moves the game rejects, each a tap that changes nothing and says why: a ship over another, Ready before five ships,
-- a ship after five are placed, Clear with nothing placed, and in firing a square shot twice.
local taps = require("taps")

local steps = {}
-- Seat 1: the Carrier, then the Battleship over it.
taps.append(steps, (taps.place(1, { { 1, 1, "H" } }, false)))
steps[#steps + 1] = taps.shoot(1, 1, 3, { move = false, shows = "Ships cannot overlap" })
steps[#steps + 1] = taps.press(1, "ready", { move = false, shows = "Place all five ships first" })
taps.append(steps, (taps.place(1, { { 2, 1, "H" }, { 3, 1, "H" }, { 4, 1, "H" }, { 5, 1, "H" } }, false)))
steps[#steps + 1] = taps.shoot(1, 7, 7, { move = false, shows = "All five ships are placed" })
steps[#steps + 1] = taps.press(1, "ready")
-- Seat 2: Clear with nothing placed.
steps[#steps + 1] = taps.press(2, "clear", { move = false, shows = "No ships to clear" })
taps.append(steps, (taps.place(2, { { 6, 1, "H" }, { 7, 1, "H" }, { 8, 1, "H" }, { 9, 1, "H" }, { 10, 1, "H" } }, false)))
steps[#steps + 1] = taps.press(2, "ready")
-- Firing: seat 1 misses at (5, 5), seat 2 hits at (1, 1), and seat 1 fires at (5, 5) again.
steps[#steps + 1] = taps.shoot(1, 5, 5, { shows = "Miss" })
steps[#steps + 1] = taps.shoot(2, 1, 1, { shows = "Hit!" })
steps[#steps + 1] = taps.shoot(1, 5, 5, { move = false, shows = "You already fired" })

return { mode = "pass", steps = steps, unfinished = true }
