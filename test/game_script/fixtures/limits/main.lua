-- Fault fixture for the simulator and the device: each band breaks one limit of
-- entries 8 to 10 (AD-10's codec limits, AD-8's status, AD-7's display list) or
-- raises a plain Lua error. Every band must end in the error view with Back.
-- Copy this folder to /.games/limits/ on the SD card (fs_/.games/limits/ in the simulator).
local game = {}

local BANDS = {
  { label = "State over 1,400 bytes", hint = "apply returns it" },
  { label = "Move over 256 bytes", hint = "input returns it" },
  { label = "ch.store over 4 KB", hint = "ch.store.set refuses it" },
  { label = "Invalid status", hint = "status returns turn 2 of 1 seat" },
  { label = "Frame over 2,048 commands", hint = "draw overflows the display list" },
  { label = "Lua error in input", hint = "error('boom')" },
}
local TOP = 100
local BAND_HEIGHT = 110

function game.setup(ctx)
  return { fault = 0 }
end

function game.status(state)
  if state.fault == 4 then return { turn = 2 } end
  return { turn = 1 }
end

function game.apply(state, seat, move)
  state.fault = move.fault
  if move.fault == 1 then state.pad = string.rep("x", 1500) end
  return state
end

function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  if state.fault == 5 then
    for i = 1, 2100 do ch.gfx.rect(0, 0, 1, 1, "black", true) end
  end
  ch.gfx.text(20, 30, "Limit faults", "large", "black")
  for i, band in ipairs(BANDS) do
    local top = TOP + (i - 1) * BAND_HEIGHT
    ch.gfx.rect(20, top, 430, BAND_HEIGHT - 16, "black", false)
    ch.gfx.text(40, top + 16, band.label, "medium", "black")
    ch.gfx.text(40, top + 56, band.hint, "small", "black")
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind ~= "tap" then return nil end
  local i = (ev.y - TOP) // BAND_HEIGHT + 1
  if i == 2 then return { fault = 2, pad = string.rep("x", 300) } end
  if i == 3 then ch.store.set({ pad = string.rep("x", 5000) }) end
  if i == 6 then error("boom") end
  if i == 1 or i == 4 or i == 5 then return { fault = i } end
  return nil
end

return game
