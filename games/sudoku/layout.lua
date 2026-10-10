-- layout.lua: where everything is on the canvas, from ch.screen and the board module. Pure geometry: input, draw, and
-- the game's companion checks and rounds all ask it, so a tap is computed from the same numbers the screen is drawn
-- with. Everything is laid out in one fixed 466 x 788 box (board.W x board.H, the X4 Pro's canvas) centred in the
-- canvas, so the Sticky's 474 x 788 has the same cells and targets 4 px to the right; a canvas smaller than the box is
-- unsupported (the box then starts at 0, 0). Every target is at least 44 px.
--
-- The page, top to bottom: the header line, the 9 x 9 grid, then the pad (3 x 3 keys that abut) with the rail (NOTES,
-- ERASE, UNDO, MENU: four buttons that abut, together exactly as tall as the pad) to its right. The MENU panel
-- replaces all of that with its rows, and the HOW TO PLAY page with its text.
local board = require("board")

local layout = {}

local TOP = 56 -- the header line's height
local GAP = 8
local KEY_MAX = 80
layout.ROWS = 9 -- the MENU panel's rows
layout.RAIL = 4 -- the rail's buttons
layout.NOTE_W, layout.NOTE_H = 12, 16 -- a digit note's image (make_note_images.py checks them against its own size)
local NOTE_PITCH = 16 -- the distance from one note image to the next across (down, note_tile gives it)

-- The y offset from the middle of a line of text to the top of its box, per size, so a numeral is drawn centred (the
-- built-in fonts' ascent and digit height; the simulator frames settle them).
layout.DY = { small = 13, medium = 15, large = 27 }

local cache, cw, chh

-- The geometry for the canvas: board.layout's table (x, y, cell, size, block, ox, oy) with the rest added.
function layout.get()
  local w, h = ch.screen.w, ch.screen.h
  if cache and cw == w and chh == h then return cache end
  local L = board.layout(w, h, { top = TOP, bottom = 2 * GAP + 3 * 72 })
  L.pad_y = L.y + L.size + GAP
  L.kw = L.size * 2 // 9 -- a key's width, so the pad is two thirds of the grid
  L.kh = math.min(KEY_MAX, (board.H - (L.pad_y - L.oy) - GAP) // 3) // 4 * 4
  L.rail_x = L.x + 3 * L.kw + GAP
  L.rail_w = L.x + L.size - L.rail_x
  L.bh = 3 * L.kh // 4 -- four buttons as tall as the three rows of keys
  L.row_h = math.min(80, (board.H - 72 - 16) // layout.ROWS)
  L.row_y, L.row_x, L.row_w = L.oy + 72, L.ox + 24, board.W - 48
  cache, cw, chh = L, w, h
  return L
end

-- The canvas rectangle x, y, w, h of cell c (1..81, row by row).
function layout.cell_rect(c)
  return board.cell_rect(layout.get(), (c - 1) // 9 + 1, (c - 1) % 9 + 1)
end

-- The cell (1..81) under canvas point x, y, or nil.
function layout.cell_at(x, y)
  local row, col = board.cell_at(layout.get(), x, y)
  if not row then return nil end
  return (row - 1) * 9 + col
end

-- The rectangle of pad key d (1..9, 1 top left, 9 bottom right).
function layout.key_rect(d)
  local L = layout.get()
  return L.x + (d - 1) % 3 * L.kw, L.pad_y + (d - 1) // 3 * L.kh, L.kw, L.kh
end

-- The pad key (1..9) under x, y, or nil: the inverse of key_rect, over the pad's whole rectangle.
function layout.key_at(x, y)
  local L = layout.get()
  local col, row = (x - L.x) // L.kw, (y - L.pad_y) // L.kh
  if x < L.x or y < L.pad_y or col > 2 or row > 2 then return nil end
  return row * 3 + col + 1
end

-- The rectangle of rail button i (1 NOTES, 2 ERASE, 3 UNDO, 4 MENU).
function layout.rail_rect(i)
  local L = layout.get()
  return L.rail_x, L.pad_y + (i - 1) * L.bh, L.rail_w, L.bh
end

-- The rail button (1..4) under x, y, or nil.
function layout.rail_at(x, y)
  local L = layout.get()
  local i = (y - L.pad_y) // L.bh + 1
  if x < L.rail_x or x >= L.rail_x + L.rail_w or y < L.pad_y or i > layout.RAIL then return nil end
  return i
end

-- The three snaps below move a tap in the margin between a block and the box's edge onto that block's edge
-- (board.snap: the same rows or columns as the block, a gap smaller than the target's own size), so a tap a few pixels
-- off the grid, the pad or the rail's side counts as the edge cell, key or button. The exact functions above stay
-- exact; the MENU panel and the HOW TO PLAY page have no snap. Each takes x, y and returns x, y.

-- Over the grid (cell-sized targets): the 8 px at its sides; its top (56 px) and the gap to the pad do not snap.
function layout.snap_cell(x, y)
  local L = layout.get()
  return board.snap(L, x, y, L.x, L.y, L.size, L.size, L.cell, L.cell)
end

-- Over the pad (kw x kh keys): the 8 px at its left and the 34 px under it.
function layout.snap_key(x, y)
  local L = layout.get()
  return board.snap(L, x, y, L.x, L.pad_y, 3 * L.kw, 3 * L.kh, L.kw, L.kh)
end

-- Over the rail (rail_w x bh buttons): the 8 px at its right and the 34 px under it.
function layout.snap_rail(x, y)
  local L = layout.get()
  return board.snap(L, x, y, L.rail_x, L.pad_y, L.rail_w, layout.RAIL * L.bh, L.rail_w, L.bh)
end

-- The rectangle of MENU panel row i (1..9).
function layout.menu_rect(i)
  local L = layout.get()
  return L.row_x, L.row_y + (i - 1) * L.row_h, L.row_w, L.row_h
end

-- The MENU panel row (1..9) under x, y, or nil.
function layout.menu_at(x, y)
  local L = layout.get()
  local i = (y - L.row_y) // L.row_h + 1
  if x < L.row_x or x >= L.row_x + L.row_w or y < L.row_y or i > layout.ROWS then return nil end
  return i
end

-- The canvas point X0, Y0 of the note image for mark k (1..9) of the cell whose rectangle starts at x, y: three columns
-- and three rows of 12 x 16 images inside the cell's grid lines (the 1 px cell line is offset 0 and a 3 px block line
-- covers offsets 0..1 and cell - 1, so the images lie in offsets 2..cell - 2). The cells are 50 px on every canvas,
-- with 47 rows, so the rows go at a 15 px pitch and each image's one pixel margin overlaps the next (the margins
-- agree: white for G and B, the ground's own checker for H). The image's checker is baked in, so its origin's x + y
-- must be even (odd for an `inverted` one, drawn "white", which swaps the checker) to continue the screen's "dark"
-- dither, which is black where screen x + y is even; the canvas origin's x + y is even on both boards (3 + 9, 7 + 9),
-- and the box's offset on the Sticky's 474 x 788 is 4 in x, so the box's phase holds there. The one pixel nudge `a`
-- keeps that true from cell to cell and from row to row.
function layout.note_tile(x, y, k, inverted)
  local row = (k - 1) // 3
  local pitch_y = (layout.get().cell - 3 - layout.NOTE_H) // 2
  local a = (x + y + pitch_y * row + (inverted and 1 or 0)) % 2
  return x + 4 + NOTE_PITCH * ((k - 1) % 3) + a, y + 2 + pitch_y * row
end

-- The centre x, y of a rectangle.
function layout.centre(x, y, w, h) return x + w // 2, y + h // 2 end

-- The HOW TO PLAY page's text area: x, the first line's y, and the width in pixels, in the box.
function layout.help_area()
  local L = layout.get()
  return L.ox + 24, L.oy + 80, board.W - 48
end

return layout
