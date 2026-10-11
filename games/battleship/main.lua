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
-- view.lua requires fleet and layout, so it loads after them and its own load nests no module.
local view = require("view")

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
-- view.lua draws everything about the other seat from here, so secrecy is one function the checks pin.
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

-- The HOW TO PLAY page's lines as { y, text } and the y below the last one, wrapped to the box's width.
function game.help_lines()
  local L = canvas()
  local out, y = {}, L.oy + 90
  for _, paragraph in ipairs(HELP_PARAGRAPHS) do
    for _, line in ipairs(view.wrap(paragraph, "small", L.W - 48)) do
      out[#out + 1] = { y = y, text = line }
      y = y + 28
    end
    y = y + 12
  end
  return out, y
end

-- The placement status line for a seat's own fleet: the ship to place and its direction, or the Ready prompt.
function game.prompt(own, vertical)
  local index = fleet.next_ship(own)
  if not index then return "All ships placed. Tap Ready" end
  local ship = fleet.SHIPS[index]
  return "Place the " .. ship.name .. " (" .. ship.len .. "), " .. (vertical and "down" or "across")
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

-- The shot count line for player i at Over: the shots fired at the other fleet.
function game.count_line(state, i)
  local shots = fleet.count_shots(state.f[3 - i])
  return "Player " .. i .. " fired " .. shots .. (shots == 1 and " shot" or " shots")
end

function game.draw(state, seat, ui)
  sayUnsupported()
  view.draw(game, canvas(), state, seat, ui, mode(state, seat))
end

return game
