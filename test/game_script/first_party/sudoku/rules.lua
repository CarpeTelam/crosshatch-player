-- rules.lua: the game's rules pinned one by one, each on the grid a round dealt (its `steps(state)` calls them, so every
-- rule is checked beside the round that plays it through taps). A failure is an error that names its line. They live
-- here, in the round's own VM, and not in checks.lua: that VM holds the solver, the counter, and the bank (most
-- of the 256 KB the sandbox allows, with a heap the allocator fragments), and had no room left for them. So do the
-- pins on what the draw marks on the board (record, watch, expect_marks, rules.frame, rules.draw_marks): the highlight
-- fills, the pad counts, the strokes, and the full refresh, read through a recording `ch.gfx`.
local game = require("main")
local grid = require("grid")
local layout = require("layout")
local solver = require("solver")

local rules = {}

local CLUE, NOMARK, ZEROS = "Clues cannot be changed", "That mark is not possible", string.rep("\0", 162)

local function eq(got, want, what)
  if got ~= want then error((what or "") .. " got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- The dealt grid with nothing written: a state of the round's level and puzzle.
local function fresh(state) return { l = state.l, v = state.v, n = ZEROS, u = "", t = 0 } end

local function empties(s)
  local out = {}
  for c = 1, 81 do
    if s.v:byte(c) == 48 then out[#out + 1] = c end
  end
  return out
end

local solutions = {}
local function answer(s)
  local clues = s.v:gsub("%l", "0")
  solutions[clues] = solutions[clues] or assert(solver.answer(clues))
  return solutions[clues]
end

local function digit_at(s, c) return answer(s):byte(c) - 48 end

local function letter(d) return string.char(96 + d) end

-- A move applied (dt 0): the state, or an error.
local function mv(s, ...)
  local move = { ... }
  move[#move + 1] = 0
  return assert(game.apply(s, 1, move))
end

local function mask(s, c) return (string.unpack("<I2", s.n, 2 * c - 1)) end

-- A digit cell c can take.
local function candidate(s, c)
  local m = grid.candidates(s.v)[c]
  for d = 1, 9 do
    if m & (1 << d - 1) ~= 0 then return d end
  end
end

local function rejects(s, move, reason)
  local before = s.v .. s.n .. s.u .. (s.z or "") .. s.t .. tostring(s.h)
  local result, got = game.apply(s, 1, move)
  eq(result, nil)
  eq(got, reason)
  assert(#got <= 64)
  eq(s.v .. s.n .. s.u .. (s.z or "") .. s.t .. tostring(s.h), before)
end

-- Taps on the game's input, with ui kept by the caller; the first call starts ui.
local function tap(s, ui, x, y) return game.input(s, 1, ui, { kind = "tap", x = x, y = y }) end
local function on_cell(s, ui, c) return tap(s, ui, layout.centre(layout.cell_rect(c))) end
local function on_key(s, ui, d) return tap(s, ui, layout.centre(layout.key_rect(d))) end
local function on_rail(s, ui, i) return tap(s, ui, layout.centre(layout.rail_rect(i))) end
local function on_menu(s, ui, i)
  ui.panel = "menu"
  return tap(s, ui, layout.centre(layout.menu_rect(i)))
end

-- A clue, a bad move, a mark that is not possible, and what has nothing to do are rejected, and the clock adds up.
function rules.apply(state)
  local s = fresh(state)
  local clue, c = s.v:find("[1-9]"), empties(s)[1]
  for _, kind in ipairs({ { "w", clue, 5, 0 }, { "n", clue, 5, 0 }, { "e", clue, 0 } }) do rejects(s, kind, CLUE) end
  for _, bad in ipairs({
    5, "w", {}, { "x", 1, 0 }, { "w", 1, 1 }, { "w", c, 1, 0, 0 }, { "w", 0, 1, 0 }, { "w", 82, 1, 0 }, { "w", 1.5, 1, 0 },
    { "w", "1", 1, 0 }, { "w", c, 0, 0 }, { "w", c, 10, 0 }, { "w", c, 1.5, 0 }, { "w", c, 1, -1 }, { "w", c, 1, 1.5 },
    { "w", c, 1, "0" }, { "u" }, { "u", -1 }, { "u", 0.5 }, { "e", 0, 0 }, { "f" }, { "h", -3 },
  }) do
    rejects(s, bad, "Bad move")
  end
  rejects(s, { "u", 0 }, "Nothing to undo")
  rejects(s, { "e", c, 0 }, "Nothing to erase")
  mv(s, "w", c, 3)
  rejects(s, { "n", c, 4, 0 }, NOMARK)
  local c2, held = empties(s)[2], nil
  for d = 9, 1, -1 do
    if grid.peer_has(s.v, c2, d) then held = d end
  end
  assert(held, "c2 has no peer digit to rule a mark out")
  rejects(s, { "n", c2, held, 0 }, NOMARK)
  mv(s, "f")
  rejects(s, { "f", 0 }, "Nothing to fill")
  s.t = 40
  mv(s, "e", c)
  assert(game.apply(s, 1, { "w", c, 3, math.maxinteger }))
  eq(s.t, 5999000)
  local solved = fresh(state)
  solved.v = answer(solved)
  eq(game.status(solved).over, true)
  rejects(solved, { "w", 1, 1, 0 }, "The grid is solved")
  rejects(solved, { "u", 0 }, "The grid is solved")
end

-- UNDO takes back a cell, a clear, an erase, and a note, restoring the digit and the notes.
function rules.undo_cell(state)
  local s = fresh(state)
  local a, b = empties(s)[1], empties(s)[2]
  local da = candidate(s, a)
  mv(s, "n", a, da)
  mv(s, "w", a, da)
  eq(s.v:byte(a), 96 + da)
  eq(mask(s, a), 0)
  mv(s, "w", a, da)
  eq(s.v:byte(a), 48)
  mv(s, "u")
  eq(s.v:byte(a), 96 + da)
  mv(s, "e", a)
  mv(s, "u")
  mv(s, "u")
  eq(s.v:byte(a), 48)
  eq(mask(s, a), 1 << da - 1)
  mv(s, "u")
  eq(mask(s, a), 0)
  eq(#s.u, 0)
  mv(s, "n", b, candidate(s, b))
  mv(s, "e", b)
  eq(mask(s, b), 0)
  mv(s, "u")
  assert(mask(s, b) ~= 0)
end

-- UNDO takes back a whole FILL NOTES as one step, then goes on into the steps before it; a second FILL NOTES drops
-- the older fill and every step before it, and keeps the steps after it.
function rules.undo_fill(state)
  local s = fresh(state)
  local a, b = empties(s)[1], empties(s)[2]
  mv(s, "w", a, digit_at(s, a))
  mv(s, "f")
  eq(s.z, ZEROS)
  mv(s, "w", b, digit_at(s, b))
  mv(s, "u")
  eq(s.v:byte(b), 48)
  assert(s.z)
  mv(s, "u")
  eq(s.n, ZEROS)
  eq(s.z, nil)
  mv(s, "u")
  eq(s.v:byte(a), 48)
  eq(#s.u, 0)

  local s = fresh(state)
  local a, b = empties(s)[1], empties(s)[2]
  mv(s, "w", a, digit_at(s, a))
  mv(s, "f")
  mv(s, "w", b, digit_at(s, b))
  local before = s.n
  mv(s, "f")
  eq(#s.u, 6)
  eq(s.z, before)
  mv(s, "u")
  eq(s.n, before)
  mv(s, "u")
  eq(s.v:byte(b), 48)
  eq(s.v:byte(a), 96 + digit_at(s, a))
  rejects(s, { "u", 0 }, "Nothing to undo")
end

-- The ring holds 48 steps; the oldest goes, and a fill that is the oldest goes with its copy.
function rules.ring(state)
  local s = fresh(state)
  local c = empties(s)[1]
  local d = candidate(s, c)
  for _ = 1, 60 do mv(s, "n", c, d) end
  eq(#s.u, 3 * game.RING)
  for _ = 1, game.RING do mv(s, "u") end
  eq(mask(s, c), 0)
  rejects(s, { "u", 0 }, "Nothing to undo")
  s = fresh(state)
  mv(s, "f")
  for _ = 2, game.RING do mv(s, "n", c, d) end
  eq(#s.u, 3 * game.RING)
  assert(s.z)
  eq(#s.v + #s.n + #s.u + #s.z, 81 + 162 + 144 + 162)
  mv(s, "n", c, d)
  eq(#s.u, 3 * game.RING)
  eq(s.z, nil)
  local notes = s.n
  for _ = 1, game.RING do mv(s, "u") end
  eq(s.n, notes)
end

-- A digit that repeats in a row, column, or box clashes, and a full grid with a clash is not over.
function rules.clash(state)
  local s = fresh(state)
  eq(grid.clashes(s.v), nil)
  local v = answer(s)
  eq(game.status({ v = v }).winners[1], 1)
  local row = v:sub(1, 1) .. v:sub(1, 1) .. v:sub(3)
  local clash = grid.clashes(row)
  assert(clash[1] and clash[2])
  eq(game.status({ v = row }).over, nil)
  local boxed = grid.clashes("1" .. string.rep("0", 9) .. "1" .. string.rep("0", 70))
  assert(boxed[1] and boxed[11] and not boxed[2])
  local column = grid.clashes("e" .. string.rep("0", 17) .. "e" .. string.rep("0", 62))
  assert(column[1] and column[19])
  eq(game.status({ v = string.rep("0", 81) }).turn, 1)
end

-- Taps select, write, clear, note, focus, and erase as the interaction table says.
function rules.taps(state)
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

-- HINT names a wrong digit before any rule, CHECK strikes it, and an edit ends both marks.
function rules.hint(state)
  local s, ui = fresh(state), {}
  local c = empties(s)[3]
  local right = digit_at(s, c)
  s.v = s.v:sub(1, c - 1) .. letter(right % 9 + 1) .. s.v:sub(c + 1)
  tap(s, ui, 0, 0)
  eq(on_menu(s, ui, 1)[1], "h")
  eq(ui.sel, c)
  eq(ui.msg, "Wrong digit")
  eq(on_menu(s, ui, 3), nil)
  assert(ui.check[c])
  eq(ui.msg, "1 wrong digit")
  on_cell(s, ui, c)
  on_cell(s, ui, c)
  assert(ui.check)
  local fix = on_key(s, ui, right)
  eq(fix[1], "w")
  eq(ui.check, nil)
  eq(ui.msg, nil)
  game.apply(s, 1, fix)
  on_menu(s, ui, 3)
  eq(ui.msg, "All correct")
  on_menu(s, ui, 1)
  assert(ui.msg ~= "Wrong digit" and s.v:byte(ui.sel) == 48 and not ui.msg:find("%d"))
end

-- The toggles live in ch.store with defaults, and a best time is kept per band unless HINT was used.
function rules.store(state)
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

-- The layout's targets are at least 44 px, on the canvas, and exact inverses of their rectangles.
function rules.layout()
  local L, w, h = layout.get(), ch.screen.w, ch.screen.h
  -- Each of n targets: at least 44 px, on the canvas, and at() gives it back along its top and left edges and at its
  -- far corner.
  local function tiles(n, rect, at, what)
    for i = 1, n do
      local x, y, rw, rh = rect(i)
      assert(rw >= 44 and rh >= 44 and x >= 0 and y >= 0 and x + rw <= w and y + rh <= h, what .. i)
      for j = 0, rw - 1 do eq(at(x + j, y), i, what .. i) end
      for j = 0, rh - 1 do eq(at(x, y + j), i, what .. i) end
      eq(at(x + rw - 1, y + rh - 1), i, what .. i)
    end
  end
  tiles(81, layout.cell_rect, layout.cell_at, "cell ")
  tiles(9, layout.key_rect, layout.key_at, "key ")
  tiles(layout.RAIL, layout.rail_rect, layout.rail_at, "rail ")
  tiles(layout.ROWS, layout.menu_rect, layout.menu_at, "row ")
  eq(layout.cell_at(L.x - 1, L.y), nil)
  eq(layout.cell_at(L.x, L.y + L.size), nil)
  eq(layout.key_at(L.x + 3 * L.kw, L.pad_y), nil)
  eq(layout.key_at(L.x, L.pad_y + 3 * L.kh), nil)
  eq(layout.key_at(L.x - 1, L.pad_y), nil)
  eq(layout.key_at(L.x, L.pad_y - 1), nil)
  eq(layout.key_at(L.x + L.kw, L.pad_y), 2)
  eq(layout.key_at(L.x, L.pad_y + L.kh), 4)
  local _, y1, _, h1 = layout.rail_rect(1)
  local _, y4, _, h4 = layout.rail_rect(4)
  eq(y4 + h4 - y1, 3 * L.kh)
  assert(L.rail_x >= L.x + 3 * L.kw and L.pad_y >= L.y + L.size)
  eq(layout.rail_at(L.rail_x - 1, L.pad_y), nil)
  eq(layout.menu_at(L.row_x, L.row_y - 1), nil)
end

-- The clock: moves carry the time since the move before, and a refused move's time is not lost.
local clock = 0
local function with_clock(f)
  local real = ch.time
  ch.time = { ms = function() return clock end }
  local ok, err = pcall(f)
  ch.time = real
  if not ok then error(err, 0) end
end

function rules.clock(state)
  with_clock(function()
    local s, ui = fresh(state), {}
    local clue, cell = s.v:find("[1-9]"), empties(s)[1]
    clock = 0
    tap(s, ui, 0, 0)
    clock = 5000
    on_cell(s, ui, clue)
    local marks = { [3] = true }
    ui.msg, ui.check = "1 wrong digit", marks
    local erase = on_rail(s, ui, 2)
    eq(erase[1], "e")
    eq(erase[3], 5000)
    game.input(s, 1, ui, { kind = "rejected", reason = CLUE })
    eq(ui.note, CLUE)
    eq(ui.msg, "1 wrong digit") -- a refused move leaves HINT's and CHECK's marks, an accepted edit (below) ends them
    eq(ui.check, marks)
    clock = 7000
    on_cell(s, ui, cell)
    local move = on_key(s, ui, candidate(s, cell))
    eq(move[4], 7000)
    eq(game.apply(s, 1, move).t, 7000)
    eq(ui.msg, nil)
    eq(ui.check, nil)
  end)
end

-- A new puzzle on a reused ui starts it over: selection, focus, pencil, panel, marks, the best time, the clock origin,
-- and the toggles read again from the store; the same puzzle leaves it alone.
function rules.reset(state)
  with_clock(function()
    local s, ui = fresh(state), {}
    ch.store.set({ rem = false })
    clock = 100
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
    clock = 4000
    game.input(other, 1, ui, { kind = "timer" })
    assert(ui.sel == nil and ui.foc == nil and ui.pencil == nil and ui.panel == nil and ui.check == nil and ui.best == nil)
    eq(ui.last, 4000)
    assert(ui.rem and not ui.shade and ui.dots)
    ch.store.set({})
  end)
end

-- The commands of a frame, counted. TEST DOUBLE for the engine's ch.gfx (which faults outside a draw call, keeps a frame
-- to 2,048 commands, and checks an image's name against the package): it counts every call as one command, as the
-- engine does (board.draw_grid says 28 commands, and the first check below pins that this double counts it so), and
-- hands each image call to `on_image` and every call, image included, to `on_call(name, ...)` instead of keeping the
-- calls (a round's VM has no room for a frame's worth of tables). It takes its function names from the real ch.gfx's own
-- keys, so a name the engine lacks (a misspelled call in a draw) raises here too. It is more permissive than the device:
-- it clips nothing, takes any arguments, and does not look an image up, so a name that is not in the package passes it
-- (the games check's rounds, drawing the real ch.gfx, fault on one). A failed draw leaves no recorder installed: ch.gfx is
-- restored and the error raised again.
local function record(f, on_image, on_call)
  local count = 0
  local gfx = {}
  for name, value in pairs(ch.gfx) do
    if type(value) == "function" then
      gfx[name] = function(...)
        count = count + 1
        if name == "image" and on_image then on_image(...) end
        if on_call then on_call(name, ...) end
      end
    end
  end
  local real = ch.gfx
  ch.gfx = gfx
  local ok, err = pcall(f)
  ch.gfx = real
  if not ok then error(err, 0) end
  return count
end

-- What a frame draws beyond its text, gathered as record's `on_call` sees the commands: the cells filled `light` (by
-- cell), the pad's remaining counts (by digit), the diagonal strokes (by cell, counting lines: a stroke is eight), and
-- the full refreshes. A command that is this game's but outside its place (a fill that is no cell's, a count that is
-- not on a key) is an error here.
local function watch()
  local seen = { light = {}, count = {}, rising = {}, falling = {}, full = 0 }
  local at = {}
  for d = 1, 9 do
    local x, y, w = layout.key_rect(d)
    at[(x + w - 8) .. "," .. (y + 4)] = d
  end
  local function on_call(name, a, b, c, d, e, f)
    if name == "rect" and e == "light" then
      local cell = assert(layout.cell_at(a + 1, b + 1), "a light fill outside the grid")
      local x, y, w, h = layout.cell_rect(cell)
      assert(f == true and a == x + 1 and b == y + 1 and c == w - 1 and d == h - 1, "a light fill that is no cell's")
      seen.light[cell] = (seen.light[cell] or 0) + 1
    elseif name == "text" and d == "small" and f == "right" then
      local key = assert(at[a .. "," .. b], "a right-aligned count that is on no key")
      assert(seen.count[key] == nil, "two counts on key " .. key)
      seen.count[key] = c
    elseif name == "line" and a ~= c and b ~= d then
      local cell = assert(layout.cell_at((a + c) // 2, (b + d) // 2), "a stroke outside the grid")
      local into = b > d and seen.rising or seen.falling
      into[cell] = (into[cell] or 0) + 1
    elseif name == "refresh" and a == "full" then
      seen.full = seen.full + 1
    end
  end
  return seen, on_call
end

-- The digit of a cell's byte and whether it is a clue, from the notation alone ("1".."9" a clue, "a".."i" the player's).
local function digit_of(b)
  if b >= 49 and b <= 57 then return b - 48, true end
  if b >= 97 and b <= 105 then return b - 96, false end
end

-- The row, column, and box (0 to 8 each) of cell c, by arithmetic: no grid.lua.
local function units_of(c)
  local row, col = (c - 1) // 9, (c - 1) % 9
  return row, col, row // 3 * 3 + col // 3
end

-- The cells whose digit repeats in a row, column, or box, as a set.
local function clashing(v)
  local out = {}
  for unit = 1, 3 do
    local homes = {}
    for c = 1, 81 do
      local d = digit_of(v:byte(c))
      if d then
        local key = select(unit, units_of(c)) * 10 + d
        homes[key] = homes[key] or {}
        homes[key][#homes[key] + 1] = c
      end
    end
    for _, cells in pairs(homes) do
      if #cells > 1 then
        for _, c in ipairs(cells) do out[c] = true end
      end
    end
  end
  return out
end

-- Pins what a frame of state `s` and ui draws to what the interaction table says, cell by cell: SHADE PEERS fills `light`
-- exactly the selected cell's row, column, and box cells that are neither a clue (dark ground) nor a copy of the focused
-- digit (black ground), once each, and nothing when it is off or nothing is selected; SHOW REMAINING puts nine counts on
-- the keys (nine less the digit's cells, clues and the player's, never below 0) and none when off; every clashing cell
-- has a rising stroke and every CHECK-marked cell a falling one (eight lines each), and no other cell has either.
local function expect_marks(seen, s, ui)
  local clash = clashing(s.v)
  local sel_row, sel_col, sel_box
  if ui.sel then sel_row, sel_col, sel_box = units_of(ui.sel) end
  local left = { 9, 9, 9, 9, 9, 9, 9, 9, 9 }
  for c = 1, 81 do
    local d, clue = digit_of(s.v:byte(c))
    local shaded = false
    if ui.shade and ui.sel and not clue and not (d and d == ui.foc) then
      local row, col, box = units_of(c)
      shaded = row == sel_row or col == sel_col or box == sel_box
    end
    eq(seen.light[c], shaded and 1 or nil, "light fills of cell " .. c)
    eq(seen.rising[c], clash[c] and 8 or nil, "clash strokes of cell " .. c)
    eq(seen.falling[c], ui.check and ui.check[c] and 8 or nil, "CHECK strokes of cell " .. c)
    if d then left[d] = left[d] - 1 end
  end
  for d = 1, 9 do eq(seen.count[d], ui.rem and tostring(math.max(left[d], 0)) or nil, "the count on key " .. d) end
end

-- The worst frame of the dealt grid: every empty cell holds all nine notes (the ones a neighbour's digit rules out stay
-- hidden), SHADE PEERS, SHOW REMAINING, and a CHECK stroke on every cell are on, a digit is focused, and a cell is
-- selected. It stays inside the engine's 2,048 commands; with notes as images (`digits`), each shown mark is one image of
-- the focus colour its digit calls for, set inside its cell clear of the 3 px block lines (an image keeps a one-pixel
-- paper margin, so 2 px from each cell edge keeps the paper off the lines); with dots, no image is drawn.
function rules.frame(state, digits)
  eq(record(function() require("board").draw_grid(layout.get()) end), 28)
  local s = fresh(state)
  local cand, notes, empty = grid.candidates(s.v), {}, empties(s)
  for c = 1, 81 do
    notes[c] = s.v:byte(c) == 48 and 0x1FF or 0
  end
  s.n = string.pack(grid.FMT, table.unpack(notes))
  local ui = {}
  game.input(s, 1, ui, { kind = "timer" })
  ui.dots, ui.shade, ui.rem, ui.foc, ui.sel, ui.check = not digits, true, true, 5, empty[1], {}
  for c = 1, 81 do
    ui.check[c] = true
  end
  local L, images = layout.get(), 0
  local function on_image(name, x, y, color)
    images = images + 1
    local set, d = name:match("^note_([gb])([1-9])$")
    assert(set and color == "black", "an image is a note image: " .. tostring(name))
    eq(set == "b", tonumber(d) == ui.foc, name .. " focus colour")
    local cx, cy = layout.cell_rect(layout.cell_at(x + 5, y + 7))
    local dx, dy = x - cx, y - cy
    assert(dx >= 2 and dx + 10 <= L.cell - 1 and dy >= 2 and dy + 14 <= L.cell - 1, name .. " at " .. dx .. ", " .. dy)
  end
  local seen, on_call = watch()
  local commands = record(function() game.draw(s, 1, ui) end, on_image, on_call)
  assert(commands <= 2048, "the worst frame is " .. commands .. " commands")
  expect_marks(seen, s, ui)
  eq(seen.full, 1, "full refreshes of the first frame")
  local shown = 0
  for _, c in ipairs(empty) do
    for d = 1, 9 do
      if cand[c] & (1 << d - 1) ~= 0 then shown = shown + 1 end
    end
  end
  eq(images, digits and shown or 0, "marks drawn")
end

-- The marks `game.draw` of state `st` with `u` makes, as watch gathers them.
local function marks_of(st, u)
  local seen, on_call = watch()
  record(function() game.draw(st, 1, u) end, nil, on_call)
  return seen
end

-- The marks a draw adds to the board, each on a grid the test sets up and checked against expect_marks's own arithmetic:
-- SHADE PEERS and the focus ground (a legal digit written in a peer of the selected cell, which is then not shaded), SHOW
-- REMAINING with a digit past its nine, the strokes of a clash (rising) and of CHECK (falling) on cells that overlap, the
-- toggles' off states, and the full refresh: once on the first frame and on each change between the board, the MENU, the
-- HOW TO PLAY page, and the end screen, never on a tap of the board.
function rules.draw_marks(state)
  -- The recorder: a misspelled call raises naming it, ch.gfx is the real table again after any error, the error comes
  -- back as it was, and on_call sees each command.
  local real = ch.gfx
  local ok, err = pcall(record, function() ch.gfx.rectt(0, 0, 1, 1, "black") end)
  assert(not ok and tostring(err):find("rectt", 1, true), "a misspelled ch.gfx call does not raise: " .. tostring(err))
  eq(ch.gfx, real, "ch.gfx after a misspelled call")
  ok, err = pcall(record, function() ch.gfx.clear("white") error("boom", 0) end)
  eq(ok, false, "an error in the drawing function")
  eq(err, "boom", "the error is raised again as it was")
  eq(ch.gfx, real, "ch.gfx after an error in the drawing function")
  local names = {}
  eq(record(function() ch.gfx.clear("white") ch.gfx.line(1, 2, 3, 4, "dark") end, nil,
    function(name) names[#names + 1] = name end), 2, "commands recorded")
  eq(table.concat(names, ","), "clear,line", "on_call sees each command in order")
  local s, ui = fresh(state), {}
  game.input(s, 1, ui, { kind = "timer" })
  local cells = empties(s)
  local sel = cells[1]
  local sel_row, sel_col, sel_box = units_of(sel)
  local peer
  for _, c in ipairs(cells) do
    local row, col, box = units_of(c)
    if c ~= sel and (row == sel_row or col == sel_col or box == sel_box) then
      peer = c
      break
    end
  end
  assert(peer, "the first empty cell has no empty peer")
  local foc = candidate(s, peer)
  mv(s, "w", peer, foc)
  ui.rem, ui.shade, ui.dots, ui.sel, ui.foc = true, true, true, sel, foc
  local seen = marks_of(s, ui)
  expect_marks(seen, s, ui)
  local clue_peers = 0
  for c = 1, 81 do
    local row, col, box = units_of(c)
    local _, clue = digit_of(s.v:byte(c))
    if clue and (row == sel_row or col == sel_col or box == sel_box) then clue_peers = clue_peers + 1 end
  end
  assert(next(seen.light) and clue_peers > 0 and not seen.light[peer], "SHADE PEERS has nothing to tell apart here")
  -- Off, nothing selected, nothing focused: the fills follow (and the counts and strokes stay as they are).
  ui.shade = false
  assert(next(marks_of(s, ui).light) == nil, "SHADE PEERS off still fills")
  expect_marks(marks_of(s, ui), s, ui)
  ui.shade, ui.sel = true, nil
  assert(next(marks_of(s, ui).light) == nil, "no selection still fills")
  expect_marks(marks_of(s, ui), s, ui)
  ui.sel, ui.foc = sel, nil
  expect_marks(marks_of(s, ui), s, ui)
  ui.foc = foc
  -- SHOW REMAINING: off shows no count; with a digit in every empty cell, digit 1 is past its nine and shows 0.
  ui.rem = false
  assert(next(marks_of(s, ui).count) == nil, "SHOW REMAINING off still counts")
  ui.rem = true
  local flooded = fresh(state)
  flooded.v = (s.v:gsub("0", "a"))
  seen = marks_of(flooded, ui)
  expect_marks(seen, flooded, ui)
  eq(seen.count[1], "0", "the count of a digit past its nine")

  -- Strokes: a clash (two equal digits in a row) and CHECK on overlapping cells.
  local by_row, c1, c2 = {}, nil, nil
  for _, c in ipairs(cells) do
    local row = (c - 1) // 9
    by_row[row] = by_row[row] or {}
    table.insert(by_row[row], c)
    if #by_row[row] == 2 and not c1 then c1, c2 = by_row[row][1], by_row[row][2] end
  end
  local twins = fresh(state)
  for _, c in ipairs({ c1, c2 }) do twins.v = twins.v:sub(1, c - 1) .. "b" .. twins.v:sub(c + 1) end
  local c3 = cells[#cells]
  ui.check = { [c3] = true, [c1] = true }
  seen = marks_of(twins, ui)
  expect_marks(seen, twins, ui)
  assert(seen.rising[c1] and seen.rising[c2], "the twins do not clash")
  assert(seen.falling[c1] and seen.falling[c3] and not seen.falling[c2], "CHECK strokes the cells it marks only")
  ui.check = nil
  seen = marks_of(fresh(state), ui)
  expect_marks(seen, fresh(state), ui)
  assert(next(seen.rising) == nil and next(seen.falling) == nil, "a dealt grid with no CHECK has a stroke")

  -- The full refresh, frame by frame through the taps that change the screen.
  local function refreshes(st, u) return marks_of(st, u).full end
  local u, t = {}, fresh(state)
  eq(refreshes(t, u), 1, "the first frame")
  eq(refreshes(t, u), 0, "the same frame again")
  on_cell(t, u, cells[2])
  eq(refreshes(t, u), 0, "a tap that selects a cell")
  on_key(t, u, 5)
  eq(refreshes(t, u), 0, "a tap on the pad")
  on_rail(t, u, 1)
  eq(refreshes(t, u), 0, "NOTES")
  on_rail(t, u, 4)
  eq(u.panel, "menu")
  eq(refreshes(t, u), 1, "the MENU opens")
  eq(refreshes(t, u), 0, "the MENU, drawn again")
  on_menu(t, u, 4)
  ch.store.set({}) -- the toggle wrote SHOW REMAINING to ch.store; put the defaults back for what runs after
  eq(refreshes(t, u), 0, "a toggle inside the MENU")
  on_menu(t, u, 7)
  eq(u.panel, "help")
  eq(refreshes(t, u), 1, "HOW TO PLAY opens")
  tap(t, u, 0, 0)
  eq(u.panel, nil)
  eq(refreshes(t, u), 1, "the page closes onto the board")
  on_rail(t, u, 4)
  eq(refreshes(t, u), 1, "the MENU opens again")
  on_menu(t, u, 8)
  eq(u.panel, nil)
  eq(refreshes(t, u), 1, "CLOSE returns to the board")
  local solved = fresh(state)
  solved.v = answer(solved)
  eq(refreshes(solved, u), 1, "the end screen")
  eq(refreshes(solved, u), 0, "the end screen, drawn again")
end

return rules
