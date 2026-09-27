-- Timer fixture: redraws on its own. setup arms a 3 s timer; each timer event is
-- a move that apply counts and saves with ch.store.set, and input re-arms until
-- the third tick ends the round. A tap restarts the countdown. print and ch.log
-- lines show in the device log tagged "timer".
-- Copy this folder to /.games/timer/ on the SD card (fs_/.games/timer/ in the simulator).
local game = {}

local DELAY_MS = 3000
local TICKS = 3

function game.setup(ctx)
  ch.timer.after(DELAY_MS)
  print("armed", DELAY_MS, "ms; api", ch.api)
  return { ticks = 0, at = 0 }
end

function game.status(state)
  if state.ticks >= TICKS then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end

function game.apply(state, seat, move)
  state.ticks = state.ticks + 1
  state.at = move.at
  ch.store.set({ ticks = state.ticks })
  return state
end

function game.input(state, seat, ui, ev)
  if ev.kind == "timer" then
    local now = ch.time.ms()
    ch.log("tick at", now, "ms")
    if state.ticks + 1 < TICKS then ch.timer.after(DELAY_MS) end
    return { at = now }
  elseif ev.kind == "tap" and state.ticks < TICKS then
    ch.timer.after(DELAY_MS)
    ui.restarted = (ui.restarted or 0) + 1
  end
end

function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.rect(20, 20, 400, 100, "black", false)
  ch.gfx.text(40, 50, "Timer", "large", "black")
  ch.gfx.text(40, 150, "Ticks: " .. state.ticks .. " of " .. TICKS, "medium", "black")
  if state.ticks > 0 then
    ch.gfx.text(40, 190, "Last tick at " .. state.at .. " ms", "small", "black")
  else
    ch.gfx.text(40, 190, "Waiting " .. DELAY_MS // 1000 .. " s for the first tick", "small", "black")
  end
  if game.status(state).over then
    ch.gfx.text(40, 230, "Done", "medium", "black")
  end
  ch.gfx.text(40, 270, "Saved ticks: " .. (ch.store.get().ticks or 0), "small", "black")
  ch.gfx.text(40, 300, "Drawn at " .. ch.time.ms() .. " ms, api " .. ch.api, "small", "black")
  if ui.restarted then ch.gfx.text(40, 330, "Restarted " .. ui.restarted .. "x", "small", "black") end
end

return game
