-- pass-art fixture: what a developer controls (epic-pass-and-play entry 12): a title.png splash (480 x 480), a
-- handoff.png hand-off page (480 x 800), default_mode "pass", and two settings, Level (Easy, Hard; default Hard) and
-- Board (Small, Medium, Large; default the first). A hidden game that also plays solo. setup reads ctx.settings and
-- keeps the values in state, and every frame prints the mode and both values, so the screen shows which choices the
-- match was started with: Options changes them for the next New game, Play again keeps them, and Continue resumes
-- with the save's own. Each seat has a secret only its own frame shows (seat 1 "lantern", seat 2 "harbour"); a tap is
-- a move, and the round ends after four moves with no winner. The log lines name the seat each call was for.
local game = {}

local SECRETS = { "lantern", "harbour" }
local MOVES = 4

function game.setup(ctx)
  local level = ctx.settings.level or "none"
  local board = ctx.settings.board or "none"
  ch.log("setup " .. ctx.mode .. " level " .. level .. " board " .. board)
  return { seats = ctx.seats, mode = ctx.mode, level = level, board = board, moves = 0 }
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
    return { tap = true }
  elseif ev.kind == "over" then
    ch.log("over for seat " .. seat)
  end
  return nil
end

function game.draw(state, seat, ui)
  ch.log("draw for seat " .. seat)
  ch.gfx.clear("white")
  if seat == 0 then
    ch.gfx.text(40, 40, "Everyone", "large", "black")
    ch.gfx.text(40, 120, "The secrets were " .. SECRETS[1] .. " and " .. SECRETS[2], "small", "black")
  else
    ch.gfx.text(40, 40, "Player " .. seat, "large", "black")
    ch.gfx.text(40, 120, "Player " .. seat .. "'s secret: " .. SECRETS[seat], "medium", "black")
  end
  ch.gfx.text(40, 220, "Mode: " .. state.mode, "medium", "black")
  ch.gfx.text(40, 260, "Level: " .. state.level, "medium", "black")
  ch.gfx.text(40, 300, "Board: " .. state.board, "medium", "black")
  ch.gfx.text(40, 340, "Moves: " .. state.moves, "medium", "black")
end

return game
