-- interaction.lua: what taps do, how the toggles are kept, and what starts a ui over, pinned on the grid a round dealt
-- (a round's `steps(state)` calls them; rules.lua says why they live in a round's VM). Apart from rules.lua so that a
-- round that calls only these does not load that module's compiled code (first_party/README.md, "The check VMs' limits").
local game = require("main")
local pins = require("pins")
local eq, fresh, empties, answer, mv = pins.eq, pins.fresh, pins.empties, pins.answer, pins.mv
local candidate, rejects, tap, on_cell, on_key = pins.candidate, pins.rejects, pins.tap, pins.on_cell, pins.on_key
local on_rail, on_menu, with_clock, CLUE, ZEROS = pins.on_rail, pins.on_menu, pins.with_clock, pins.CLUE, pins.ZEROS
local time = pins.time
local layout = require("layout")

local interaction = {}

-- Taps select, write, clear, note, focus, and erase as the interaction table says.
function interaction.taps(state)
  collectgarbage("collect")
  local s, ui = fresh(state), {}
  local a, b, clue = empties(s)[1], empties(s)[2], s.v:find("[1-9]")
  eq(on_cell(s, ui, a), nil)
  eq(ui.sel, a)
  on_cell(s, ui, a)
  eq(ui.sel, nil)
  on_cell(s, ui, a)
  local d = candidate(s, a)
  local move = on_key(s, ui, d)
  eq(table.concat({ move[1], move[2], move[3] }, ","), "w," .. a .. "," .. d)
  eq(ui.sel, nil)
  eq(ui.foc, d)
  game.apply(s, 1, move)
  on_cell(s, ui, a)
  move = on_key(s, ui, d)
  eq(move[1], "w")
  game.apply(s, 1, move)
  eq(s.v:byte(a), 48)
  on_cell(s, ui, clue)
  eq(on_key(s, ui, 9), nil)
  eq(ui.foc, 9)
  on_key(s, ui, 9)
  eq(ui.foc, nil)
  move = on_rail(s, ui, 2)
  eq(move[1], "e")
  rejects(s, move, CLUE)
  on_rail(s, ui, 1)
  on_cell(s, ui, b)
  eq(on_key(s, ui, candidate(s, b))[1], "n")
  eq(ui.sel, nil)
  mv(s, "w", b, candidate(s, b))
  on_cell(s, ui, b)
  eq(on_key(s, ui, 2), nil)
  eq(ui.sel, b)
  eq(on_rail(s, ui, 3)[1], "u")
  eq(on_rail(s, ui, 4), nil)
  eq(ui.panel, "menu")
end

-- The toggles live in ch.store with defaults (NOTES AS reads DIGITS unless the store holds `dots = true`), and a best
-- time is kept per band unless HINT was used.
function interaction.store(state)
  ch.store.set({})
  local s, ui = fresh(state), {}
  tap(s, ui, 0, 0)
  assert(ui.rem and ui.shade and ui.timer and ui.dots == false)
  on_menu(s, ui, 4)
  on_menu(s, ui, 6)
  eq(ui.panel, "menu")
  eq(ui.dots, true)
  local stored = ch.store.get()
  assert(stored.rem == false and stored.dots == true and stored.shade == nil and stored.timer == nil)
  local again = {}
  tap(s, again, 0, 0)
  assert(not again.rem and again.shade and again.dots == true)
  on_menu(s, ui, 6) -- and back: the toggle writes `false`, which is DIGITS
  eq(ch.store.get().dots, false)
  -- The store's `dots`: none gives DIGITS, true gives DOTS, false gives DIGITS, and a value the toggle never writes
  -- gives DIGITS too (only a stored `true` gives DOTS).
  for _, case in ipairs({ { "no key", {}, false }, { "true", { dots = true }, true },
                          { "false", { dots = false }, false }, { "yes", { dots = "yes" }, false },
                          { "the string true", { dots = "true" }, false }, { "0", { dots = 0 }, false },
                          { "1", { dots = 1 }, false }, { "another key", { rem = false }, false } }) do
    ch.store.set(case[2])
    local read = {}
    tap(s, read, 0, 0)
    eq(read.dots, case[3], "the store with dots " .. case[1])
  end
  ch.store.set({ best = 5, rem = "yes", timer = "yes" })
  local odd = {}
  tap(s, odd, 0, 0)
  assert(odd.rem and odd.shade and odd.timer and odd.dots == false)
  ch.store.set({})
  local v = answer(s)
  -- A solved state of band `level` and time t (hinted or not): what the end screen shows, and what the store keeps.
  local function solve(level, t, hinted)
    local u = {}
    game.input({ l = level, v = v, n = ZEROS, u = "", t = t, h = hinted and 1 or nil }, 1, u, { kind = "over" })
    local best = ch.store.get().best
    return u.best, best and best[level]
  end
  eq(select(2, solve(1, 65000)), 65000)
  eq(select(2, solve(1, 70000)), 65000)
  eq(select(2, solve(1, 60000)), 60000)
  local shown, kept = solve(1, 1000, true)
  eq(shown, 60000)
  eq(kept, 60000)
  eq(select(2, solve(3, 90000)), 90000)
  eq(select(2, solve(1, 59999)), 59999)
  ch.store.set({})
  eq((solve(2, 1000, true)), nil)
  eq(ch.store.get().best, nil)
end

-- A new puzzle on a reused ui starts it over: selection, focus, pencil, panel, marks, the best time, the clock origin,
-- and the toggles read again from the store; the same puzzle leaves it alone.
function interaction.reset(state)
  with_clock(function()
    local s, ui = fresh(state), {}
    ch.store.set({ rem = false, dots = true })
    time.now = 100
    game.input(s, 1, ui, { kind = "timer" })
    assert(not ui.rem and ui.shade and ui.dots == true and ui.timer)
    eq(ui.last, 100)
    ui.sel, ui.foc, ui.pencil, ui.panel, ui.check, ui.best, ui.last = 5, 2, true, "menu", { [3] = true }, 1234, 777
    ui.timer = false
    game.input(s, 1, ui, { kind = "timer" })
    assert(ui.sel == 5 and ui.foc == 2 and ui.pencil and ui.panel == "menu" and ui.check and ui.best == 1234)
    eq(ui.timer, false)
    eq(ui.last, 777)
    local other = fresh(state)
    other.v = game.symmetry(state.v, math.random)
    assert(other.v ~= s.v)
    ch.store.set({ shade = false })
    time.now = 4000
    game.input(other, 1, ui, { kind = "timer" })
    assert(ui.sel == nil and ui.foc == nil and ui.pencil == nil and ui.panel == nil and ui.check == nil and ui.best == nil)
    eq(ui.last, 4000)
    assert(ui.rem and not ui.shade and ui.dots == false and ui.timer)
    ch.store.set({})
  end)
end

-- The play time in the header (Sudoku's TIMER). TEST DOUBLE for the engine's ch.timer: it records each call as
-- "after N" or "cancel", and refuses an `after` under 1,000 ms (and a non-integer) as ChBindings' timerAfter does; it
-- has none of the engine's timer semantics (no pending timer, no due time, no stale serial, no raw clock, no event):
-- what a pin reads is only which calls draw makes and with what delay. The rounds' real ch.timer takes the same calls.
local MINUTES = "^%d+ min$" -- the header time, as no other text is

local function with_timer(f)
  local real, calls = ch.timer, {}
  ch.timer = {
    after = function(ms)
      if math.tointeger(ms) == nil or ms < 1000 then
        error("ch.timer.after: at least 1000 ms, got " .. tostring(ms), 2)
      end
      calls[#calls + 1] = "after " .. ms
    end,
    cancel = function() calls[#calls + 1] = "cancel" end,
  }
  local ok, err = pcall(f, calls)
  ch.timer = real
  if not ok then error(err, 0) end
end

-- The texts of one draw of state s with ui u: the timer calls it made (the double's list is cleared first), the text
-- of the header time (a text ending " min", or nil), where it was drawn, and every text.
local function drawn_header(calls, s, u)
  local record = require("drawn").record
  for i = #calls, 1, -1 do calls[i] = nil end
  local texts = {}
  record(function() game.draw(s, 1, u) end, nil, function(name, x, y, str, size, color, align)
    if name == "text" then
      texts[#texts + 1] = { x = x, y = y, str = str, size = size, color = color, align = align }
    end
  end)
  local shown
  for _, t in ipairs(texts) do
    if t.str:find(MINUTES) then
      assert(shown == nil, "two header times in one frame")
      shown = t
    end
  end
  return table.concat(calls, ","), shown, texts
end

-- TIMER: the minutes in the header and the one timer behind them, every row of the MENU, and the toggle.
function interaction.timer(state)
  local view = require("view")
  local L = layout.get()
  collectgarbage("collect")
  -- The text of a play time: whole minutes, capped at the Solved screen's 99:59.
  eq(view.minutes(0), "0 min")
  eq(view.minutes(59999), "0 min")
  eq(view.minutes(60000), "1 min")
  eq(view.minutes(5999000), "99 min")
  with_clock(function()
    with_timer(function(calls)
      ch.store.set({})
      local s, ui = fresh(state), {}
      time.now = 0
      eq(game.input(s, 1, ui, { kind = "timer" }), nil)
      eq(ui.timer, true)
      -- A dealt board, TIMER on: the text of the clock and the delay to the next minute (at least 1,000 ms).
      local function board(now, want_text, want_calls, what)
        time.now = now
        local got, shown = drawn_header(calls, s, ui)
        eq(got, want_calls, what .. ": the timer calls")
        assert(shown, what .. ": no time in the header")
        eq(shown.str, want_text, what .. ": the time")
        eq(shown.size, "medium", what .. ": the time's size")
        eq(shown.color, "black", what .. ": the time's colour")
        eq(shown.align, "right", what .. ": the time's alignment")
        eq(shown.x, L.x + L.size, what .. ": the time ends at the grid's right edge")
        eq(shown.y, L.oy + 13, what .. ": the time's line")
        return shown
      end
      board(0, "0 min", "after 60000", "at 0 ms")
      board(45000, "0 min", "after 15000", "at 45,000 ms")
      board(59500, "0 min", "after 1000", "at 59,500 ms")
      board(59999, "0 min", "after 1000", "at 59,999 ms")
      board(60000, "1 min", "after 60000", "at 60,000 ms")
      board(125000, "2 min", "after 55000", "at 125,000 ms")
      -- No next minute: from 99 min on the timer is cancelled and not armed.
      board(5940000, "99 min", "cancel", "at 99 min")
      board(5999000, "99 min", "cancel", "at the cap")
      board(9000000, "99 min", "cancel", "past the cap")
      -- The saved time counts: t plus the clock since the last move, capped.
      local late = fresh(state)
      late.t = 5900000
      time.now = 600000
      local got, shown = drawn_header(calls, late, ui)
      eq(got, "cancel")
      eq(shown and shown.str, "99 min", "t + 600,000 ms")
      late.t = 59000
      time.now = 0
      got, shown = drawn_header(calls, late, ui)
      eq(got, "after 1000")
      eq(shown and shown.str, "0 min", "t 59,000 ms at the origin")

      -- A pause: the clock stands still between two draws, so the text and the delay are those of the unmoved clock; a
      -- timer that fires early draws the same frame and arms for the same boundary, and changes nothing in ui.
      time.now = 45000
      local first_calls, first = drawn_header(calls, s, ui)
      local before = {}
      for k, v in pairs(ui) do before[k] = v end
      eq(game.input(s, 1, ui, { kind = "timer" }), nil)
      local count = 0
      for k, v in pairs(ui) do
        eq(before[k], v, "ui." .. k .. " after a timer event")
        count = count + 1
      end
      for _ in pairs(before) do count = count - 1 end
      eq(count, 0, "keys of ui after a timer event")
      local again_calls, again = drawn_header(calls, s, ui)
      eq(again_calls, first_calls, "the timer calls of the unmoved clock")
      eq(again.str, first.str, "the text of the unmoved clock")
      eq(first_calls, "after 15000")

      -- Not played: TIMER off, the MENU, HOW TO PLAY, and the Solved screen cancel the timer, arm none, and draw no
      -- time.
      local function silent(u, st, what)
        time.now = 45000
        local got2, shown2, texts = drawn_header(calls, st, u)
        eq(got2, "cancel", what .. ": the timer calls")
        eq(shown2, nil, what .. ": a time is drawn")
        for _, t in ipairs(texts) do
          assert(not t.str:find(MINUTES), what .. ": '" .. t.str .. "' is a time")
        end
      end
      ui.timer = false
      silent(ui, s, "TIMER off")
      ui.timer, ui.panel = true, "menu"
      silent(ui, s, "the MENU")
      ui.panel = "help"
      silent(ui, s, "HOW TO PLAY")
      ui.panel = nil
      local solved = fresh(state)
      solved.v = answer(solved)
      solved.t = 70000
      local over_ui = {}
      time.now = 45000
      game.input(solved, 1, over_ui, { kind = "timer" })
      eq(over_ui.timer, true)
      silent(over_ui, solved, "the Solved screen")

      -- The header's left text and the time never overlap: the longest left text that leaves 16 px beside the time
      -- shows it (and arms the next minute), one character more hides it and arms nothing (a timer would only redraw
      -- the same frame), and the left text is drawn as it was either way.
      time.now = 61000
      local width = L.size - ch.text_width("1 min", "medium")
      local n = 0
      while ch.text_width(string.rep("W", n + 1), "medium") + 16 <= width do n = n + 1 end
      assert(n >= 1 and n < 200, "the widest left text that leaves room: " .. n)
      for _, case in ipairs({ { n, true }, { n + 1, false } }) do
        ui.msg = string.rep("W", case[1])
        local got3, shown3, texts = drawn_header(calls, s, ui)
        eq(shown3 ~= nil, case[2], "a left text of " .. case[1] .. " characters shows the time")
        local lefts = 0
        for _, t in ipairs(texts) do
          if t.str == ui.msg then
            lefts = lefts + 1
            eq(t.x, L.x)
          end
        end
        eq(lefts, 1, "the left text, once")
        eq(got3, case[2] and "after 59000" or "cancel", "the timer calls with " .. case[1] .. " characters")
      end
      -- The left text short again: the time and the timer come back.
      ui.msg = nil
      local back_calls, back = drawn_header(calls, s, ui)
      eq(back and back.str, "1 min", "the time with the left text short again")
      eq(back_calls, "after 59000", "the timer calls with the left text short again")
    end)

    -- The toggle: row 7 flips TIMER and writes it to the store, a fresh ui reads it back, and a store without the key,
    -- or with a non-boolean one, reads as on.
    with_timer(function(calls)
      ch.store.set({})
      local s, ui = fresh(state), {}
      tap(s, ui, 0, 0)
      eq(ui.timer, true)
      eq(on_menu(s, ui, 7), nil)
      eq(ui.timer, false)
      eq(ch.store.get().timer, false)
      local _, _, texts = drawn_header(calls, s, ui)
      local label
      for _, t in ipairs(texts) do
        if t.str:find("^TIMER") then label = t.str end
      end
      eq(label, "TIMER: OFF")
      local again = {}
      tap(s, again, 0, 0)
      eq(again.timer, false)
      eq(on_menu(s, ui, 7), nil)
      eq(ui.timer, true)
      eq(ch.store.get().timer, true)
      _, _, texts = drawn_header(calls, s, ui)
      label = nil
      for _, t in ipairs(texts) do
        if t.str:find("^TIMER") then label = t.str end
      end
      eq(label, "TIMER: ON")
      eq(ui.panel, "menu")
      for _, case in ipairs({ { {}, true }, { { timer = false }, false }, { { timer = true }, true },
                              { { timer = "yes" }, true }, { { timer = 0 }, true } }) do
        ch.store.set(case[1])
        local read = {}
        tap(s, read, 0, 0)
        eq(read.timer, case[2], "the store " .. tostring(case[1].timer))
      end
      ch.store.set({})
    end)

    -- The MENU's nine rows: each at least 44 px, and a tap at the row's centre and at its far corner reaches the
    -- row's action.
    eq(layout.ROWS, 9)
    local actions = {
      function(move, ui) eq(move[1], "h") assert(ui.sel and ui.msg) end,
      function(move) eq(move[1], "f") end,
      function(move, ui) eq(move, nil) assert(ui.check and ui.msg) end,
      function(move, ui) eq(move, nil) eq(ui.rem, false) end,
      function(move, ui) eq(move, nil) eq(ui.shade, false) end,
      function(move, ui) eq(move, nil) eq(ui.dots, true) end,
      function(move, ui) eq(move, nil) eq(ui.timer, false) end,
      function(move, ui) eq(move, nil) eq(ui.panel, "help") end,
      function(move, ui) eq(move, nil) eq(ui.panel, nil) end,
    }
    for i = 1, layout.ROWS do
      local x, y, w, h = layout.menu_rect(i)
      assert(h >= 44 and w >= 44, "MENU row " .. i .. " is under 44 px")
      for _, at in ipairs({ { layout.centre(x, y, w, h) }, { x + w - 1, y + h - 1 }, { x, y } }) do
        ch.store.set({})
        local s, ui = fresh(state), {}
        tap(s, ui, 0, 0)
        ui.panel = "menu"
        actions[i](tap(s, ui, at[1], at[2]), ui)
      end
    end
    ch.store.set({})
  end)
end

return interaction
