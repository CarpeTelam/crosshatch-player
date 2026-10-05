-- interaction.lua: what taps do, how the toggles are kept, and what starts a ui over, pinned on the grid a round dealt
-- (a round's `steps(state)` calls them; rules.lua says why they live in a round's VM). Apart from rules.lua so that a
-- round that calls only these does not load that module's compiled code (first_party/README.md, "The check VMs' limits").
local game = require("main")
local pins = require("pins")
local eq, fresh, empties, answer, mv = pins.eq, pins.fresh, pins.empties, pins.answer, pins.mv
local candidate, rejects, tap, on_cell, on_key = pins.candidate, pins.rejects, pins.tap, pins.on_cell, pins.on_key
local on_rail, on_menu, with_clock, CLUE, ZEROS = pins.on_rail, pins.on_menu, pins.with_clock, pins.CLUE, pins.ZEROS
local time = pins.time

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

-- The toggles live in ch.store with defaults, and a best time is kept per band unless HINT was used.
function interaction.store(state)
  ch.store.set({})
  local s, ui = fresh(state), {}
  tap(s, ui, 0, 0)
  assert(ui.rem and ui.shade and ui.dots)
  on_menu(s, ui, 4)
  on_menu(s, ui, 6)
  eq(ui.panel, "menu")
  local stored = ch.store.get()
  assert(stored.rem == false and stored.dots == false and stored.shade == nil)
  local again = {}
  tap(s, again, 0, 0)
  assert(not again.rem and again.shade and not again.dots)
  ch.store.set({ best = 5, rem = "yes" })
  local odd = {}
  tap(s, odd, 0, 0)
  assert(odd.rem and odd.shade and odd.dots)
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
    ch.store.set({ rem = false })
    time.now = 100
    game.input(s, 1, ui, { kind = "timer" })
    assert(not ui.rem and ui.shade and ui.dots)
    eq(ui.last, 100)
    ui.sel, ui.foc, ui.pencil, ui.panel, ui.check, ui.best, ui.last = 5, 2, true, "menu", { [3] = true }, 1234, 777
    game.input(s, 1, ui, { kind = "timer" })
    assert(ui.sel == 5 and ui.foc == 2 and ui.pencil and ui.panel == "menu" and ui.check and ui.best == 1234)
    eq(ui.last, 777)
    local other = fresh(state)
    other.v = game.symmetry(state.v, math.random)
    assert(other.v ~= s.v)
    ch.store.set({ shade = false })
    time.now = 4000
    game.input(other, 1, ui, { kind = "timer" })
    assert(ui.sel == nil and ui.foc == nil and ui.pencil == nil and ui.panel == nil and ui.check == nil and ui.best == nil)
    eq(ui.last, 4000)
    assert(ui.rem and not ui.shade and ui.dots)
    ch.store.set({})
  end)
end

return interaction
