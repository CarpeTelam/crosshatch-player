-- Taps that change nothing: an occupied cell, the question button (the HOW TO PLAY page), the tap that closes the page,
-- and a tap off the grid; then a legal move is played. The moves come from tools/make_rounds.py: after them the next
-- move is in board 5, whose cell 5 is taken.
local taps = require("taps")

local moves = {
  {5, 5}, {5, 1}, {1, 5},
}

local steps = taps.steps(moves)
local function add(step) steps[#steps + 1] = step end

local x, y = taps.help()
add({ seat = 2, x = x, y = y, move = false, shows = "HOW TO PLAY" })
-- Any tap closes the page without a move, here the one a legal move would be (board 5, cell 2).
add(taps.step(2, 5, 2, { move = false }))
add(taps.step(2, 5, 5, { move = false, shows = "That cell is taken" }))
x, y = taps.off_grid()
add({ seat = 2, x = x, y = y, move = false })
add(taps.step(2, 5, 2))

return { mode = "pass", steps = steps, unfinished = true }
