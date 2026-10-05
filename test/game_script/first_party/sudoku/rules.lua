-- rules.lua: the game's rules pinned one by one, each on the grid a round dealt (its `steps(state)` calls them, so every
-- rule is checked beside the round that plays it through taps). A failure is an error that names its line. They live
-- here, in the round's own VM, and not in checks.lua: that VM holds the solver, the counter, and the bank (most
-- of the 256 KB the sandbox allows, with a heap the allocator fragments), and had no room left for them.
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
-- hands each image call to `on_image` instead of keeping the calls (a round's VM has no room for a frame's worth of
-- tables). It is more permissive than the device: it clips nothing and does not look an image up, so a name that is not
-- in the package passes it (the games check's rounds, drawing the real ch.gfx, fault on one).
local function record(f, on_image)
  local count = 0
  local gfx = setmetatable({}, {
    __index = function(_, name)
      return function(...)
        count = count + 1
        if name == "image" and on_image then on_image(...) end
      end
    end,
  })
  local real = ch.gfx
  ch.gfx = gfx
  local ok, err = pcall(f)
  ch.gfx = real
  if not ok then error(err, 0) end
  return count
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
  local commands = record(function() game.draw(s, 1, ui) end, on_image)
  assert(commands <= 2048, "the worst frame is " .. commands .. " commands")
  local shown = 0
  for _, c in ipairs(empty) do
    for d = 1, 9 do
      if cand[c] & (1 << d - 1) ~= 0 then shown = shown + 1 end
    end
  end
  eq(images, digits and shown or 0, "marks drawn")
end

return rules
