-- drawn.lua: the pins on what one frame draws (record, watch, expect_marks, frame, look): the worst frame of a dealt
-- grid, its highlight fills, pad counts, strokes, selection frames, and note tiles, and the grounds and numerals of a
-- board, read through a recording `ch.gfx`. A round's `steps(state)` calls them; rules.lua says why they live in a
-- round's VM. marks.lua holds the pins on what each toggle and each change of screen draws, apart from this so that a
-- round that calls only these does not load that module's compiled code (first_party/README.md, "The checks VM heap").
local game = require("main")
local grid = require("grid")
local layout = require("layout")
local pins = require("pins")
local eq, fresh, empties, answer, digit_at = pins.eq, pins.fresh, pins.empties, pins.answer, pins.digit_at
local digit_of, units_of = pins.digit_of, pins.units_of

local drawn = {}

-- The commands of a frame, counted. TEST DOUBLE for the engine's ch.gfx (which faults outside a draw call, keeps a
-- frame to 2,048 commands, and checks an image's name against the package): it counts every call as one command, as the
-- engine does (board.draw_grid says 28 commands, and the first check below pins that this double counts it so), and
-- hands each image call to `on_image` and every call, image included, to `on_call(name, ...)` instead of keeping the
-- calls (a round's VM has no room for a frame's worth of tables). It takes its function names from the real ch.gfx's
-- own keys, so a name the engine lacks (a misspelled call in a draw) raises here too. It is more permissive than the
-- device: it clips nothing, takes any arguments, and does not look an image up, so a name that is not in the package
-- passes it (the games check's rounds, drawing the real ch.gfx, fault on one). A failed draw leaves no recorder
-- installed: ch.gfx is restored and the error raised again.
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

-- What a frame draws beyond its text, gathered as record's `on_call` sees the commands: the cells filled `light` (the
-- clues) and `dark` (SHADE PEERS), by cell, the pad's remaining counts (by digit), the diagonal strokes (by cell,
-- counting lines: a stroke is eight), the outlines inside a cell (its selection frame, by cell and inset from the
-- cell's edge, as the colour drawn), and the full refreshes. A command that is this game's but outside its place (a
-- fill that is no cell's, a count that is not on a key) is an error here.
local function watch()
  local seen = { light = {}, dark = {}, frame = {}, count = {}, rising = {}, falling = {}, full = 0 }
  local at = {}
  for d = 1, 9 do
    local x, y, w = layout.key_rect(d)
    at[(x + w - 8) .. "," .. (y + 4)] = d
  end
  local function on_call(name, a, b, c, d, e, f)
    -- Every command starts on the canvas, and a rect or a line ends on it (a text's width is the device's metrics, not
    -- this check's; an image's name is its first argument).
    local w, h = ch.screen.w, ch.screen.h
    local x, y, ex, ey = a, b, nil, nil
    if name == "rect" then
      ex, ey = a + c, b + d
    elseif name == "line" then
      ex, ey = c, d
    elseif name == "image" then
      x, y = b, c
    end
    if name == "rect" or name == "line" or name == "text" or name == "image" then
      assert(x >= 0 and y >= 0 and x <= w and y <= h and (not ex or (ex >= 0 and ey >= 0 and ex <= w and ey <= h)),
        "a " .. name .. " command is outside the " .. w .. " x " .. h .. " canvas")
    end
    if name == "rect" and (e == "light" or e == "dark") then
      local cell = assert(layout.cell_at(a + 1, b + 1), "a " .. e .. " fill outside the grid")
      local x, y, w, h = layout.cell_rect(cell)
      assert(f == true and a == x + 1 and b == y + 1 and c == w - 1 and d == h - 1, "a " .. e .. " fill that is no cell's")
      seen[e][cell] = (seen[e][cell] or 0) + 1
    elseif name == "rect" and f ~= true and layout.cell_at(a, b) then
      local cell = layout.cell_at(a, b)
      local x, y, w = layout.cell_rect(cell)
      local inset = a - x
      if b - y == inset and c == w - 2 * inset then
        seen.frame[cell] = seen.frame[cell] or {}
        seen.frame[cell][inset] = e
      end
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

-- Pins what a frame of state `s` and ui draws to what the interaction table says, cell by cell: every clue that is not
-- a copy of the focused digit (black ground) is filled `light`, once; SHADE PEERS fills `dark` exactly the selected
-- cell's row, column, and box cells that are neither a clue nor a copy of the focused digit, once each, and nothing
-- when it is off or nothing is selected; SHOW REMAINING puts nine counts on the keys (nine less the digit's cells,
-- clues and the player's, never below 0) and none when off; every clashing cell has a rising stroke and every
-- CHECK-marked cell a falling one (eight lines each), and no other cell has either.
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
    eq(seen.light[c], clue and not (d == ui.foc) and 1 or nil, "light fills of cell " .. c)
    eq(seen.dark[c], shaded and 1 or nil, "dark fills of cell " .. c)
    eq(seen.rising[c], clash[c] and 8 or nil, "clash strokes of cell " .. c)
    eq(seen.falling[c], ui.check and ui.check[c] and 8 or nil, "CHECK strokes of cell " .. c)
    if d then left[d] = left[d] - 1 end
  end
  for d = 1, 9 do eq(seen.count[d], ui.rem and tostring(math.max(left[d], 0)) or nil, "the count on key " .. d) end
end

-- The selection frame of cell sel: black outlines at insets 0 and 1 and, with `halo`, white ones at 2 to 4 (a selected
-- cell with no digit notes on a "dark" ground); without it a black one at 2 (a digit note image's blank margin) and
-- nothing at 3 and 4, where its glyph starts. No other cell has a frame.
local function expect_frame(seen, sel, halo)
  local f = assert(seen.frame[sel], "no selection frame")
  eq(f[0], "black", "frame inset 0")
  eq(f[1], "black", "frame inset 1")
  eq(f[2], halo and "white" or "black", "frame inset 2")
  for i = 3, 4 do eq(f[i], halo and "white" or nil, "frame inset " .. i) end
  for c in pairs(seen.frame) do eq(c, sel, "a frame on another cell") end
end

-- The selection frame of a cell on a ground that is not "dark" or black (a clue, or any cell with SHADE PEERS off) and
-- with no digit notes to show: black outlines at insets 1 and 2 with a white one at 3, and nothing at 0 or 4. No other
-- cell has a frame.
local function expect_plain(seen, sel)
  local f = assert(seen.frame[sel], "no selection frame")
  eq(f[0], nil, "frame inset 0")
  eq(f[1], "black", "frame inset 1")
  eq(f[2], "black", "frame inset 2")
  eq(f[3], "white", "frame inset 3")
  eq(f[4], nil, "frame inset 4")
  for c in pairs(seen.frame) do eq(c, sel, "a frame on another cell") end
end

-- Whether cell c is in a row, column, or box of cell sel: what SHADE PEERS shades.
local function peer(c, sel)
  local a, b, e = units_of(c)
  local sa, sb, se = units_of(sel)
  return a == sa or b == sb or e == se
end

-- Where layout.note_tile puts a digit note's image, on the canvases of both boards: the Sticky's 474 x 788 and the X4
-- Pro's 466 x 788 (BoardConfig insets 9, 7, 3, 7), which lay out the same 466 x 788 box: 50 px cells and a 15 px row
-- pitch on both. The row is literal, the column is the slot's (4, 20, 36) or one pixel on for the dither nudge, the
-- image lies inside offsets 2..cell - 2 of the cell on both axes (no cell line, no block line), and its origin's x + y
-- is even, odd for an inverted one, whatever the cell's corner.
local function expect_tiles()
  local real = ch.screen.w
  local ok, err = pcall(function()
    for _, canvas in ipairs({ { w = 474, cell = 50, pitch = 15 }, { w = 466, cell = 50, pitch = 15 } }) do
      ch.screen.w = canvas.w
      eq(layout.get().cell, canvas.cell, "the cell of a canvas " .. canvas.w .. " wide")
      for x = 0, 1 do
        for y = 0, 1 do
          for _, inverted in ipairs({ false, true }) do
            for k = 1, 9 do
              local tx, ty = layout.note_tile(x, y, k, inverted)
              local name = "image " .. k .. " at " .. x .. ", " .. y .. " on " .. canvas.w
              local col = 4 + 16 * ((k - 1) % 3)
              eq(ty, y + 2 + canvas.pitch * ((k - 1) // 3), name .. " y")
              assert(tx - x == col or tx - x == col + 1, name .. " x " .. tx)
              assert(tx - x >= 2 and tx - x + layout.NOTE_W <= canvas.cell - 1, name .. " is off the cell across")
              assert(ty - y >= 2 and ty - y + layout.NOTE_H <= canvas.cell - 1, name .. " is off the cell down")
              eq((tx + ty) % 2, inverted and 1 or 0, name .. " dither phase")
            end
          end
        end
      end
    end
  end)
  ch.screen.w = real
  if not ok then error(err, 0) end
end

-- The worst frame of the dealt grid: every empty cell holds all nine notes (the ones a neighbour's digit rules out stay
-- hidden), SHADE PEERS, SHOW REMAINING, and a CHECK stroke on every cell are on, a digit is focused, and a cell is
-- selected. It stays inside the engine's 2,048 commands; with notes as images (`digits`), each shown mark is one image
-- of the set and colour its cell and digit call for (see on_image below), at layout.note_tile, inside offsets 2..49 of
-- its cell on both axes so it covers no cell line or block line, with the dither parity the tile's baked checker needs;
-- with dots, no image is drawn.
function drawn.frame(state, digits)
  collectgarbage("collect")
  expect_tiles()
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
    local set, d = name:match("^note_([gbh])([1-9])$")
    assert(set, "an image is a note image: " .. tostring(name))
    d = tonumber(d)
    local c = layout.cell_at(x + layout.NOTE_W // 2, y + layout.NOTE_H // 2)
    local cx, cy = layout.cell_rect(c)
    -- A peer of the selection is shaded, so it takes H, the focused digit's drawn "white"; any other cell takes B for
    -- the focused digit and G for the rest, all "black".
    local shaded, focused = ui.shade and ui.sel ~= nil and peer(c, ui.sel), d == ui.foc
    eq(set, shaded and "h" or (focused and "b" or "g"), name .. " set")
    eq(color, shaded and focused and "white" or "black", name .. " colour")
    local inverted = shaded and focused
    local tx, ty = layout.note_tile(cx, cy, d, inverted)
    eq(x, tx, name .. " x")
    eq(y, ty, name .. " y")
    local dx, dy = x - cx, y - cy
    assert(
      dx >= 2 and dx + layout.NOTE_W <= L.cell - 1 and dy >= 2 and dy + layout.NOTE_H <= L.cell - 1,
      name .. " at " .. dx .. ", " .. dy
    )
    eq((x + y) % 2, inverted and 1 or 0, name .. " dither phase")
  end
  local seen, on_call = watch()
  local commands = record(function() game.draw(s, 1, ui) end, on_image, on_call)
  assert(commands <= 2048, "the worst frame is " .. commands .. " commands")
  expect_marks(seen, s, ui)
  -- The selected cell holds marks: a frame of two black outlines when they are images (they start at inset 2), the
  -- halo frame of its "dark" ground when they are dots (as built).
  expect_frame(seen, ui.sel, not digits)
  eq(seen.full, 1, "full refreshes of the first frame")
  local shown = 0
  for _, c in ipairs(empty) do
    for d = 1, 9 do
      if cand[c] & (1 << d - 1) ~= 0 then shown = shown + 1 end
    end
  end
  eq(images, digits and shown or 0, "marks drawn")
  -- SHADE PEERS off, then nothing selected: no cell is shaded, so every mark is a B or G (on_image reads ui.shade,
  -- ui.sel). The selected cell, on no ground, still has marks: the two black outlines for images and the plain frame
  -- (black at 1 and 2, white at 3) for dots; with nothing selected there is no frame.
  for i, change in ipairs({ function() ui.shade = false end, function() ui.shade, ui.sel = true, nil end }) do
    change()
    images = 0
    local again, on_again = watch()
    record(function() game.draw(s, 1, ui) end, on_image, on_again)
    eq(images, digits and shown or 0, "marks drawn")
    if i == 2 then
      eq(next(again.frame), nil, "a frame with nothing selected")
    elseif digits then
      expect_frame(again, ui.sel, false)
    else
      expect_plain(again, ui.sel)
    end
  end
  -- A selected cell whose stored notes are all ruled out by a neighbour's digit shows no marks, so it keeps the halo
  -- frame of its "dark" ground, as an image or a dot would not.
  local sel, hidden = empty[1], 0
  for d = 1, 9 do
    if cand[sel] & (1 << d - 1) == 0 then hidden = hidden | (1 << d - 1) end
  end
  assert(hidden ~= 0, "the selected cell has a hidden note to store")
  notes[sel] = hidden
  s.n = string.pack(grid.FMT, table.unpack(notes))
  ui.sel = sel
  local quiet, on_quiet = watch()
  record(function() game.draw(s, 1, ui) end, nil, on_quiet)
  expect_frame(quiet, sel, true)
  collectgarbage("collect")
end

-- The grounds and numeral colours of a board with SHADE PEERS on, a cell selected, and a digit focused: every empty
-- cell but the selected one holds the dealt grid's answer as the player's digit. A focused digit's copies are "black"
-- with a "white" numeral (outranking clue and shade), a clue is "light" with a black numeral, a peer of the selection
-- that is neither is "dark" with a black numeral, and every other cell has no ground and a black numeral.
function drawn.look(state)
  collectgarbage("collect")
  local s = fresh(state)
  local solved, empty, v = answer(s), empties(s), {}
  for c = 1, 81 do
    v[c] = s.v:byte(c) == 48 and string.char(solved:byte(c) + 48) or s.v:sub(c, c)
  end
  local sel = empty[1]
  v[sel] = "0"
  s.v = table.concat(v)
  local ui = {}
  game.input(s, 1, ui, { kind = "timer" })
  ui.shade, ui.dots, ui.sel, ui.foc = true, false, sel, digit_at(s, empty[2])
  local grounds, numerals = {}, {}
  local seen, watched = watch()
  local function on_call(name, ...)
    watched(name, ...)
    local x, y, w, h, color, filled = ...
    if name == "rect" and filled and w == layout.get().cell - 1 and h == layout.get().cell - 1 then
      grounds[x .. "," .. y] = color
    elseif name == "text" and h == "large" then
      numerals[x .. "," .. y] = { str = w, color = color }
    end
  end
  record(function() game.draw(s, 1, ui) end, nil, on_call)
  expect_frame(seen, sel, true)
  -- A selected clue (not the focused digit's) is on the "light" ground, so it has the plain frame, not the halo.
  for c = 1, 81 do
    if s.v:byte(c) >= 49 and s.v:byte(c) <= 57 and s.v:byte(c) - 48 ~= ui.foc then
      ui.sel = c
      break
    end
  end
  assert(ui.sel ~= sel, "a clue to select")
  local plain, on_plain = watch()
  record(function() game.draw(s, 1, ui) end, nil, on_plain)
  expect_plain(plain, ui.sel)
  ui.sel = sel
  local kinds = { black = 0, light = 0, dark = 0, none = 0 }
  for c = 1, 81 do
    local x, y, w, hh = layout.cell_rect(c)
    local cx, cy = layout.centre(x, y, w, hh)
    local d, clue, shaded = tonumber(s.v:sub(c, c)) or s.v:byte(c) - 96, s.v:byte(c) <= 57, peer(c, sel)
    local want, ink = nil, "black"
    if c == sel then
      want = "dark"
    elseif d == ui.foc then
      want, ink = "black", "white"
    elseif clue then
      want = "light"
    elseif shaded then
      want = "dark"
    end
    eq(grounds[x + 1 .. "," .. y + 1], want, "ground of cell " .. c)
    kinds[want or "none"] = kinds[want or "none"] + 1
    if c ~= sel then
      local n = numerals[cx .. "," .. cy - layout.DY.large]
      assert(n, "numeral of cell " .. c)
      eq(n.str, tostring(d), "numeral of cell " .. c)
      eq(n.color, ink, "numeral colour of cell " .. c)
    end
  end
  for _, kind in ipairs({ "black", "light", "dark", "none" }) do assert(kinds[kind] > 0, "no " .. kind .. " cell") end
  collectgarbage("collect")
end

-- record, watch, and expect_marks are also marks.lua's.
drawn.record, drawn.watch, drawn.expect_marks = record, watch, expect_marks

return drawn
