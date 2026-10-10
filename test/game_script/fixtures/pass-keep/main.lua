-- pass-keep fixture: a fixture hidden pass game whose move can keep the turn (pass-hidden's every move
-- passes it); the tests' own HIDDEN_KEEPS_TURN_GAME (games_check/TestSupport.h) and Battleship's
-- placement moves keep the turn too, so it is the engine's simplest such game, not its only one. A tap above y = 200 is a "keep" move: the mover's `kept` count goes up by
-- one and the turn stays, so the match stays in Playing on the same seat's frame. A tap at or
-- below y = 200 is a "pass" move: the turn goes to the next seat and `kept` resets. The frame
-- shows "Player S", "Kept: K", and "Passes: P". The round never ends. The log lines name the
-- seat each call was for (`tap for seat S`, `apply seat S <kind>`, `draw for seat S`).
local game = {}

function game.setup(ctx)
  return { seats = ctx.seats, turn = 1, kept = 0, passes = 0 }
end

function game.status(state)
  return { turn = state.turn }
end

function game.apply(state, seat, move)
  ch.log("apply seat " .. seat .. " " .. move.kind)
  if move.kind == "keep" then
    state.kept = state.kept + 1
  else
    state.turn = state.turn % state.seats + 1
    state.kept = 0
    state.passes = state.passes + 1
  end
  return state
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("tap for seat " .. seat)
    if ev.y < 200 then return { kind = "keep" } end
    return { kind = "pass" }
  end
  return nil
end

function game.draw(state, seat, ui)
  ch.log("draw for seat " .. seat)
  ch.gfx.clear("white")
  ch.gfx.text(40, 40, "Player " .. seat, "large", "black")
  ch.gfx.text(40, 120, "Kept: " .. state.kept, "medium", "black")
  ch.gfx.text(40, 170, "Passes: " .. state.passes, "medium", "black")
end

return game
