-- Clear lifts every ship and the seat places again from the first: seat 1 places two ships down, taps Clear (the
-- Carrier is asked for again), places five, and taps Ready; seat 2 places its first ship and the prompt moves on.
-- The round does not finish.
local taps = require("taps")

local steps, vertical = taps.place(1, { { 1, 1, "V" }, { 1, 3, "V" } }, false)
steps[#steps + 1] = taps.press(1, "clear", { shows = "Place the Carrier (5)" })
-- The direction stays what the seat last chose, so the new ships start down.
local again = taps.place(1, { { 1, 1, "V" }, { 1, 2, "V" }, { 1, 3, "V" }, { 1, 4, "V" }, { 1, 5, "V" } }, vertical)
taps.append(steps, again)
steps[#steps + 1] = taps.press(1, "ready")
steps[#steps + 1] = taps.shoot(2, 10, 1, { shows = "Place the Battleship (4)" })

return { mode = "pass", steps = steps, unfinished = true }
