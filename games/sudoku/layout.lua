-- layout.lua: where everything is on the canvas, from ch.screen and the board module. Pure geometry: input, draw, and
-- the game's companion checks and rounds all ask it, so a tap is computed from the same numbers the screen is drawn
-- with. Every target is at least 44 px on the 474 x 788 canvas.
--
-- The page, top to bottom: the header line, the 9 x 9 grid, then the pad (3 x 3 keys that abut) with the rail (NOTES,
-- ERASE, UNDO, MENU: four buttons that abut, together exactly as tall as the pad) to its right. The MENU panel
-- replaces all of that with its rows, and the HOW TO PLAY page with its text.
local board = require("board")

local layout = {}

local TOP = 56 -- the header line's height
local GAP = 8
local KEY_MAX = 80
layout.ROWS = 8 -- the MENU panel's rows
layout.RAIL = 4 -- the rail's buttons

-- The y offset from the middle of a line of text to the top of its box, per size, so a numeral is drawn centred (the
-- built-in fonts' ascent and digit height; the simulator frames settle them).
layout.DY = { small = 13, medium = 15, large = 27 }

local cache, cw, chh

-- The geometry for the canvas: board.layout's table (x, y, cell, size, block) with the rest added.
function layout.get()
  local w, h = ch.screen.w, ch.screen.h
  if cache and cw == w and chh == h then return cache end
  local L = board.layout(w, h, { top = TOP, bottom = 2 * GAP + 3 * 72 })
  L.pad_y = L.y + L.size + GAP
  L.kw = L.size * 2 // 9 -- a key's width, so the pad is two thirds of the grid
  L.kh = math.min(KEY_MAX, (h - L.pad_y - GAP) // 3) // 4 * 4
  L.rail_x = L.x + 3 * L.kw + GAP
  L.rail_w = L.x + L.size - L.rail_x
  L.bh = 3 * L.kh // 4 -- four buttons as tall as the three rows of keys
  L.row_h = math.min(80, (h - 72 - 16) // layout.ROWS)
  L.row_y, L.row_x, L.row_w = 72, 24, w - 48
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

-- The rectangle of MENU panel row i (1..8).
function layout.menu_rect(i)
  local L = layout.get()
  return L.row_x, L.row_y + (i - 1) * L.row_h, L.row_w, L.row_h
end

-- The MENU panel row (1..8) under x, y, or nil.
function layout.menu_at(x, y)
  local L = layout.get()
  local i = (y - L.row_y) // L.row_h + 1
  if x < L.row_x or x >= L.row_x + L.row_w or y < L.row_y or i > layout.ROWS then return nil end
  return i
end

-- The centre x, y of a rectangle.
function layout.centre(x, y, w, h) return x + w // 2, y + h // 2 end

-- The HOW TO PLAY page's text area: x, the first line's y, and the width in pixels.
function layout.help_area() return 24, 80, ch.screen.w - 48 end

return layout
