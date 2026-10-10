-- canvases.lua: one layout on every canvas, pinned from rounds/canvases.lua. The Sticky's 474 x 788 gets the X4 Pro's
-- 466 x 788 layout and frames with every x 4 px right (the game lays out one 466 x 788 box and centres it), and a
-- canvas under 466 x 788 is unsupported: laid out from its corner and said so once in ch.log. It is a module of its
-- own, called by a round whose VM loads none of the pin modules (rules.lua, drawn.lua, and the rest), as built when that VM
-- had the device's 256 KB heap and a frame's draw left tens of KB of garbage (first_party/README.md, "The check VMs'
-- limits"): it keeps no table of a frame's commands, only a digest.
local game = require("main")
local grid = require("grid")
local layout = require("layout")

local canvases = {}

-- A solved grid (every cell a clue): what the end screen is drawn for.
local SOLVED = "534678912672195348198342567859761423426853791713924856961537284287419635345286179"

local function eq(got, want, what)
  if got ~= want then error((what or "") .. " got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- Runs f() with ch.screen as w x h and puts it back, also when f raises.
local function at_canvas(w, h, f)
  local rw, rh = ch.screen.w, ch.screen.h
  ch.screen.w, ch.screen.h = w, h
  local ok, err = pcall(f)
  ch.screen.w, ch.screen.h = rw, rh
  if not ok then error(err, 0) end
end

-- Runs draw() with a recording `ch.gfx` that hands each command to on_call(name, ...): the names are the real table's
-- own keys, so a name the engine lacks raises here too (drawn.lua's `record` is the fuller one; this is the part a
-- digest needs).
local function record(draw, on_call)
  local gfx = {}
  for name, value in pairs(ch.gfx) do
    if type(value) == "function" then gfx[name] = function(...) on_call(name, ...) end end
  end
  local real = ch.gfx
  ch.gfx = gfx
  local ok, err = pcall(draw)
  ch.gfx = real
  if not ok then error(err, 0) end
end

-- A digest of the frame `draw` makes with ch.screen as w x h, every x taken `dx` back first: the commands folded in
-- order into a number (the numbers as they are, a string by its length and every byte), kept at every 32nd command too,
-- so a difference is placed within 32. A command that carries no x and is no clear or refresh is an error, so a draw
-- call this table does not know cannot go unchecked. A `refresh` is folded into the digest (a frame that asks for one
-- differently differs) but is no command: the engine appends nothing for it (ChBindings.cpp), so it is not counted, and a
-- `clear` is one. Returns the digests and the number of commands.
local function digests(w, h, dx, draw)
  local marks, count, h0 = {}, 0, 0
  local function fold(v)
    local t = type(v)
    if t == "number" then
      h0 = (h0 * 31 + v) & 0x7FFFFFFF
    elseif t == "string" then
      h0 = (h0 * 31 + #v) & 0x7FFFFFFF
      for i = 1, #v do h0 = (h0 * 31 + v:byte(i)) & 0x7FFFFFFF end
    else
      h0 = (h0 * 31 + (v and 1 or 2)) & 0x7FFFFFFF
    end
  end
  local function on_call(name, a, b, c, d, e, f, g)
    if name == "line" then
      a, c = a - dx, c - dx
    elseif name == "icon" or name == "image" then
      b = b - dx
    elseif name == "rect" or name == "circle" or name == "text" then
      a = a - dx
    else
      assert(name == "clear" or name == "refresh", "a command with unknown coordinates: " .. name)
    end
    fold(name)
    fold(a)
    fold(b)
    fold(c)
    fold(d)
    fold(e)
    fold(f)
    fold(g)
    if name == "refresh" then return end
    count = count + 1
    if count % 32 == 0 then marks[count // 32] = h0 end
  end
  at_canvas(w, h, function() record(draw, on_call) end)
  marks[count // 32 + 1] = h0
  return marks, count
end

-- A copy of a ui table, with nothing shown yet (the first draw of a ui asks for a full refresh).
local function copy_of(ui)
  local copy = {}
  for k, v in pairs(ui) do copy[k] = v end
  copy.shown = nil
  return copy
end

-- The frame of state `st` and ui `ui` on the Sticky's 474 x 788 is the X4 Pro's 466 x 788 frame with every x 4 px
-- right: the same commands in the same order, and a difference is placed within 32 commands.
local function same_shifted(st, ui, what)
  local a, count = digests(466, 788, 0, function() game.draw(st, 1, copy_of(ui)) end)
  local b, other = digests(474, 788, 4, function() game.draw(st, 1, copy_of(ui)) end)
  assert(count >= 5, what .. ": a frame of " .. count .. " commands")
  eq(other, count, what .. ": commands on the Sticky against the X4 Pro")
  for i = 1, #a do
    if a[i] ~= b[i] then
      error(what .. ": the commands " .. (i - 1) * 32 + 1 .. " to " .. i * 32 .. " differ between 466 and 474", 0)
    end
  end
end

-- A ui table as the game makes one for state `s` (the toggles at their defaults), with `fields` over it.
local function ui_for(s, fields)
  local ui = { sig = (s.v:gsub("%l", "0")), last = 0, rem = true, shade = true, dots = false }
  for k, v in pairs(fields) do ui[k] = v end
  return ui
end

local function rects()
  local into = { cell = {}, key = {}, rail = {}, menu = {}, tile = {}, L = layout.get() }
  for c = 1, 81 do into.cell[c] = table.concat({ layout.cell_rect(c) }, ",") end
  for d = 1, 9 do into.key[d] = table.concat({ layout.key_rect(d) }, ",") end
  for i = 1, layout.RAIL do into.rail[i] = table.concat({ layout.rail_rect(i) }, ",") end
  for i = 1, layout.ROWS do into.menu[i] = table.concat({ layout.menu_rect(i) }, ",") end
  for _, c in ipairs({ 1, 2, 10, 41, 81 }) do
    local x, y = layout.cell_rect(c)
    for k = 1, 9 do
      for _, inverted in ipairs({ false, true }) do
        into.tile[#into.tile + 1] = table.concat({ layout.note_tile(x, y, k, inverted) }, ",")
      end
    end
  end
  return into
end

-- The layout on the Sticky's 474 x 788 is the X4 Pro's 466 x 788 with every x 4 px right: every cell, key, rail button,
-- and MENU row, and every note tile (whose dither phase holds because 4 is even); the cells are 50 px on both.
function canvases.layout()
  local home, wide
  at_canvas(466, 788, function() home = rects() end)
  at_canvas(474, 788, function() wide = rects() end)
  for _, kind in ipairs({ "cell", "key", "rail", "menu" }) do
    for i, r in ipairs(home[kind]) do
      local x, y, w, h = r:match("^(-?%d+),(-?%d+),(-?%d+),(-?%d+)$")
      eq(wide[kind][i], table.concat({ x + 4, y, w, h }, ","), "the " .. kind .. " " .. i .. " rectangle on the Sticky")
    end
  end
  for i, t in ipairs(home.tile) do
    local x, y = t:match("^(-?%d+),(-?%d+)$")
    eq(wide.tile[i], (x + 4) .. "," .. y, "note tile " .. i .. " on the Sticky")
  end
  for _, key in ipairs({ "x", "ox", "rail_x", "row_x" }) do
    eq(wide.L[key], home.L[key] + 4, "L." .. key .. " on the Sticky")
  end
  local same = { "y", "oy", "cell", "size", "block", "pad_y", "kw", "kh", "rail_w", "bh", "row_y", "row_h", "row_w" }
  for _, key in ipairs(same) do
    eq(wide.L[key], home.L[key], "L." .. key .. " on the Sticky")
  end
  eq(home.L.cell, 50, "the cell on the X4 Pro")
  eq(home.L.ox + home.L.oy, 0, "the X4 Pro's box is the canvas")
  eq(wide.L.ox, 4, "the Sticky's box x")
  return home.L
end

-- The frames: the worst frame of the dealt grid (every empty cell holds all nine notes, SHADE PEERS, SHOW REMAINING, a
-- CHECK stroke on every cell, a digit focused, a cell selected) with notes as digits and as dots, the MENU panel, the
-- HOW TO PLAY page, and the end screen, each the same on both boards, 4 px right. With ch.log watched: a supported
-- canvas says nothing, a canvas under the box (320 x 480) is laid out as the box at 0, 0 and the first draw says so,
-- once.
function canvases.frames(state, home)
  local s = { l = state.l, v = state.v, n = string.rep("\0", 162), u = "", t = 0 }
  local notes, empty = {}, nil
  for c = 1, 81 do
    notes[c] = s.v:byte(c) == 48 and 0x1FF or 0
    if notes[c] ~= 0 then empty = empty or c end
  end
  s.n = string.pack(grid.FMT, table.unpack(notes))
  local checks = {}
  for c = 1, 81 do checks[c] = true end
  local real, lines = ch.log, {}
  ch.log = function(...) lines[#lines + 1] = table.concat({ ... }, " ") end
  local ok, err = pcall(function()
    for _, dots in ipairs({ false, true }) do
      same_shifted(s, ui_for(s, { dots = dots, foc = 5, sel = empty, check = checks }),
        dots and "the worst frame with dot notes" or "the worst frame with digit notes")
    end
    same_shifted(s, ui_for(s, { panel = "menu" }), "the MENU panel")
    same_shifted(s, ui_for(s, { panel = "help" }), "the HOW TO PLAY page")
    local solved = { l = state.l, v = SOLVED, n = s.n, u = "", t = 83000 }
    eq(game.status(solved).over, true, "the solved grid is over")
    same_shifted(solved, ui_for(solved, { best = 83000 }), "the end screen")
    eq(#lines, 0, "log lines from draws on supported canvases")
    at_canvas(320, 480, function()
      local small = layout.get()
      eq(small.ox + small.oy, 0, "the box on a small canvas")
      eq(small.x, home.x, "x on a small canvas")
      eq(small.y, home.y, "y on a small canvas")
      eq(small.cell, 50, "cell on a small canvas")
      record(function() game.draw(s, 1, ui_for(s, {})) end, function() end)
      eq(#lines, 1, "log lines from the first draw on a 320 x 480 canvas")
      assert(
        lines[1]:find("320 x 480", 1, true) and lines[1]:find("466 x 788", 1, true),
        "the line names both: " .. lines[1]
      )
      record(function() game.draw(s, 1, ui_for(s, { panel = "menu" })) end, function() end)
      eq(#lines, 1, "log lines after a second draw")
    end)
  end)
  ch.log = real
  if not ok then error(err, 0) end
end

return canvases
