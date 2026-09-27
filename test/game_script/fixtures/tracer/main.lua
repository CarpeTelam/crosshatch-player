-- Tracer fixture: a move-driven solo game. A tap below the banner is a move that
-- apply records in the state; a tap on the banner is rejected; the fifth recorded
-- tap ends the round. ui keeps the last rejection and counts over events.
-- Copy this folder to /.games/tracer/ on the SD card (fs_/.games/tracer/ in the simulator).
local game = {}

local GOAL = 5
local BANNER_BOTTOM = 130

function game.setup(ctx)
  return { taps = 0, ctx = ctx.mode .. ", " .. ctx.seats .. " seat, api " .. ctx.api }
end

function game.status(state)
  if state.taps >= GOAL then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end

function game.apply(state, seat, move)
  if move.y < BANNER_BOTTOM then return nil, "Not on the banner" end
  state.taps = state.taps + 1
  state.x, state.y = move.x, move.y
  return state
end

function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.rect(20, 20, 400, 100, "black", false)
  ch.gfx.text(40, 50, "Tracer", "large", "black")
  ch.gfx.text(40, 150, "Taps: " .. state.taps .. " of " .. GOAL, "medium", "black")
  if game.status(state).over then
    ch.gfx.text(40, 190, "Game over", "medium", "black")
    ch.gfx.text(40, 230, "Over events: " .. (ui.overs or 0), "small", "black")
  else
    ch.gfx.text(40, 190, ui.message or "Tap below the banner", "small", "black")
  end
  ch.gfx.text(40, 270, state.ctx, "small", "black")
  if state.x then
    ch.gfx.rect(state.x - 20, state.y - 20, 40, 40, "black", true)
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind == "rejected" then
    ui.message = ev.reason
  elseif ev.kind == "over" then
    ui.overs = (ui.overs or 0) + 1
  elseif ev.kind == "tap" then
    ui.message = nil
    return { x = ev.x, y = ev.y }
  end
  return nil
end

return game
