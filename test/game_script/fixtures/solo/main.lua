-- Solo check fixture: the closing device run of epic-script-runtime (Done-when 1).
-- A round is eight prompts against a 60 s countdown: tap the circle, hold the
-- square, or swipe the way the arrow points. input turns each touch and timer
-- event into a move; apply scores it and ticks the clock; status ends the round
-- when the prompts run out or the time does. The round's end saves rounds
-- finished and the best score with ch.store, shown at the bottom, which survive
-- Leave, reopening, and a restart. The checklist shows which inputs this round
-- has seen. A round's first frame asks for a full refresh, its last a half one,
-- and the rest fast ones; the over event cancels the timer.
-- Copy this folder to /.games/solo/ on the SD card (fs_/.games/solo/ in the simulator).
local game = {}

local START_S = 60
local TICK_MS = 5000
local TICK_S = TICK_MS // 1000
local R = 50 -- the circle's radius
local HALF = 60 -- half the square's side
local SLACK = 12 -- a touch this far outside a target still hits it

-- Swipes start inside the play box, clear of the Back (left quarter) and Home
-- (bottom 14 %) edge gestures.
local STEPS = {
  { k = "tap", x = 140, y = 300 },
  { k = "hold", x = 330, y = 430 },
  { k = "swipe", d = "left" },
  { k = "tap", x = 340, y = 280 },
  { k = "swipe", d = "up" },
  { k = "hold", x = 160, y = 440 },
  { k = "swipe", d = "right" },
  { k = "swipe", d = "down" },
}

local CHECKS = {
  { key = "tap", label = "tap" },
  { key = "hold", label = "long press" },
  { key = "swipe", label = "swipe" },
  { key = "timer", label = "timer" },
}

local function score(state)
  return math.max(0, 10 * state.hits + state.left - 5 * state.misses)
end

local function over(state)
  return state.step > #STEPS or state.left <= 0
end

local function describe(move)
  if move.k == "tap" then return "a tap" end
  if move.k == "hold" then return "a long press" end
  return "a swipe " .. move.d
end

local function hits(want, move)
  if move.k ~= want.k then return false end
  if move.k == "swipe" then return move.d == want.d end
  local dx, dy = move.x - want.x, move.y - want.y
  if move.k == "tap" then return dx * dx + dy * dy <= (R + SLACK) ^ 2 end
  return math.abs(dx) <= HALF + SLACK and math.abs(dy) <= HALF + SLACK
end

function game.setup(ctx)
  ch.timer.after(TICK_MS)
  local saved = ch.store.get()
  return {
    round = (saved.rounds or 0) + 1,
    step = 1, left = START_S, hits = 0, misses = 0, moves = 0,
    seen = { tap = false, hold = false, swipe = false, timer = false },
    note = "Go",
  }
end

function game.status(state)
  if over(state) then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end

function game.apply(state, seat, move)
  state.moves = state.moves + 1
  if move.k == "tick" then
    state.left = math.max(0, state.left - TICK_S)
    state.seen.timer = true
  else
    state.seen[move.k] = true
    if move.k == "swipe" then state.dir = move.d end
    if hits(STEPS[state.step], move) then
      state.hits = state.hits + 1
      state.step = state.step + 1
      state.note = "Hit: " .. describe(move)
    else
      state.misses = state.misses + 1
      local aimed = move.k ~= "swipe" and move.k == STEPS[state.step].k
      state.note = "Miss: " .. describe(move) .. (aimed and " off target" or "")
    end
  end
  if over(state) then
    state.final = score(state)
    local saved = ch.store.get()
    ch.store.set({ rounds = (saved.rounds or 0) + 1, best = math.max(saved.best or 0, state.final) })
  end
  return state
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    return { k = "tap", x = ev.x, y = ev.y }
  elseif ev.kind == "long_press" then
    return { k = "hold", x = ev.x, y = ev.y }
  elseif ev.kind == "swipe" then
    return { k = "swipe", d = ev.dir }
  elseif ev.kind == "timer" then
    if state.left - TICK_S > 0 then ch.timer.after(TICK_MS) end
    return { k = "tick" }
  elseif ev.kind == "over" then
    ch.timer.cancel()
    ui.over_round = state.round
  end
  return nil
end

local ARROWS = { left = { -1, 0 }, right = { 1, 0 }, up = { 0, -1 }, down = { 0, 1 } }

local function arrow(cx, cy, dir, len)
  local ux, uy = ARROWS[dir][1], ARROWS[dir][2]
  local tx, ty = cx + ux * len, cy + uy * len
  local head = len // 3
  ch.gfx.line(cx - ux * len, cy - uy * len, tx, ty, "black")
  -- The two barbs: back along the shaft, out to either side.
  ch.gfx.line(tx, ty, tx - ux * head + uy * head, ty - uy * head + ux * head, "black")
  ch.gfx.line(tx, ty, tx - ux * head - uy * head, ty - uy * head - ux * head, "black")
end

local function drawTarget(want)
  if want.k == "tap" then
    ch.gfx.circle(want.x, want.y, R, "dark", true)
    ch.gfx.circle(want.x, want.y, R, "black", false)
    ch.gfx.text(want.x, want.y + R + 8, "Tap", "small", "black", "center")
  elseif want.k == "hold" then
    ch.gfx.rect(want.x - HALF, want.y - HALF, 2 * HALF, 2 * HALF, "light", true)
    ch.gfx.rect(want.x - HALF, want.y - HALF, 2 * HALF, 2 * HALF, "black", false)
    ch.gfx.text(want.x, want.y - 12, "Hold", "small", "black", "center")
  else
    arrow(240, 380, want.d, 90)
    ch.gfx.text(240, 530, "Swipe " .. want.d .. " inside this box", "small", "black", "center")
  end
end

local function drawOver(state, ui)
  local saved = ch.store.get()
  -- Above and below the end-of-round menu, which covers the middle of the frame.
  ch.gfx.text(240, 184, "Round over", "large", "black", "center")
  ch.gfx.text(240, 244, "Score " .. state.final .. ", best " .. (saved.best or 0), "medium", "black", "center")
  if ui.over_round == state.round then
    ch.gfx.text(240, 530, "Over event received", "small", "black", "center")
  end
end

function game.draw(state, seat, ui)
  local W = ch.screen.w
  ch.gfx.clear("white")
  ch.gfx.rect(0, 0, W, 64, "light", true)
  ch.gfx.text(W // 2, 6, "Solo check", "large", "black", "center")

  ch.gfx.text(16, 74, "Round " .. state.round, "medium", "black")
  local tally = over(state) and ("Score " .. state.final) or ("Hits " .. state.hits .. "  Misses " .. state.misses)
  ch.gfx.text(W - 16, 74, tally, "medium", "black", "right")
  -- The countdown as a dark bar that shrinks every tick.
  ch.gfx.rect(16, 114, W - 32, 18, "black", false)
  ch.gfx.rect(18, 116, (W - 36) * state.left // START_S, 14, "dark", true)
  ch.gfx.text(W // 2, 136, state.left .. " s left", "small", "black", "center")

  ch.gfx.rect(16, 172, W - 32, 410, "black", false)
  if over(state) then
    drawOver(state, ui)
  else
    local want = STEPS[state.step]
    local verb = want.k == "tap" and "Tap the circle" or want.k == "hold" and "Hold the square" or ("Swipe " .. want.d)
    ch.gfx.text(W // 2, 184, state.step .. " of " .. #STEPS .. ": " .. verb, "medium", "black", "center")
    drawTarget(want)
  end

  ch.gfx.text(16, 590, state.note, "small", "black")
  ch.gfx.text(W - 16, 590, "Last swipe: " .. (state.dir or "none"), "small", "black", "right")

  ch.gfx.text(16, 626, "Seen this round:", "small", "black")
  for i, check in ipairs(CHECKS) do
    local x, y = 16 + ((i - 1) % 2) * 232, 660 + ((i - 1) // 2) * 34
    ch.gfx.rect(x, y, 22, 22, "black", false)
    if state.seen[check.key] then ch.gfx.rect(x + 4, y + 4, 14, 14, "black", true) end
    ch.gfx.text(x + 32, y - 2, check.label, "small", "black")
  end

  local saved = ch.store.get()
  ch.gfx.line(16, 736, W - 16, 736, "black")
  ch.gfx.text(16, 746, "Rounds finished " .. (saved.rounds or 0), "small", "black")
  ch.gfx.text(W - 16, 746, "Best " .. (saved.best or 0), "small", "black", "right")

  if state.moves == 0 then
    ch.gfx.refresh("full")
  elseif over(state) then
    ch.gfx.refresh("half")
  else
    ch.gfx.refresh("fast")
  end
end

return game
