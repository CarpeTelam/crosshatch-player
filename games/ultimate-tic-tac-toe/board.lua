-- board.lua: a 9 x 9 board of nine 3 x 3 blocks on the canvas, with no game rule in it. It lays the board out, maps
-- cells and blocks to canvas rectangles and a tap back to a cell, and draws the grid; the game draws what is in the
-- cells. Rows and columns are 1-based. Everything is pure except draw_grid, which needs draw's context.
local board = {}

-- layout(w, h, opts?) -> L = { x, y, cell, size, block } for a canvas of w x h pixels: the largest cell that fits
-- between opts.margin (default 4) at each side and opts.top (120) and opts.bottom (40) above and below, centred.
function board.layout(w, h, opts)
  opts = opts or {}
  local top = opts.top or 120
  local bottom = opts.bottom or 40
  local margin = opts.margin or 4
  local cell = math.min((w - 2 * margin) // 9, (h - top - bottom) // 9)
  return { x = (w - 9 * cell) // 2, y = top, cell = cell, size = 9 * cell, block = 3 * cell }
end

-- The canvas rectangle x, y, w, h of the cell at row, col.
function board.cell_rect(L, row, col)
  return L.x + (col - 1) * L.cell, L.y + (row - 1) * L.cell, L.cell, L.cell
end

-- The canvas rectangle x, y, w, h of the 3 x 3 block at brow, bcol.
function board.block_rect(L, brow, bcol)
  return L.x + (bcol - 1) * L.block, L.y + (brow - 1) * L.block, L.block, L.block
end

-- The row, col of the cell under canvas point x, y, or nil outside the grid.
function board.cell_at(L, x, y)
  if L.cell < 1 or x < L.x or y < L.y or x >= L.x + L.size or y >= L.y + L.size then return nil end
  return (y - L.y) // L.cell + 1, (x - L.x) // L.cell + 1
end

-- 1 px lines on every cell edge, then 3 px black lines on every block edge and the border: 28 commands.
function board.draw_grid(L)
  for i = 0, 9 do
    local e = i * L.cell
    ch.gfx.line(L.x + e, L.y, L.x + e, L.y + L.size, "black")
    ch.gfx.line(L.x, L.y + e, L.x + L.size, L.y + e, "black")
  end
  for i = 0, 3 do
    local e = i * L.block
    ch.gfx.rect(L.x + e - 1, L.y - 1, 3, L.size + 3, "black", true)
    ch.gfx.rect(L.x - 1, L.y + e - 1, L.size + 3, 3, "black", true)
  end
end

return board
