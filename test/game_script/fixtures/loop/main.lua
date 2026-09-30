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

-- A backtracking pattern that fails: string.find tries about C(n + k + 1, k + 1)
-- splits (over every start position) inside one C call, where no hook runs, while
-- the Lua loop around it spends few instructions. k stays within the matcher's
-- depth limit (MAXCCALLS, 16).
local function backtrackArgs(k, n)
  return string.rep("a", n), string.rep(".-", k) .. "b"
end

local function backtrack(k, n)
  return string.find(backtrackArgs(k, n))
end

-- Each call must end well inside the 500 ms the match waits for the VM after a
-- cancel (GameMatchActivity::STOP_TIMEOUT_MS): the hook runs at the next call, and
-- only then sees the cancel. The pattern is built once, so the loop spends about 5
-- instructions a call and the 2 M instruction budget stays far beyond the 3 s
-- watchdog.
local function slowCallsForever()
  local find = string.find
  local subject, pattern = backtrackArgs(6, 10)
  while true do find(subject, pattern) end -- about 19 K splits (27 K matcher steps) a call
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

function game.status(state)
  return { turn = 1 }
end

function game.apply(state, seat, move)
  return state
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
