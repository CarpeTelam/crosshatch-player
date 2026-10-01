-- pass-hidden fixture: a hidden pass match's hand-off (epic-pass-and-play entry 4). Each seat
-- has a secret word only its own frame shows: seat 1's is "apple", seat 2's "river". A tap is a
-- move; the turn passes after each, so the match shows the mover's frame under the "Tap to pass"
-- banner, then the blank hand-off screen, then the next seat's frame. The round ends after four
-- moves with no winner, and seat 0's frame, the one for everyone, names both secrets. setup arms
-- a 5 s ch.timer and each tap re-arms it, so a timer that falls due during a hand-off shows where
-- it is delivered. The log lines name the seat each call was for. Only the timer depends on time:
-- on a device it falls due during a hand-off only when the players take more than 5 s, so a run
-- without a `timer for seat` line is no failure.
local game = {}

local SECRETS = { "apple", "river" }
local MOVES = 4
local DELAY_MS = 5000

function game.setup(ctx)
  ch.timer.after(DELAY_MS)
  return { seats = ctx.seats, moves = 0 }
end

function game.status(state)
  if state.moves >= MOVES then return { over = true, winners = {} } end
  return { turn = state.moves % state.seats + 1 }
end

function game.apply(state, seat, move)
  ch.log("apply seat " .. seat)
  state.moves = state.moves + 1
  return state
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("tap for seat " .. seat)
    ch.timer.after(DELAY_MS)
    return { tap = true }
  elseif ev.kind == "timer" then
    ch.log("timer for seat " .. seat)
  elseif ev.kind == "over" then
    ch.log("over for seat " .. seat)
  end
  return nil
end

function game.draw(state, seat, ui)
  ch.log("draw for seat " .. seat)
  ch.gfx.clear("white")
  if seat == 0 then
    ch.gfx.text(40, 120, "Everyone: the secrets were " .. SECRETS[1] .. " and " .. SECRETS[2], "small", "black")
    return
  end
  ch.gfx.text(40, 40, "Player " .. seat, "large", "black")
  ch.gfx.text(40, 120, "Player " .. seat .. "'s secret: " .. SECRETS[seat], "medium", "black")
  ch.gfx.text(40, 170, "Moves: " .. state.moves, "medium", "black")
end

return game
