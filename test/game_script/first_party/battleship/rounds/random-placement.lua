-- Random places the ships not placed yet and keeps the ones already placed: seat 1 places two ships and taps Random,
-- seat 2 taps Random on an empty board, each taps Ready, and seat 1 fires one shot. The round does not finish.
local taps = require("taps")

local steps = taps.place(1, { { 1, 1, "H" }, { 3, 1, "H" } }, false)
steps[#steps + 1] = taps.press(1, "random", { shows = "All ships placed" })
steps[#steps + 1] = taps.press(1, "ready", { shows = "Player 1: fleet ready" })
steps[#steps + 1] = taps.press(2, "random", { shows = "All ships placed" })
steps[#steps + 1] = taps.press(2, "ready")
steps[#steps + 1] = taps.shoot(1, 5, 5)

return { mode = "pass", steps = steps, unfinished = true }
