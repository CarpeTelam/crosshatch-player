-- view.lua: everything Battleship draws, from the state, the ui table, and layout.lua: the HOW TO PLAY page, a seat's
-- header, its boards, tray, buttons, and lines, and seat 0's two fleets at Over. main.lua passes itself in as `game`
-- for the views and text lines the checks pin; this module never requires main.
local fleet = require("fleet")
local layout = require("layout")

local view = {}

local ICONS = { rotate = "arrow-clockwise", random = "shuffle", clear = "trash", ready = "check" }
local LABELS = { rotate = "Rotate", random = "Random", clear = "Clear", ready = "Ready" }

-- Words of `text` in lines no wider than `maxw` pixels at `size`.
local function wrap(text, size, maxw)
  local lines, line = {}, ""
  for word in text:gmatch("%S+") do
    local try = line == "" and word or line .. " " .. word
    if line ~= "" and ch.text_width(try, size) > maxw then
      lines[#lines + 1] = line
      line = word
    else
      line = try
    end
  end
  if line ~= "" then lines[#lines + 1] = line end
  return lines
end
view.wrap = wrap

local function drawHelp(game, L)
  ch.gfx.text(L.ox + 24, L.oy + 24, "HOW TO PLAY", "large", "black")
  for _, line in ipairs(game.help_lines()) do ch.gfx.text(L.ox + 24, line.y, line.text, "small", "black") end
end

local function drawHeader(game, L, state, seat)
  ch.gfx.text(L.big.x, L.oy + 12, game.headline(state, seat), "medium", "black")
  ch.gfx.icon("question", L.question.x, L.question.y, "small", "black")
end

local function drawGrid(B)
  local side = layout.BOARD * B.cell
  for i = 0, layout.BOARD do
    ch.gfx.line(B.x, B.y + i * B.cell, B.x + side, B.y + i * B.cell, "black")
    ch.gfx.line(B.x + i * B.cell, B.y, B.x + i * B.cell, B.y + side, "black")
  end
end

-- A frame `thick` px wide inside a board's cell.
local function outline(B, row, col, thick)
  local x, y, w, h = layout.cell_rect(B, row, col)
  for i = 0, thick - 1 do ch.gfx.rect(x + i, y + i, w - 2 * i, h - 2 * i, "black") end
end

-- A fleet: each ship a black bar (stretched across the inset to a neighbour of the same ship, so touching ships stay
-- apart), a hit crossed in white, a miss a ring.
local function drawFleet(B, f)
  drawGrid(B)
  local cell = B.cell
  local inset = math.max(1, cell // 12)
  for row = 1, layout.BOARD do
    for col = 1, layout.BOARD do
      local at = (row - 1) * layout.BOARD + col
      local char = f:sub(at, at)
      local x, y = layout.cell_rect(B, row, col)
      if char == "o" then
        ch.gfx.circle(x + cell // 2, y + cell // 2, cell // 8, "black")
      elseif char ~= "." then
        local key = char:lower()
        local w, h = cell - 2 * inset, cell - 2 * inset
        if col < layout.BOARD and f:sub(at + 1, at + 1):lower() == key then w = w + 2 * inset end
        if row < layout.BOARD and f:sub(at + layout.BOARD, at + layout.BOARD):lower() == key then h = h + 2 * inset end
        ch.gfx.rect(x + inset, y + inset, w, h, "black", true)
        if char ~= key then
          local m = inset + math.max(1, cell // 8)
          for dx = 0, 1 do
            ch.gfx.line(x + m + dx, y + m, x + cell - 1 - m + dx, y + cell - 1 - m, "white")
            ch.gfx.line(x + cell - 1 - m + dx, y + m, x + m + dx, y + cell - 1 - m, "white")
          end
        end
      end
    end
  end
end

-- The target board as `fleet.mask` gives it: a wave for a miss, a flame for a hit, a solid flame for a sunk ship.
local function drawTarget(B, mask)
  drawGrid(B)
  for row = 1, layout.BOARD do
    for col = 1, layout.BOARD do
      local at = (row - 1) * layout.BOARD + col
      local char = mask:sub(at, at)
      if char ~= "." then
        local x, y = layout.cell_rect(B, row, col)
        if char == "o" then
          ch.gfx.icon("waves", x + 6, y + 6, "small", "black")
        elseif char == "x" then
          ch.gfx.icon("fire", x + 6, y + 6, "small", "black")
        else
          ch.gfx.icon("fire", x + 6, y + 6, "small", "black", "fill")
        end
      end
    end
  end
end

local function drawMessage(ui, x, y, maxw)
  if not ui.message then return end
  for i, line in ipairs(wrap(ui.message, "small", maxw)) do ch.gfx.text(x, y + (i - 1) * 24, line, "small", "black") end
end

local function drawTray(L, own)
  local index = fleet.next_ship(own)
  local x = L.tray.x
  local side = L.tray.square
  for i, ship in ipairs(fleet.SHIPS) do
    local w = ship.len * side
    if own:find(ship.key, 1, true) then
      ch.gfx.rect(x, L.tray.y, w, side, "black", true)
    else
      ch.gfx.rect(x, L.tray.y, w, side, "black")
      if i == index then ch.gfx.rect(x + 1, L.tray.y + 1, w - 2, side - 2, "black") end
    end
    x = x + w + L.tray.gap
  end
end

local function drawButtons(L, own)
  local ready = fleet.next_ship(own) == nil
  for _, name in ipairs(layout.BUTTONS) do
    local r = L.buttons[name]
    local inverted = name == "ready" and ready
    local ink = inverted and "white" or "black"
    ch.gfx.rect(r.x, r.y, r.w, r.h, "black", inverted)
    ch.gfx.rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, "black", inverted)
    ch.gfx.icon(ICONS[name], r.x + (r.w - 32) // 2, r.y + 12, "small", ink)
    ch.gfx.text(r.x + r.w // 2, r.y + 50, LABELS[name], "small", ink, "center")
  end
end

local function drawPlacing(game, L, state, seat, ui)
  local seen = game.views(state, seat)
  drawFleet(L.big, seen.own)
  ch.gfx.text(L.status.x, L.status.y, game.prompt(seen.own, ui.vertical), "medium", "black")
  drawTray(L, seen.own)
  drawButtons(L, seen.own)
  drawMessage(ui, L.message.x, L.message.y, layout.BOARD * L.big.cell)
end

-- Placed, and the other seat is placing: the fleet and nothing to tap.
local function drawWaiting(game, L, state, seat, ui)
  local seen = game.views(state, seat)
  drawFleet(L.big, seen.own)
  local line = seat < state.p and "Your fleet is ready" or "Waiting for Player " .. state.p
  ch.gfx.text(L.status.x, L.status.y, line, "medium", "black")
  drawTray(L, seen.own)
  drawMessage(ui, L.message.x, L.message.y, layout.BOARD * L.big.cell)
end

-- Firing, and the boards of a finished round: the target large, the own fleet small, and the column beside the small
-- board.
local function drawFiring(game, L, state, seat, ui)
  local seen = game.views(state, seat)
  drawTarget(L.big, seen.target)
  drawFleet(L.small, seen.own)
  local l = state.l
  if l then
    if l[1] == seat then outline(L.big, l[2], l[3], 3) else outline(L.small, l[2], l[3], 2) end
  end
  local col = L.column
  local y = col.y
  for _, line in ipairs(wrap(game.last_shot_line(state, seat), "medium", col.w)) do
    ch.gfx.text(col.x, y, line, "medium", "black")
    y = y + 28
  end
  y = y + 4
  ch.gfx.icon("boat", col.x, y, "small", "black")
  ch.gfx.text(col.x + 36, y, "Their ships: " .. seen.left, "small", "black")
  ch.gfx.text(col.x + 36, y + 24, "Your ships: " .. fleet.ships_left(seen.own), "small", "black")
  drawMessage(ui, col.x, y + 56, col.w)
end

-- Seat 0 at Over: both fleets and each player's shot count.
local function drawBoth(game, L, state)
  local seen = game.views(state, 0)
  for i = 1, 2 do
    ch.gfx.text(L.over[i].x, L.over_label_y, "Player " .. i, "small", "black")
    drawFleet(L.over[i], seen.fleets[i])
    ch.gfx.text(L.over[1].x, L.over_counts_y + (i - 1) * 28, game.count_line(state, i), "small", "black")
  end
end

-- A frame: `game` is main's table, L the canvas's layout, and m the seat's layout mode (main's `mode`).
function view.draw(game, L, state, seat, ui, m)
  ch.gfx.clear("white")
  if ui.help then
    drawHelp(game, L)
    return
  end
  local over = game.status(state).over
  if seat == 0 then
    ch.gfx.text(L.big.x, L.oy + 12, game.headline(state, 0), "medium", "black")
    if over then drawBoth(game, L, state) end
    return
  end
  drawHeader(game, L, state, seat)
  if m == "firing" then
    drawFiring(game, L, state, seat, ui)
  elseif m == "placing" then
    drawPlacing(game, L, state, seat, ui)
  else
    drawWaiting(game, L, state, seat, ui)
  end
end

return view
