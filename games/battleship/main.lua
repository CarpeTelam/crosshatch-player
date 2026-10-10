-- Battleship: two players, each with a hidden fleet of five ships on a 10 x 10 board. They place their ships one at a
-- time (the device is passed between them), then take turns firing at the other board; the first to sink all five
-- ships wins. Each seat's frame shows only its own fleet and its own shots; seat 0, shown once the round is over,
-- shows both fleets.
--
-- state = {
--   f = { <100 chars>, <100 chars> },  seat s's own fleet (fleet.lua has the alphabet),
--   p = phase: 1 or 2 is that seat placing its ships, 3 is firing,
--   t = the seat that fires (1 first; set when seat 2's Ready makes p 3),
--   l = the last shot { seat, row, col, result, ship }, result 0 miss, 1 hit, 2 sunk, ship 1..5 (0 for a miss);
--       absent until a shot }
-- A move is { "P", row, col, "H" | "V" } (place the next ship, across or down), { "R" } (Random places the ships not
-- placed yet), { "C" } (Clear lifts every ship), { "Y" } (Ready: the placement is done), or { "F", row, col } (fire).
local fleet = require("fleet")
local layout = require("layout")

local game = {}

local OVER = "The game is over"
local PLACE_FIRST = "Place your ships first"
local PLACED = "The ships are placed. Fire at the other board"
local TAP_BOARD = "Tap a square on the board"
local NO_SHIPS = "No ships to clear"
local NOT_READY = "Place all five ships first"

local HELP_PARAGRAPHS = {
  "Sink all five of the other player's ships before they sink yours. The ships are 5, 4, 3, 3, and 2 squares long, "
    .. "on a 10 by 10 board.",
  "Placing: tap a square to put the next ship there, longest first. Rotate turns it. Random places the ships you have "
    .. "not placed yet, Clear lifts them all, and Ready hands the device on. Ships may touch but not overlap.",
  "Firing: tap a square on the big board. A wave is a miss, a flame a hit, and a sunk ship is named. You cannot fire "
    .. "at a square twice.",
  "The small board is your fleet: rings are the other player's misses, and crossed ships their hits.",
  "When the device is passed, look away. The first to sink all five ships wins.",
  "The question mark button opens this page. Tap anywhere to close it.",
}

local ICONS = { rotate = "arrow-clockwise", random = "shuffle", clear = "trash", ready = "check" }
local LABELS = { rotate = "Rotate", random = "Random", clear = "Clear", ready = "Ready" }

local function canvas() return layout.compute(ch.screen.w, ch.screen.h) end

-- The game lays out one fixed 466 x 788 box (layout.lua) and never adapts to a smaller canvas: it says so once per VM.
local said = false
local function sayUnsupported()
  if said or layout.fits(ch.screen.w, ch.screen.h) then return end
  said = true
  ch.log("Battleship: the canvas is " .. ch.screen.w .. " x " .. ch.screen.h .. " and the game needs " .. layout.W
    .. " x " .. layout.H .. ": unsupported, laid out from the canvas's corner")
end

function game.setup(ctx)
  return { f = { fleet.EMPTY, fleet.EMPTY }, p = 1, t = 1 }
end

function game.status(state)
  if state.p < 3 then return { turn = state.p } end
  if fleet.beaten(state.f[2]) then return { over = true, winners = { 1 } } end
  if fleet.beaten(state.f[1]) then return { over = true, winners = { 2 } } end
  return { turn = state.t }
end

local function isCell(v) return math.type(v) == "integer" and v >= 1 and v <= fleet.SIZE end

-- Validates the whole move before changing anything, so a rejection leaves the state as it was.
function game.apply(state, seat, move)
  if game.status(state).over then return nil, OVER end
  local kind = type(move) == "table" and move[1] or nil
  if kind ~= "P" and kind ~= "R" and kind ~= "C" and kind ~= "Y" and kind ~= "F" then return nil, TAP_BOARD end

  if kind == "F" then
    if state.p < 3 then return nil, PLACE_FIRST end
    local row, col = move[2], move[3]
    if not isCell(row) or not isCell(col) then return nil, TAP_BOARD end
    local target = 3 - state.t
    local f, result, ship = fleet.shot(state.f[target], row, col)
    if not f then return nil, result end
    state.f[target] = f
    state.l = { state.t, row, col, result, ship }
    -- The turn passes, unless the shot sank the last ship: then the shooter stays the turn seat and the round is over.
    if not fleet.beaten(f) then state.t = target end
    return state
  end

  if state.p == 3 then return nil, PLACED end
  local own = state.f[state.p]
  if kind == "P" then
    local row, col, dir = move[2], move[3], move[4]
    if not isCell(row) or not isCell(col) or (dir ~= "H" and dir ~= "V") then return nil, TAP_BOARD end
    local f, reason = fleet.place(own, row, col, dir == "V")
    if not f then return nil, reason end
    state.f[state.p] = f
  elseif kind == "R" then
    local f, reason = fleet.random_fill(own)
    if not f then return nil, reason end
    state.f[state.p] = f
  elseif kind == "C" then
    if not own:find("[a-e]") then return nil, NO_SHIPS end
    state.f[state.p] = fleet.EMPTY
  else
    if fleet.next_ship(own) then return nil, NOT_READY end
    state.p = state.p + 1
    if state.p == 3 then state.t = 1 end
  end
  return state
end

-- What a seat's frame may hold: its own fleet, the other fleet as `fleet.mask` shows it (only once firing starts),
-- and the number of the other seat's ships not yet sunk (public: every sinking is announced). Seat 0 sees both fleets.
-- draw takes everything it draws about the other seat from here, so secrecy is one function the checks pin.
function game.views(state, seat)
  if seat == 0 then return { fleets = { state.f[1], state.f[2] } } end
  local view = { own = state.f[seat] }
  if state.p == 3 then
    local other = state.f[3 - seat]
    view.target = fleet.mask(other)
    view.left = fleet.ships_left(other)
  end
  return view
end

function game.headline(state, seat)
  local st = game.status(state)
  if st.over then return "Player " .. st.winners[1] .. " wins!" end
  if seat == 0 then return "Battleship" end
  if state.p < 3 then
    if state.p == seat then return "Player " .. seat .. ": place ships" end
    if seat < state.p then return "Player " .. seat .. ": fleet ready" end
    return "Player " .. seat .. ": waiting"
  end
  if state.t == seat then return "Player " .. seat .. ": fire!" end
  return "Player " .. seat .. ": waiting"
end

-- The layout mode of a seat's frame: "over" (seat 0), "firing", "placing", or "waiting" (placed, the other seat is
-- placing). The same four decide what a tap means.
local function mode(state, seat)
  if seat == 0 then return "over" end
  if state.p == 3 then return "firing" end
  if state.p == seat then return "placing" end
  return "waiting"
end

function game.input(state, seat, ui, ev)
  if ev.kind == "rejected" then
    ui.message = ev.reason
    return nil
  end
  if ev.kind ~= "tap" then return nil end
  if ui.help then
    ui.help = nil
    return nil
  end
  ui.message = nil
  local L = canvas()
  -- Seat 0's frame draws no question button, so it has no tap target there either.
  if seat ~= 0 and layout.inside(L.question_rect, ev.x, ev.y) then
    ui.help = true
    return nil
  end
  if game.status(state).over then return nil end
  local m = mode(state, seat)
  -- A tap in the margin beside the edge cells or buttons counts as that target (layout.snap); exact hits come first.
  local bigRect = layout.board_rect(L.big)
  local bpx, bpy = layout.snap(L, ev.x, ev.y, bigRect, L.big.cell, L.big.cell)
  if m == "placing" then
    local qx, qy = layout.snap(L, ev.x, ev.y, L.button_row, L.button_w, L.buttons.rotate.h)
    if layout.inside(L.buttons.rotate, qx, qy) then
      ui.vertical = not ui.vertical
      return nil
    end
    if layout.inside(L.buttons.random, qx, qy) then return { "R" } end
    if layout.inside(L.buttons.clear, qx, qy) then return { "C" } end
    if layout.inside(L.buttons.ready, qx, qy) then return { "Y" } end
    local row, col = layout.cell_at(L.big, bpx, bpy)
    if not row then return nil end
    -- The tapped square is the ship's first; a ship that would pass the edge is shifted back to fit.
    local index = fleet.next_ship(state.f[seat])
    local len = index and fleet.SHIPS[index].len or 1
    if ui.vertical then
      return { "P", math.min(row, fleet.SIZE + 1 - len), col, "V" }
    end
    return { "P", row, math.min(col, fleet.SIZE + 1 - len), "H" }
  elseif m == "firing" then
    local row, col = layout.cell_at(L.big, bpx, bpy)
    if row then return { "F", row, col } end
  end
  return nil
end

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

-- The HOW TO PLAY page's lines as { y, text } and the y below the last one, wrapped to the box's width.
function game.help_lines()
  local L = canvas()
  local out, y = {}, L.oy + 90
  for _, paragraph in ipairs(HELP_PARAGRAPHS) do
    for _, line in ipairs(wrap(paragraph, "small", L.W - 48)) do
      out[#out + 1] = { y = y, text = line }
      y = y + 28
    end
    y = y + 12
  end
  return out, y
end

local function drawHelp()
  local L = canvas()
  ch.gfx.text(L.ox + 24, L.oy + 24, "HOW TO PLAY", "large", "black")
  for _, line in ipairs(game.help_lines()) do ch.gfx.text(L.ox + 24, line.y, line.text, "small", "black") end
end

local function drawHeader(L, state, seat)
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

-- The placement status line for a seat's own fleet: the ship to place and its direction, or the Ready prompt.
function game.prompt(own, vertical)
  local index = fleet.next_ship(own)
  if not index then return "All ships placed. Tap Ready" end
  local ship = fleet.SHIPS[index]
  return "Place the " .. ship.name .. " (" .. ship.len .. "), " .. (vertical and "down" or "across")
end

local function drawPlacing(L, state, seat, ui)
  local view = game.views(state, seat)
  drawFleet(L.big, view.own)
  ch.gfx.text(L.status.x, L.status.y, game.prompt(view.own, ui.vertical), "medium", "black")
  drawTray(L, view.own)
  drawButtons(L, view.own)
  drawMessage(ui, L.message.x, L.message.y, layout.BOARD * L.big.cell)
end

-- Placed, and the other seat is placing: the fleet and nothing to tap.
local function drawWaiting(L, state, seat, ui)
  local view = game.views(state, seat)
  drawFleet(L.big, view.own)
  local line = seat < state.p and "Your fleet is ready" or "Waiting for Player " .. state.p
  ch.gfx.text(L.status.x, L.status.y, line, "medium", "black")
  drawTray(L, view.own)
  drawMessage(ui, L.message.x, L.message.y, layout.BOARD * L.big.cell)
end

local RESULT_MINE = { [0] = "Miss", "Hit!" }
local RESULT_THEIRS = { [0] = " missed", " hit your ship" }

-- The last shot as the seat sees it: the shooter's own result, or what the other player did to its fleet.
function game.last_shot_line(state, seat)
  local l = state.l
  if not l then
    if state.t == seat then return "Tap a square to fire" end
    return "Waiting for Player " .. state.t
  end
  local name = fleet.SHIPS[l[5]] and fleet.SHIPS[l[5]].name
  if l[1] == seat then return l[4] == 2 and ("You sank the " .. name .. "!") or RESULT_MINE[l[4]] end
  if l[4] == 2 then return "Player " .. l[1] .. " sank your " .. name .. "!" end
  return "Player " .. l[1] .. RESULT_THEIRS[l[4]]
end

-- Firing, and the boards of a finished round: the target large, the own fleet small, and the column beside the small
-- board.
local function drawFiring(L, state, seat, ui)
  local view = game.views(state, seat)
  drawTarget(L.big, view.target)
  drawFleet(L.small, view.own)
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
  ch.gfx.text(col.x + 36, y, "Their ships: " .. view.left, "small", "black")
  ch.gfx.text(col.x + 36, y + 24, "Your ships: " .. fleet.ships_left(view.own), "small", "black")
  drawMessage(ui, col.x, y + 56, col.w)
end

-- The shot count line for player i at Over: the shots fired at the other fleet.
function game.count_line(state, i)
  local shots = fleet.count_shots(state.f[3 - i])
  return "Player " .. i .. " fired " .. shots .. (shots == 1 and " shot" or " shots")
end

-- Seat 0 at Over: both fleets and each player's shot count.
local function drawBoth(L, state)
  local view = game.views(state, 0)
  for i = 1, 2 do
    ch.gfx.text(L.over[i].x, L.over_label_y, "Player " .. i, "small", "black")
    drawFleet(L.over[i], view.fleets[i])
    ch.gfx.text(L.over[1].x, L.over_counts_y + (i - 1) * 28, game.count_line(state, i), "small", "black")
  end
end

function game.draw(state, seat, ui)
  sayUnsupported()
  ch.gfx.clear("white")
  if ui.help then
    drawHelp()
    return
  end
  local L = canvas()
  local over = game.status(state).over
  if seat == 0 then
    ch.gfx.text(L.big.x, L.oy + 12, game.headline(state, 0), "medium", "black")
    if over then drawBoth(L, state) end
    return
  end
  drawHeader(L, state, seat)
  local m = mode(state, seat)
  if m == "firing" then
    drawFiring(L, state, seat, ui)
  elseif m == "placing" then
    drawPlacing(L, state, seat, ui)
  else
    drawWaiting(L, state, seat, ui)
  end
end

return game
