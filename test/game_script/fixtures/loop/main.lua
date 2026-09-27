-- Fault fixture for the simulator and the device: each band runs a script that
-- never ends on its own. The runtime must stop it and stay responsive.
-- Copy this folder to /.games/loop/ on the SD card (fs_/.games/loop/ in the simulator).
local game = {}

local function loopForever()
  while true do end
end

local function loopInsidePcall()
  while true do
    pcall(loopForever)
  end
end

local function recurse()
  pcall(recurse)
end

-- A backtracking pattern that fails: string.find tries about C(n + k, k) splits
-- inside one C call, where no hook runs, while the Lua loop around it spends few
-- instructions. k stays within the matcher's depth limit (MAXCCALLS, 16).
local function backtrack(k, n)
  return string.find(string.rep("a", n), string.rep(".-", k) .. "b")
end

local function slowCallsForever()
  while true do backtrack(6, 30) end -- about 2 M steps a call
end

local function stuckInOneCall()
  backtrack(12, 40) -- about 2 x 10^11 steps
end

local BANDS = {
  { label = "Loop forever", hint = "Ends on the instruction budget", run = loopForever },
  { label = "Loop inside pcall", hint = "Ends too: the budget is sticky", run = loopInsidePcall },
  { label = "Recurse through pcall", hint = "Ends on C stack headroom", run = recurse },
  { label = "Slow C calls forever", hint = "The 3 s watchdog cancels it", run = slowCallsForever },
  { label = "Stuck in one C call", hint = "The watchdog abandons it", run = stuckInOneCall },
}
local TOP = 100
local BAND_HEIGHT = 130

function game.setup(ctx)
  return {}
end

function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.text(20, 30, "Runaway scripts", "large", "black")
  for i, band in ipairs(BANDS) do
    local top = TOP + (i - 1) * BAND_HEIGHT
    ch.gfx.rect(20, top, 440, BAND_HEIGHT - 20, "black", false)
    ch.gfx.text(40, top + 20, band.label, "medium", "black")
    ch.gfx.text(40, top + 65, band.hint, "small", "black")
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind ~= "tap" then return nil end
  local i = (ev.y - TOP) // BAND_HEIGHT + 1
  if BANDS[i] then BANDS[i].run() end
  return nil
end

return game
