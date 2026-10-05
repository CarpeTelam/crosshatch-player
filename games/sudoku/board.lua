-- board.lua: a 9 x 9 board of nine 3 x 3 blocks on the canvas, with no game rule in it. It lays the board out, maps
-- cells and blocks to canvas rectangles and a tap back to a cell, and draws the grid; the game draws what is in the
-- cells. Rows and columns are 1-based. Everything is pure except draw_grid, which needs draw's context.
local board = {}

-- The design box every layout is made in: 466 x 788, the X4 Pro's canvas. A larger canvas (the Sticky's 474 x 788)
-- centres the box, so the cells and tap targets are the same on every device; a smaller one is unsupported.
board.W, board.H = 466, 788

-- origin(w, h) -> ox, oy: where the box starts on a canvas of w x h pixels, centred and never negative.
function board.origin(w, h)
  return math.max(0, (w - board.W) // 2), math.max(0, (h - board.H) // 2)
end

-- fits(w, h): whether a canvas of w x h pixels holds the box.
function board.fits(w, h) return w >= board.W and h >= board.H end

-- layout(w, h, opts?) -> L = { x, y, cell, size, block, ox, oy } for a canvas of w x h pixels: the largest cell that
-- fits the box between opts.margin (default 4) at each side and opts.top (120) and opts.bottom (40) above and below,
-- centred in the box; the box sits at ox, oy on the canvas.
function board.layout(w, h, opts)
  opts = opts or {}
  local top = opts.top or 120
  local bottom = opts.bottom or 40
  local margin = opts.margin or 4
  local ox, oy = board.origin(w, h)
  local cell = math.min((board.W - 2 * margin) // 9, (board.H - top - bottom) // 9)
  return {
    x = ox + (board.W - 9 * cell) // 2, y = oy + top, cell = cell, size = 9 * cell, block = 3 * cell, ox = ox, oy = oy,
  }
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
