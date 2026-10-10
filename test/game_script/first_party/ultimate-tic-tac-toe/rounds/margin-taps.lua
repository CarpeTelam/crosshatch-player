-- Moves played by taps in the margin beside the grid's first and last columns (the 8 px between the grid and the box's
-- edge): a first-column move is tapped at x 4 of the box, a last-column move at x 460 (where the owner's finger landed
-- on the X4 Pro); the other moves are tapped on their cell. Then a tap in the corner of the margin (x 460, one pixel
-- above the grid, on no row), which changes nothing. No board is won.
local taps = require("taps")

-- { board, cell, margin }: the moves are legal in this order (each goes to the board the move before it played).
local moves = {
  { 3, 3, true }, { 3, 6, true }, { 6, 3, true }, { 3, 9, true }, { 9, 1 }, { 1, 1, true }, { 1, 4, true },
  { 4, 7, true }, { 7, 1, true },
}

local steps = {}
for i, move in ipairs(moves) do
  local seat = (i - 1) % 2 + 1
  steps[i] = move[3] and taps.margin_step(seat, move[1], move[2]) or taps.step(seat, move[1], move[2])
end
local x, y = taps.corner()
steps[#steps + 1] = { seat = 2, x = x, y = y, move = false }

return { mode = "pass", steps = steps, unfinished = true }
