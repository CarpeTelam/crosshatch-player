-- Battleship's geometry: where each board, button, and text block sits on a canvas of w x h pixels. Pure (no `ch`):
-- main.lua draws and reads taps through it, and the game's checks call it directly. The numbers in the comments are
-- the 474 x 788 device canvas's.
local layout = {}

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

function layout.compute(w, h)
  local header, gap = layout.HEADER, layout.GAP
  -- The big board: placement's own fleet and firing's target, 44 px cells (440 px) with 280 px left below it.
  local cell = math.min(44, (w - 8) // 10, (h - header - gap - 280) // 10)
  local x = (w - layout.BOARD * cell) // 2
  local L = {
    w = w,
    h = h,
    question = { x = w - 48, y = 10, size = 32 },
    question_rect = { x = w - 72, y = 0, w = 72, h = header },
    big = { x = x, y = header, cell = cell },
  }
  local below = header + layout.BOARD * cell + gap -- 500

  -- Firing: the small board (own fleet), and the column to its right (the last shot, the ships left, a message), which
  -- ends above Result's banner (about y 649).
  L.small = { x = x, y = below, cell = 28 }
  local columnX = x + layout.BOARD * 28 + 12
  L.column = { x = columnX, y = below, w = w - 8 - columnX, h = 140 }

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
  local oc = math.min(18, (w - 24) // 20)
  local total = 2 * layout.BOARD * oc + 14
  local ox = (w - total) // 2
  L.over = {
    { x = ox, y = 72, cell = oc },
    { x = ox + layout.BOARD * oc + 14, y = 72, cell = oc },
  }
  L.over_label_y = 44
  L.over_counts_y = 570
  return L
end

return layout
