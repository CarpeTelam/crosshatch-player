-- Slow-restart fixture: the tracer's round, whose Play again takes about 2 s. Every
-- round after the first spins in setup for SPIN_MS on ch.time.ms(), so a tap made
-- while the end-of-round menu is still on screen falls in the gap before the new
-- round's first frame. That tap must not reach the new round: it starts at
-- "Round 2, taps: 0 of 3" with no square drawn.
-- Copy this folder to /.games/slow-restart/ on the SD card (fs_/.games/slow-restart/
-- in the simulator).
local game = {}

local GOAL = 3
local BANNER_BOTTOM = 130
local SPIN_MS = 2000

-- Rounds started since the game opened; setup runs once a round.
local round = 0

-- A backtracking pattern that fails: many steps inside one C call and few Lua
-- instructions around it, so the spin stays far under the instruction budget.
local SUBJECT = string.rep("a", 24)
local PATTERN = string.rep(".-", 5) .. "b"
local function slowCall()
  return string.find(SUBJECT, PATTERN)
end

function game.setup(ctx)
  round = round + 1
  if round > 1 then
    local start = ch.time.ms()
    while ch.time.ms() - start < SPIN_MS do slowCall() end
  end
  return { taps = 0, round = round }
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
  ch.gfx.text(40, 50, "Slow restart", "large", "black")
  ch.gfx.text(40, 150, "Round " .. state.round .. ", taps: " .. state.taps .. " of " .. GOAL, "medium", "black")
  ch.gfx.text(40, 190, ui.message or "Tap below the banner", "small", "black")
  if state.round > 1 then
    ch.gfx.text(40, 230, "This round's setup took about 2 s", "small", "black")
  end
  if state.x then
    ch.gfx.rect(state.x - 20, state.y - 20, 40, 40, "black", true)
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind == "rejected" then
    ui.message = ev.reason
  elseif ev.kind == "tap" then
    ui.message = nil
    return { x = ev.x, y = ev.y }
  end
  return nil
end

return game
