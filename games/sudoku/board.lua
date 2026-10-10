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

-- snap(L, x, y, bx, by, bw, bh, tw, th) -> x, y: a tap in the margin between a block of targets and the edge of the
-- box, on the same rows (left and right margins) or columns (top and bottom margins) as the block, moved onto the
-- block's nearest edge pixel so the exact hit-test finds the edge target; any other point comes back unchanged. The
-- block is bx, by, bw, bh on the canvas and its targets tw x th; a gap counts as a margin only when it is smaller than
-- a target's own size along its axis (a tw wide gap at the sides, a th tall one above and below). The box is the
-- 466 x 788 one at L.ox, L.oy, so the Sticky's 4 px outside it never snap. The block's own points are unchanged.
function board.snap(L, x, y, bx, by, bw, bh, tw, th)
  local left, top = L.ox, L.oy
  local right, bottom = left + board.W, top + board.H
  if y >= by and y < by + bh then
    if x >= left and x < bx and bx - left < tw then return bx, y end
    if x >= bx + bw and x < right and right - (bx + bw) < tw then return bx + bw - 1, y end
  elseif x >= bx and x < bx + bw then
    if y >= top and y < by and by - top < th then return x, by end
    if y >= by + bh and y < bottom and bottom - (by + bh) < th then return x, by + bh - 1 end
  end
  return x, y
end

-- The row, col of the cell a tap at canvas point x, y means: the cell under it, or the edge cell its margin leads to
-- (snap), or nil. cell_at stays exact.
function board.tap_cell(L, x, y)
  return board.cell_at(L, board.snap(L, x, y, L.x, L.y, L.size, L.size, L.cell, L.cell))
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
