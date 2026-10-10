-- The game's own checks (first_party/README.md, `checks.lua`): every puzzle in the bank (one solution, at the band it
-- is filed under), the costliest puzzle of each band through the game's calls (symmetry, HINT, CHECK, FILL NOTES), the
-- header of puzzles.lua, setup, and the installed note images. The game's rules (rejections, the undo ring, the clash
-- rule, the taps, the toggles and best times, the layout) are pinned by rules.lua and the modules beside it, which the
-- rounds call from their `steps` functions. This file's VM is a check VM, so it has the check's own heap and
-- instruction budget (host::CHECK_*, README, "The check VMs' limits"), not the device's 256 KB and 2,000,000; it still
-- holds the solver, the counter, and the bank, and keeps the shape it had when it shared the device's limits:
--   - the puzzles go in batches (CHUNKS), sized from the instructions each batch took, so none passes the budget of
--     one call (2,000,000: the device's, still the measure of what a game call may cost); regenerating the bank means
--     sizing them again, and the check that they cover it fails until then;
--   - the entries share one run function and a plan of integers, so an entry is a name and a pointer;
--   - the stack is grown early and kept (see deep).
-- The four calls on a band's costliest puzzle (COSTLY) are the ones that prove a game call fits the device's budget
-- through this VM's guard: each runs through within_device_budget, which raises when the call spent the device's
-- 2,000,000 instructions, counted on the same hook, though this VM's own budget is far above it.
-- A module that loads while another module is loading is the games check's own finding, not this file's: it fails
-- main's load that nests deeper than main plus one level, as the device refuses it (first_party/README.md, "Module
-- loading"), so this file loads main and what it needs plainly.
local game = require("main")
local grid = require("grid")
local layout = require("layout")
local counter = require("counter")
local solver = require("solver")
local puzzles = require("puzzles")

-- The independent counter recurses deep, so its Lua stack grows: it is grown after the modules are loaded and their
-- garbage collected, and each entry runs under a deep call (deep), where a collection finds the stack in use and leaves it
-- its size. (With the device's 256 KB heap the first-fit allocator could no longer find room to grow it later; a check VM's
-- heap has the room, and the shape stays.)
local function deep(n, f)
  if n == 0 then return f() end
  local r = deep(n - 1, f)
  return r
end

local BAND = { "Easy", "Medium", "Hard", "Expert" }
-- How many puzzles each batch of a band takes, in order; they add up to puzzles.count.
local CHUNKS = {
  { 6, 6, 6, 6, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3 },
  { 5, 5, 4, 5, 5, 5, 5, 5, 6, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 4 },
  { 3, 5, 4, 4, 5, 5, 4, 4, 4, 4, 4, 5, 5, 5, 4, 4, 4, 5, 5, 5, 5, 5, 2 },
  { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 4, 4, 3, 3, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4 },
}
local ZEROS = string.rep("\0", 162)

-- The entries share one run function, which takes the next of `plan` each time: the harness runs a game's checks once
-- each, in order, and an entry is then a name and a pointer, not a closure of its own. A plan item is a function, a
-- batch of bank puzzles packed as band << 16 | first << 8 | last, or a call on a band's costliest puzzle (1 << 24 | band
-- << 8 | which of COSTLY).
local list, plan, at = {}, {}, 0
local verify, costly
local DEPTH = 150
-- What one entry does: the next item of the plan.
local function entry()
  at = at + 1
  local item = plan[at]
  if type(item) == "function" then return item() end
  if item >> 24 ~= 0 then return costly(item >> 8 & 255, item & 255) end
  for i = item >> 8 & 255, item & 255 do verify(item >> 16, i) end
end
local function run() deep(DEPTH, entry) end
local function add(name, item)
  plan[#plan + 1] = item
  list[#list + 1] = { name = name, run = run }
end

local function eq(got, want, what)
  if got ~= want then error((what or "") .. " got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

local function new_state(band, i)
  local _, digits = puzzles.get(band or 1, i or 1)
  return { l = band or 1, v = digits, n = ZEROS, u = "", t = 0 }
end

-- A move applied (dt 0): the state, or an error.
local function mv(s, ...)
  local move = { ... }
  move[#move + 1] = 0
  return assert(game.apply(s, 1, move))
end

local function mask(s, c) return (string.unpack("<I2", s.n, 2 * c - 1)) end

-- A tap on the game's input, with ui kept by the caller; the first call starts ui. on_menu taps MENU row i.
local function tap(s, ui, x, y) return game.input(s, 1, ui, { kind = "tap", x = x, y = y }) end
local function on_menu(s, ui, i)
  ui.panel = "menu"
  return tap(s, ui, layout.centre(layout.menu_rect(i)))
end

-- The bank -----------------------------------------------------------------------------------------------------------

function verify(band, i)
  local hash, digits = puzzles.get(band, i)
  assert(#hash == 12 and hash:find("^%x+$") and #digits == 81 and digits:find("^%d+$"), hash)
  assert(not grid.clashes(digits), hash)
  local n, solution = counter.count(digits)
  eq(n, 1, hash)
  eq(solver.grade(digits), band, hash)
  eq(solver.answer(digits), solution, hash)
end

for band = 1, 4 do
  local from = 1
  for _, n in ipairs(CHUNKS[band]) do
    add(BAND[band] .. " " .. from .. "-" .. from + n - 1, band << 16 | from << 8 | from + n - 1)
    from = from + n
  end
end

add("the bank: batches cover it, header whole", function()
  assert(#puzzles.commit == 40 and puzzles.commit:find("^%x+$"))
  assert(puzzles.width == 93 and puzzles.cap > 0 and puzzles.cap <= 1000000)
  for band = 1, 4 do
    local total = 0
    for _, n in ipairs(CHUNKS[band]) do total = total + n end
    eq(total, puzzles.count[band], band)
    -- The records of a band are in hash order, so no hash repeats when each is above the one before.
    local last = ""
    for i = 1, total do
      local hash = puzzles.get(band, i)
      assert(hash > last, hash)
      last = hash
    end
    for _, i in ipairs(puzzles.costly[band]) do assert(i >= 1 and i <= total) end
  end
end)

-- The costliest puzzle of each band, through the calls the game makes on it --------------------------------------

local COSTLY = {
  { "symmetry", function(s, digits)
    assert(s.v ~= digits)
    eq(counter.count(s.v), 1)
    eq(solver.grade(s.v), s.l)
    for _ = 1, 2 do eq(solver.grade(game.symmetry(digits, math.random)), s.l) end
  end },
  { "HINT", function(s)
    local ui = {}
    tap(s, ui, 0, 0)
    eq(on_menu(s, ui, 1)[1], "h")
    eq(s.v:byte(ui.sel), 48)
    assert(ui.msg and ui.msg ~= "Wrong digit")
    eq(mv(s, "h").h, 1)
  end },
  { "CHECK", function(s)
    local ui = {}
    tap(s, ui, 0, 0)
    eq(on_menu(s, ui, 3), nil)
    eq(ui.msg, "All correct")
  end },
  { "FILL NOTES", function(s)
    mv(s, "f")
    local want = grid.candidates(s.v)
    for c = 1, 81 do eq(mask(s, c), want[c], c) end
    eq(#s.u, 3)
    mv(s, "u")
    eq(s.n, ZEROS)
  end },
}
function costly(band, kind)
  local index = puzzles.costly[band][1]
  local s, digits = new_state(band, index), select(2, puzzles.get(band, index))
  s.v = game.symmetry(digits, math.random)
  -- The call's cost is measured against the device's budget, not this VM's (within_device_budget, ScriptVm.h).
  within_device_budget(COSTLY[kind][2], s, digits)
end

for band = 1, 4 do
  for kind, row in ipairs(COSTLY) do add(BAND[band] .. ": " .. row[1], 1 << 24 | band << 8 | kind) end
end

-- The note images ------------------------------------------------------------------------------------------------------

-- The 27 installed note images (note_g1..9, note_b1..9, note_h1..9: make_note_images.py) are NOTE_W x NOTE_H, as
-- layout.note_tile places them, read from the package the installer wrote (host.image_size, ScriptVm.h). A swapped or
-- resized PNG is make_note_images.py --check's (a ctest of its own); this is what the game places them by.
add("the 27 note images are NOTE_W x NOTE_H", function()
  local seen = 0
  for _, set in ipairs({ "g", "b", "h" }) do
    for k = 1, 9 do
      local name = "note_" .. set .. k
      local w, h = host.image_size(name)
      eq(w, layout.NOTE_W, name .. " width")
      eq(h, layout.NOTE_H, name .. " height")
      seen = seen + 1
    end
  end
  eq(seen, 27)
end)

-- The launcher icon -----------------------------------------------------------------------------------------------------

-- The Games launcher picks the installed package's own icon for Sudoku's row (games/sudoku/icon.png, which the installer
-- converts to icon.bmp; tools/make_icon.py writes it and its --check is a ctest of its own), not the Crosshatch mark and not
-- a library icon. host.launcher_icon is GameRowIcon::choose over the installed game.
add("the launcher draws the package's own icon", function()
  local source = host.launcher_icon()
  eq(source, "package", "the launcher's icon source")
end)

-- Setup ----------------------------------------------------------------------------------------------------------------

add("setup: seeds, levels, symmetry sample", function()
  local function deal(level) return game.setup({ seats = 1, mode = "solo", api = 1, settings = { level = level } }) end
  math.randomseed(7)
  local a = deal("Easy")
  math.randomseed(7)
  eq(deal("Easy").v, a.v)
  math.randomseed(8)
  assert(deal("Easy").v ~= a.v)
  for band = 1, 4 do
    local s = deal(BAND[band])
    eq(s.l, band)
    assert(#s.v == 81 and s.v:find("^%d+$") and not grid.clashes(s.v) and s.n == ZEROS and s.u == "" and s.t == 0)
    eq(s.h, nil)
    eq(game.status(s).turn, 1)
  end
  eq(deal(nil).l, 1)
  -- The symmetry keeps a sample of puzzles valid, unique, and of the same clue count.
  for i = 20, 80, 20 do
    local _, digits = puzzles.get(1, i)
    local s = game.symmetry(digits, math.random)
    eq(counter.count(s), 1)
    eq(select(2, s:gsub("0", "")), select(2, digits:gsub("0", "")))
  end
end)

-- The symmetry really moves cells and transposes: with every permutation the identity, what is left of the deal is the
-- transposition (rand(n) = n gives the identity, and 2 for the flip), and without the flip nothing; random deals move
-- the empty cells, and both outcomes of the flip occur.
add("symmetry: permutes, transposes", function()
  local _, digits = puzzles.get(1, 3)
  local calls = 0
  local flipped = game.symmetry(digits, function(n)
    calls = calls + 1
    return n
  end)
  local transposed, total = {}, calls
  for r = 0, 8 do
    for c = 0, 8 do transposed[#transposed + 1] = digits:sub(c * 9 + r + 1, c * 9 + r + 1) end
  end
  eq(flipped, table.concat(transposed))
  calls = 0
  eq(game.symmetry(digits, function(n)
    calls = calls + 1
    return calls == total and 1 or n
  end), digits)
  local moved, seen = false, {}
  for seed = 1, 12 do
    math.randomseed(seed)
    local last
    local s = game.symmetry(digits, function(n)
      last = math.random(n)
      return last
    end)
    seen[last] = true
    if s:gsub("[1-9]", "x") ~= digits:gsub("[1-9]", "x") then moved = true end
  end
  assert(moved and seen[1] and seen[2])
end)

-- solver.hint: a pinned grid for each kind of answer (a hidden single by its unit, a naked single), and on bank
-- puzzles, from the clues and from the clues plus 12 correct digits, a cell that is empty, the counter's digit for it,
-- and a rung no harder than the puzzle's.
local PINS = {
  { "002000800005020100460000029130060052009080400000302000006070200700000008020519070", 30, 8, 2, 3 },
  { "020900000048000031000063020009407003003080200400105600030570000250000180000006050" },
}
add("solver.hint: pinned grids", function()
  local cell, digit, rung, unit = solver.hint(PINS[1][1])
  eq(cell, 30)
  eq(digit, 8)
  eq(rung, 2)
  eq(unit, 3)
  cell, digit, rung, unit = solver.hint(PINS[2][1])
  eq(cell, 15)
  eq(digit, 2)
  eq(rung, 1)
  eq(unit, nil)
end)

for band = 1, 4 do
  add(BAND[band] .. ": hints from the clues and with digits added", function()
    for _, i in ipairs({ 2, 51 }) do
      local _, g = puzzles.get(band, i)
      local n, solution = counter.count(g)
      eq(n, 1)
      local _, top = solver.grade(g)
      for _ = 1, 2 do
        local cell, digit, rung = solver.hint(g)
        assert(cell and g:byte(cell) == 48 and rung <= top, i)
        eq(digit, solution:byte(cell) - 48, i)
        local added = 0
        for c = 1, 81 do
          if added < 12 and g:byte(c) == 48 then
            g = g:sub(1, c - 1) .. solution:sub(c, c) .. g:sub(c + 1)
            added = added + 1
          end
        end
      end
    end
  end)
end

collectgarbage("collect")
deep(DEPTH, function() end)

return list
