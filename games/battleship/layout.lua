-- Battleship's geometry: where each board, button, and text block sits on a canvas of w x h pixels. Pure (no `ch`):
-- main.lua draws and reads taps through it, and the game's checks call it directly. Everything is laid out in one
-- fixed 466 x 788 box (the X4 Pro's canvas) centred in the canvas, so the Sticky's 474 x 788 gets the same cells and
-- targets 4 px to the right; a canvas smaller than the box is unsupported (the box then starts at 0, 0). The numbers
-- in the comments are the box's, before the box's offset.
local layout = {}

layout.W, layout.H = 466, 788

layout.BOARD = 10
layout.HEADER = 52 -- the title band; the question button sits in it
layout.GAP = 8
layout.BUTTONS = { "rotate", "random", "clear", "ready" }

-- A board is { x, y, cell }: its top-left corner and its cell's side. Cell (row, col) is the square at
-- x + (col - 1) * cell, y + (row - 1) * cell.
function layout.cell_rect(board, row, col)
  return board.x + (col - 1) * board.cell, board.y + (row - 1) * board.cell, board.cell, board.cell
end

-- The (row, col) of the cell holding the point, or nil outside the board.
function layout.cell_at(board, px, py)
  local side = layout.BOARD * board.cell
  if px < board.x or py < board.y or px >= board.x + side or py >= board.y + side then return nil end
  return (py - board.y) // board.cell + 1, (px - board.x) // board.cell + 1
end

-- Whether the point is inside the rectangle { x, y, w, h }.
function layout.inside(rect, px, py)
  return px >= rect.x and py >= rect.y and px < rect.x + rect.w and py < rect.y + rect.h
end

-- Whether a canvas of w x h pixels holds the box.
function layout.fits(w, h) return w >= layout.W and h >= layout.H end

function layout.compute(w, h)
  local header, gap = layout.HEADER, layout.GAP
  -- The box's top-left corner on the canvas: centred, and never off the canvas.
  local bx, by = math.max(0, (w - layout.W) // 2), math.max(0, (h - layout.H) // 2)
  -- The big board: placement's own fleet and firing's target, 44 px cells (440 px) with 280 px left below it.
  local cell = math.min(44, (layout.W - 8) // 10, (layout.H - header - gap - 280) // 10)
  local x = bx + (layout.W - layout.BOARD * cell) // 2
  local L = {
    w = w,
    h = h,
    ox = bx,
    oy = by,
    W = layout.W,
    question = { x = bx + layout.W - 48, y = by + 10, size = 32 },
    question_rect = { x = bx + layout.W - 72, y = by, w = 72, h = header },
    big = { x = x, y = by + header, cell = cell },
  }
  local below = by + header + layout.BOARD * cell + gap -- 500 below the box's top

  -- Firing: the small board (own fleet), and the column to its right (the last shot, the ships left, a message), which
  -- ends above Result's banner (about y 649).
  L.small = { x = x, y = below, cell = 28 }
  local columnX = x + layout.BOARD * 28 + 12
  L.column = { x = columnX, y = below, w = bx + layout.W - 8 - columnX, h = 140 }

  -- Placement: a status line, the ship tray, four buttons, and the message.
  L.status = { x = x, y = below }
  L.tray = { x = x, y = below + 36, square = 16, gap = 12 }
  local bw = (layout.BOARD * cell - 3 * gap) // 4
  L.buttons = {}
  for i, name in ipairs(layout.BUTTONS) do
    L.buttons[name] = { x = x + (i - 1) * (bw + gap), y = below + 76, w = bw, h = 84 }
  end
  L.message = { x = x, y = below + 172 }

  -- Seat 0 at Over: both fleets side by side above the end-of-round dialog (18 px cells from y 72, so they end at y 252,
  -- clear of the dialog's top edge at about 259), a label above each (small text at y 44, a 24 px box that ends 4 px
  -- above the boards), the shot counts below.
  local oc = math.min(18, (layout.W - 24) // 20)
  local total = 2 * layout.BOARD * oc + 14
  local fx = bx + (layout.W - total) // 2
  L.over = {
    { x = fx, y = by + 72, cell = oc },
    { x = fx + layout.BOARD * oc + 14, y = by + 72, cell = oc },
  }
  L.over_label_y = by + 44
  L.over_counts_y = by + 570
  return L
end

return layout
