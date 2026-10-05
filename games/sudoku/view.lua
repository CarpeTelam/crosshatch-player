-- view.lua: everything Sudoku draws, from the state, the ui table, and layout.lua: the board (header, grid, pad, rail),
-- the MENU panel, the HOW TO PLAY page, and the end screen. The first frame, and each change between those four,
-- asks for a full refresh, as the panel replaces a dense board wholesale and a fast refresh would ghost.
local grid = require("grid")
local layout = require("layout")
local board = require("board")

local view = {}

local DIG = grid.DIG
local NAMES = { "Easy", "Medium", "Hard", "Expert" }
local MENU_ICONS = { "lightbulb", "plus", "check", "eye", "square", "pencil-simple", "question", "x" }
local RAIL = { { "pencil-simple", "NOTES" }, { "eraser", "ERASE" }, { "arrow-u-up-left", "UNDO" }, { "gear-six", "MENU" } }

-- m:ss of a time in milliseconds.
function view.time(ms)
  local s = ms // 1000
  return string.format("%d:%02d", s // 60, s % 60)
end

local function text_at(str, size, color, cx, cy)
  ch.gfx.text(cx, cy - layout.DY[size], str, size, color, "center")
end

-- Outlines of a rectangle, from inset a to inset b.
local function frame(x, y, w, h, color, a, b)
  for i = a, b do ch.gfx.rect(x + i, y + i, w - 2 * i, h - 2 * i, color) end
end

-- One stroke through a cell, in a white halo that reads over a numeral of either colour: rising (/) or falling (\).
local function stroke(x, y, w, h, rising)
  local x1, x2, y1, y2 = x + 6, x + w - 6, y + 6, y + h - 6
  if rising then y1, y2 = y2, y1 end
  for _, color in ipairs({ "white", "black" }) do
    for o = color == "white" and -2 or -1, color == "white" and 2 or 1 do
      ch.gfx.line(x1 + o, y1, x2 + o, y2, color)
    end
  end
end

local function draw_help()
  ch.gfx.text(24, 24, "HOW TO PLAY", "large", "black")
  for _, line in ipairs((require("help").lines())) do ch.gfx.text(24, line.y, line.text, "small", "black") end
end

local function draw_menu(ui)
  ch.gfx.text(24, 13, "MENU", "medium", "black")
  local labels = {
    "HINT", "FILL NOTES", "CHECK", "SHOW REMAINING: " .. (ui.rem and "ON" or "OFF"),
    "SHADE PEERS: " .. (ui.shade and "ON" or "OFF"), "NOTES AS: " .. (ui.dots and "DOTS" or "DIGITS"),
    "HOW TO PLAY", "CLOSE",
  }
  for i = 1, layout.ROWS do
    local x, y, w, h = layout.menu_rect(i)
    ch.gfx.rect(x, y, w + 1, h + 1, "black")
    ch.gfx.icon(MENU_ICONS[i], x + 12, y + (h - 32) // 2, "small", "black")
    ch.gfx.text(x + 60, y + h // 2 - layout.DY.medium, labels[i], "medium", "black")
  end
end

local function draw_end(state, ui)
  local cx = ch.screen.w // 2
  ch.gfx.text(cx, 80, "Solved", "large", "black", "center")
  ch.gfx.text(cx, 160, "Time " .. view.time(state.t), "medium", "black", "center")
  if state.h then
    ch.gfx.text(cx, 210, "No best time after a hint", "medium", "black", "center")
  elseif ui.best then
    ch.gfx.text(cx, 210, "Best " .. view.time(ui.best), "medium", "black", "center")
  end
  ch.gfx.text(cx, 260, NAMES[state.l], "small", "black", "center")
end

-- A pencil mark of digit d in the slot of a cell: a dot (grey ring, or solid black for the focused digit) or the
-- digit's own image (grey, or black for the focused digit), at the slot's centre. An image has a one-pixel paper
-- margin and a slot is 17 px in a 51 px cell, so the top and bottom rows move 1 px inward to keep the paper off the
-- 3 px block lines.
local function draw_mark(ui, d, cx, cy, focused, paper, row)
  if ui.dots then
    if focused then
      ch.gfx.circle(cx, cy, 5, "black", true)
    else
      if paper then ch.gfx.circle(cx, cy, 6, "white", true) end
      ch.gfx.circle(cx, cy, 5, "dark", true)
      ch.gfx.circle(cx, cy, 2, "white", true)
    end
  else
    ch.gfx.image((focused and "note_b" or "note_g") .. d, cx - 5, cy - 6 - row, "black")
  end
end

local function draw_board(state, ui)
  local L, v = layout.get(), state.v
  local sel, foc = ui.sel, ui.foc
  local ds, clash, cand = { v:byte(1, 81) }, grid.clashes(v), grid.candidates(v)
  local notes = { string.unpack(grid.FMT, state.n) }
  local ground = {}
  ch.gfx.text(L.x, 13, ui.note or ui.msg or ("Sudoku - " .. NAMES[state.l]), "medium", "black")

  -- Grounds: a clue dark, the focused digit's copies black, the selected cell's units light.
  for c = 1, 81 do
    local a, b, e = grid.units(c)
    local g
    if foc and DIG[ds[c]] == foc then
      g = "black"
    elseif ds[c] >= 49 and ds[c] <= 57 then
      g = "dark"
    elseif ui.shade and sel then
      local sa, sb, se = grid.units(sel)
      if a == sa or b == sb or e == se then g = "light" end
    end
    if g then
      local x, y, w, h = layout.cell_rect(c)
      ch.gfx.rect(x + 1, y + 1, w - 1, h - 1, g, true)
    end
    ground[c] = g
  end
  board.draw_grid(L)

  for c = 1, 81 do
    local x, y, w, h = layout.cell_rect(c)
    local d, g = DIG[ds[c]], ground[c]
    local cx, cy = layout.centre(x, y, w, h)
    if d then
      text_at(string.char(48 + d), "large", (g == "dark" or g == "black") and "white" or "black", cx, cy)
    else
      -- Marks a neighbour's digit rules out are hidden, never erased.
      local visible, slot = notes[c] & cand[c], w // 3
      if visible ~= 0 then
        local ox, oy = x + (w - 3 * slot) // 2 + slot // 2, y + (h - 3 * slot) // 2 + slot // 2
        for k = 1, 9 do
          if visible & (1 << k - 1) ~= 0 then
            draw_mark(ui, k, ox + (k - 1) % 3 * slot, oy + (k - 1) // 3 * slot, k == foc, g ~= nil, (k - 1) // 3)
          end
        end
      end
    end
    if clash and clash[c] then stroke(x, y, w, h, true) end
    if ui.check and ui.check[c] then stroke(x, y, w, h, false) end
  end

  if sel then
    local x, y, w, h = layout.cell_rect(sel)
    if ground[sel] == "dark" or ground[sel] == "black" then
      frame(x, y, w, h, "white", 2, 4)
      frame(x, y, w, h, "black", 0, 1)
    else
      frame(x, y, w, h, "black", 1, 2)
      frame(x, y, w, h, "white", 3, 3)
    end
  end

  -- The pad: nine keys that abut, each with how many are still to place when SHOW REMAINING is on.
  local left = { 9, 9, 9, 9, 9, 9, 9, 9, 9 }
  for c = 1, 81 do
    local d = DIG[ds[c]]
    if d then left[d] = left[d] - 1 end
  end
  for d = 1, 9 do
    local x, y, w, h = layout.key_rect(d)
    ch.gfx.rect(x, y, w + 1, h + 1, "black")
    text_at(string.char(48 + d), "large", "black", x + w // 2, y + h // 2)
    if ui.rem then ch.gfx.text(x + w - 8, y + 4, tostring(math.max(left[d], 0)), "small", "black", "right") end
    if foc == d then frame(x, y, w + 1, h + 1, "black", 1, 3) end
  end

  -- The rail: NOTES is inverted while it is on.
  for i = 1, layout.RAIL do
    local x, y, w, h = layout.rail_rect(i)
    local on = i == 1 and ui.pencil
    ch.gfx.rect(x, y, w + 1, h + 1, "black", on and true or false)
    local color = on and "white" or "black"
    ch.gfx.icon(RAIL[i][1], x + 10, y + (h - 32) // 2, "small", color)
    ch.gfx.text(x + 54, y + h // 2 - layout.DY.small, RAIL[i][2], "small", color)
  end
end

-- Draws the frame for state and ui; over is whether the grid is solved.
function view.draw(state, ui, over)
  ch.gfx.clear("white")
  local shown = over and "end" or ui.panel or "board"
  if ui.shown ~= shown then
    ui.shown = shown
    ch.gfx.refresh("full")
  end
  if over then
    draw_end(state, ui)
  elseif ui.panel == "help" then
    draw_help()
  elseif ui.panel == "menu" then
    draw_menu(ui)
  else
    draw_board(state, ui)
  end
end

return view
