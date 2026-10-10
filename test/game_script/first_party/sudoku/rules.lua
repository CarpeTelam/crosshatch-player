-- rules.lua: the game's rules pinned one by one (a clue, a bad move, the undo ring, the clash rule, HINT and CHECK, the
-- layout's targets, the clock), each on the grid a round dealt. A round's `steps(state)` calls them, so every rule is
-- checked beside the round that plays it through taps. A failure is an error that names its line. They live here, in
-- the round's own VM, and not in checks.lua: that VM holds the solver, the counter, and the bank (most of the 256 KB
-- the sandbox allows, with a heap the allocator fragments), and had no room left for them. The pins on taps, the
-- toggles, and the reset are in interaction.lua, and the pins on what the draw marks on the board are in drawn.lua and
-- marks.lua: a round loads only the modules it calls.
local game = require("main")
local grid = require("grid")
local layout = require("layout")
local pins = require("pins")
local eq, fresh, empties, answer, digit_at = pins.eq, pins.fresh, pins.empties, pins.answer, pins.digit_at
local letter, mv, mask, candidate, rejects = pins.letter, pins.mv, pins.mask, pins.candidate, pins.rejects
local tap, on_cell, on_key, on_rail, on_menu = pins.tap, pins.on_cell, pins.on_key, pins.on_rail, pins.on_menu
local with_clock, CLUE, NOMARK, ZEROS = pins.with_clock, pins.CLUE, pins.NOMARK, pins.ZEROS
local time = pins.time

local rules = {}

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

-- A tap in the margin between the grid, the pad or the rail and the box's edge acts as the target beside it, through
-- game.input: the 8 px at the grid's sides, the pad's left and the 34 px under it, the rail's right and the 34 px under
-- it, each on the target's own rows or columns. Nothing else answers: the grid's top, the gap between the grid and the
-- pad (and between the pad and the rail), the corners, and the Sticky's 4 px outside the box. The exact lookups stay
-- exact (rules.layout), and over the box at most one of cell, key and rail answers.
function rules.margins(state)
  with_clock(function()
    local L = layout.get()
    local s = fresh(state)
    local c = empties(s)[1]
    mv(s, "w", c, 3) -- a digit of the player's own, so ERASE has something to erase and a key writes it
    local left, top = L.ox, L.oy
    local right, bottom = L.ox + 465, L.oy + 787
    local pad_end = L.x + 3 * L.kw -- the pad's columns L.x .. pad_end - 1
    local rail_end, pad_bottom = L.rail_x + L.rail_w, L.pad_y + 3 * L.kh
    -- What a tap at x, y does with the player's digit at c selected: the move and what the ui keeps.
    local function seen(x, y)
      local ui = {}
      on_cell(s, ui, c)
      local m = tap(s, ui, x, y)
      return table.concat({ tostring(m and m[1]), tostring(m and m[2]), tostring(m and m[3]), tostring(ui.sel),
                            tostring(ui.foc), tostring(ui.pencil), tostring(ui.panel) }, ",")
    end
    local nothing = seen(left, top)
    local function acts(x, y, cx, cy, what)
      local want = seen(cx, cy)
      assert(want ~= nothing, what .. ": its own tap does nothing")
      eq(seen(x, y), want, what .. " from " .. x .. "," .. y)
    end
    local function ignored(x, y, what) eq(seen(x, y), nothing, what .. " at " .. x .. "," .. y) end
    local function centre_of(rect, i) return layout.centre(rect(i)) end

    for r = 1, 9 do
      for _, dy in ipairs({ 0, L.cell // 2, L.cell - 1 }) do
        local y = L.y + (r - 1) * L.cell + dy
        for x = left, L.x - 1 do
          local cx, cy = centre_of(layout.cell_rect, (r - 1) * 9 + 1)
          acts(x, y, cx, cy, "the grid's left margin, row " .. r)
        end
        for x = L.x + L.size, right do
          local cx, cy = centre_of(layout.cell_rect, r * 9)
          acts(x, y, cx, cy, "the grid's right margin, row " .. r)
        end
      end
    end
    for row = 0, 2 do
      for _, dy in ipairs({ 0, L.kh // 2, L.kh - 1 }) do
        local y = L.pad_y + row * L.kh + dy
        for x = left, L.x - 1 do
          local cx, cy = centre_of(layout.key_rect, row * 3 + 1)
          acts(x, y, cx, cy, "the pad's left margin, row " .. row + 1)
        end
      end
    end
    for col = 0, 2 do
      for _, dx in ipairs({ 0, L.kw // 2, L.kw - 1 }) do
        local x = L.x + col * L.kw + dx
        for y = pad_bottom, bottom do
          local cx, cy = centre_of(layout.key_rect, 6 + col + 1)
          acts(x, y, cx, cy, "the pad's bottom margin, column " .. col + 1)
        end
      end
    end
    for i = 1, layout.RAIL do
      for _, dy in ipairs({ 0, L.bh // 2, L.bh - 1 }) do
        local y = L.pad_y + (i - 1) * L.bh + dy
        for x = rail_end, right do
          local cx, cy = centre_of(layout.rail_rect, i)
          acts(x, y, cx, cy, "the rail's right margin, button " .. i)
        end
      end
    end
    for _, x in ipairs({ L.rail_x, L.rail_x + L.rail_w // 2, rail_end - 1 }) do
      for y = L.pad_y + layout.RAIL * L.bh, bottom do
        local cx, cy = centre_of(layout.rail_rect, 4)
        acts(x, y, cx, cy, "the rail's bottom margin")
      end
    end

    -- No answer: the header, the grid's foot and the pad's head (all x), the gap between pad and rail, the corners,
    -- and the 4 px outside the box on the Sticky.
    for y = top, L.y - 1, 5 do
      for _, x in ipairs({ left, L.x, L.x + 200, L.x + L.size - 1, right }) do ignored(x, y, "the header") end
    end
    for y = L.y + L.size, L.pad_y - 1 do
      for _, x in ipairs({ left, L.x - 1, L.x, L.x + 200, L.x + L.size - 1, L.x + L.size, right }) do
        ignored(x, y, "the gap under the grid")
      end
    end
    for x = pad_end, L.rail_x - 1 do
      for _, y in ipairs({ L.pad_y, L.pad_y + L.kh, pad_bottom - 1, pad_bottom, bottom }) do
        ignored(x, y, "the gap beside the pad")
      end
    end
    for _, y in ipairs({ pad_bottom, bottom }) do
      for _, x in ipairs({ left, L.x - 1, rail_end, right }) do ignored(x, y, "the bottom corner") end
    end
    if L.ox > 0 then
      for _, x in ipairs({ left - 1, right + 1 }) do
        for _, y in ipairs({ L.y + 20, L.pad_y + 20, L.pad_y + 3 * L.bh }) do ignored(x, y, "outside the box") end
      end
    end

    -- Over the box at most one of the three answers, and the gaps' points answer none. Every 8th pixel, and every pixel
    -- of the margins and gaps.
    local function onlyone(x, y, gap)
      local n = 0
      if layout.cell_at(layout.snap_cell(x, y)) then n = n + 1 end
      if layout.key_at(layout.snap_key(x, y)) then n = n + 1 end
      if layout.rail_at(layout.snap_rail(x, y)) then n = n + 1 end
      assert(n <= 1, "more than one target answers at " .. x .. "," .. y)
      if gap then eq(n, 0, "a gap answers at " .. x .. "," .. y) end
    end
    for y = top, bottom do
      local edge = y < L.y + 4 or (y >= L.y + L.size - 4 and y < L.pad_y + 4) or y >= pad_bottom - 4
      for x = left, right do
        local fine = x < L.x + 4 or x >= L.x + L.size - 4 or (x >= pad_end - 4 and x < L.rail_x + 4)
        if (edge or y % 8 == 0) and (fine or x % 8 == 0) then
          local gap = (y >= L.y + L.size and y < L.pad_y) or (y >= pad_bottom and x >= pad_end and x < L.rail_x)
          onlyone(x, y, gap)
        end
      end
    end
  end)
end

-- The clock: moves carry the time since the move before, and a refused move's time is not lost.
function rules.clock(state)
  with_clock(function()
    local s, ui = fresh(state), {}
    local clue, cell = s.v:find("[1-9]"), empties(s)[1]
    time.now = 0
    tap(s, ui, 0, 0)
    time.now = 5000
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
    time.now = 7000
    on_cell(s, ui, cell)
    local move = on_key(s, ui, candidate(s, cell))
    eq(move[4], 7000)
    eq(game.apply(s, 1, move).t, 7000)
    eq(ui.msg, nil)
    eq(ui.check, nil)
  end)
end

return rules
