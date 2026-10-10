-- Sudoku: fill the grid so every row, column, and box holds each digit once. You pick a cell, then a digit on the pad.
-- The puzzles are filed under four bands (the Difficulty setting) by the hardest technique a solver needs for them.
--
-- state = {
--   l = the band, 1 Easy .. 4 Expert,
--   v = 81 chars, "0" empty, "1".."9" a clue, "a".."i" a digit of the player's (1 to 9),
--   n = 162 bytes: the notes, 16 bits a cell, little endian, bit d - 1 for digit d (grid.FMT),
--   u = the undo ring: 3-byte records, oldest first, at most RING of them. A record is a cell (7 bits, 0 for a FILL
--       NOTES step), the digit the cell held (4), and its notes (9) before the step,
--   z = the notes as they were before the FILL NOTES step in the ring (absent without one),
--   t = elapsed milliseconds, the sum of every move's dt,
--   h = 1 once HINT was used (absent before) }
-- A move is a positional table ending in dt, the milliseconds since the move before it:
--   { "w", cell, digit, dt } write (the digit the cell holds clears it), { "n", cell, digit, dt } toggle a note,
--   { "e", cell, dt } erase, { "u", dt } undo, { "f", dt } fill notes, { "h", dt } HINT was used.
-- ui (never saved, so empty after a resume): sel, foc, pencil, panel, note, msg, check, the toggles, and the clock.
-- The sandbox's 256 KB cap counts garbage as well as live data, so the heap is kept small: the bank is dealt from once
-- and the help page is loaded when it is first drawn. The collector runs at this Lua's default pause; no setting
-- here changes it (`collectgarbage("incremental", n)` takes no pause in this Lua, only `"param"` does).
-- A module that is loaded while another is loading needs the C stack twice over: the device refuses a third level
-- ("script recursion too deep to load a module"), and the games check fails a main whose load nests that deep. So a
-- module another one needs is loaded before it, from main or from a function body, never by the other module's own
-- load, and each later require finds it loaded.
require("board")
local grid = require("grid")
local layout = require("layout")

local game = {}

local RING = 48 -- undo steps
local MAX_T = 5999000 -- the elapsed time stops here (99:59, the most m:ss shows)
local LEVELS = { Easy = 1, Medium = 2, Hard = 3, Expert = 4 }
local LEN = { w = 4, n = 4, e = 3, u = 2, f = 2, h = 2 } -- a move's length, dt included
local EDIT = { w = true, n = true, e = true, u = true, f = true }
local BAD, CLUE, NOMARK = "Bad move", "Clues cannot be changed", "That mark is not possible"
local UNITS = { "row", "column", "box" }
local DIG, FMT = grid.DIG, grid.FMT

local function int(x, lo, hi) return math.type(x) == "integer" and x >= lo and x <= hi end

function game.setup(ctx)
  local puzzles = require("puzzles")
  local level = LEVELS[ctx.settings and ctx.settings.level] or 1
  local _, digits = puzzles.get(level, math.random(puzzles.count[level]))
  return { l = level, v = grid.symmetry(digits, math.random), n = string.rep("\0", 162), u = "", t = 0 }
end

-- Over when every cell holds a digit and none repeats in a row, column, or box.
function game.status(state)
  if not state.v:find("0", 1, true) and not grid.clashes(state.v) then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end

local function notes_of(n, c) return (string.unpack("<I2", n, 2 * c - 1)) end

local function with_notes(n, c, m) return n:sub(1, 2 * c - 2) .. string.pack("<I2", m) .. n:sub(2 * c + 1) end

local function with_digit(v, c, d) return v:sub(1, c - 1) .. (d == 0 and "0" or string.char(96 + d)) .. v:sub(c + 1) end

-- Adds a record to the ring; the oldest goes when it is full, and a FILL NOTES step that goes takes its copy along.
local function push(state, rec)
  local u = state.u .. string.pack("<I3", rec)
  if #u > 3 * RING then
    if u:byte(1) & 127 == 0 then state.z = nil end
    u = u:sub(4)
  end
  state.u = u
end

-- FILL NOTES: every empty cell gets exactly its candidates. The one copy in z serves one fill, so a fill still in the
-- ring goes, with every step before it, when another is applied.
local function fill(state)
  local want, old = grid.candidates(state.v), { string.unpack(FMT, state.n) }
  local same = true
  for c = 1, 81 do
    if old[c] ~= want[c] then same = false end
  end
  if same then return false end
  for i = 1, #state.u, 3 do
    if state.u:byte(i) & 127 == 0 then
      state.u = state.u:sub(i + 3)
      break
    end
  end
  state.z, state.n = state.n, string.pack(FMT, table.unpack(want, 1, 81))
  push(state, 0)
  return true
end

local function undo(state)
  local u = state.u
  local rec = string.unpack("<I3", u, #u - 2)
  state.u = u:sub(1, -4)
  local cell = rec & 127
  if cell == 0 then
    if state.z then state.n, state.z = state.z, nil end
  else
    state.v = with_digit(state.v, cell, rec >> 7 & 15)
    state.n = with_notes(state.n, cell, rec >> 11)
  end
end

function game.apply(state, seat, move)
  if game.status(state).over then return nil, "The grid is solved" end
  local kind = type(move) == "table" and move[1] or nil
  local len = LEN[kind]
  local dt = len and move[len]
  if not len or #move ~= len or not int(dt, 0, math.maxinteger) then return nil, BAD end
  local c, d = move[2], move[3]
  if len > 2 and not int(c, 1, 81) then return nil, BAD end
  if len == 4 and not int(d, 1, 9) then return nil, BAD end
  if kind == "u" and #state.u == 0 then return nil, "Nothing to undo" end
  local cur, notes
  if len > 2 then
    local b = state.v:byte(c)
    if b >= 49 and b <= 57 then return nil, CLUE end
    cur, notes = b >= 97 and b - 96 or 0, notes_of(state.n, c)
    local rec = c | cur << 7 | notes << 11
    if kind == "w" then
      state.v = with_digit(state.v, c, cur == d and 0 or d)
      if notes ~= 0 then state.n = with_notes(state.n, c, 0) end
      push(state, rec)
    elseif kind == "n" then
      if cur ~= 0 or grid.peer_has(state.v, c, d) then return nil, NOMARK end
      state.n = with_notes(state.n, c, notes ~ 1 << d - 1)
      push(state, rec)
    else
      if cur == 0 and notes == 0 then return nil, "Nothing to erase" end
      state.v = with_digit(state.v, c, 0)
      state.n = with_notes(state.n, c, 0)
      push(state, rec)
    end
  elseif kind == "u" then
    undo(state)
  elseif kind == "f" then
    if not fill(state) then return nil, "Nothing to fill" end
  else
    state.h = 1
  end
  state.t = math.min(state.t + math.min(dt, MAX_T), MAX_T)
  return state
end

-- ch.store holds the toggles and the best times of this device; a store that cannot be read gives the defaults.
local function read_store()
  local ok, s = pcall(ch.store.get)
  if ok and type(s) == "table" then return s end
  return {}
end

-- A new puzzle is told from the last by its clues: ui starts over (selection, marks, panel, the toggles from the
-- store, and the clock, which counts from here: Play again keeps ui, and a resume starts a new VM).
local function fresh(ui, state)
  local sig = state.v:gsub("%l", "0")
  if ui.sig == sig then return end
  for k in pairs(ui) do ui[k] = nil end
  ui.sig, ui.last = sig, ch.time.ms()
  local s = read_store()
  ui.rem, ui.shade, ui.dots = s.rem ~= false, s.shade ~= false, s.dots ~= false
end

-- The move to return: its dt is the time since the last one, an edit ends HINT's and CHECK's marks.
local function move(ui, kind, cell, digit)
  local now = ch.time.ms()
  local dt = math.max(0, now - ui.last)
  ui.prev, ui.last, ui.was_msg, ui.was_check = ui.last, now, ui.msg, ui.check
  if EDIT[kind] then ui.msg, ui.check = nil, nil end
  if kind == "w" or kind == "n" then return { kind, cell, digit, dt } end
  if kind == "e" then return { kind, cell, dt } end
  return { kind, dt }
end

-- The answer of the clues, kept for the last puzzle asked about.
local solved_for, solved
local function answer(state)
  local clues = state.v:gsub("%l", "0")
  if clues ~= solved_for then solved_for, solved = clues, require("solver").answer(clues) end
  return solved
end

-- HINT: a wrong digit outranks any deduction; else the cell the ladder proves next, and its rule, never the digit.
local function hint(state, ui)
  local ans, v = answer(state), state.v
  ui.panel = nil
  if not ans then
    ui.note = "No hint for this grid"
    return nil
  end
  for c = 1, 81 do
    local b = v:byte(c)
    if b >= 97 and b - 48 ~= ans:byte(c) then
      ui.sel, ui.msg = c, "Wrong digit"
      return move(ui, "h")
    end
  end
  local solver = require("solver")
  local cell, _, rung, unit = solver.hint((v:gsub("%l", grid.LETTER)))
  if not cell then
    ui.note = "No hint for this grid"
    return nil
  end
  ui.sel = cell
  ui.msg = solver.name(rung) .. (unit and " (" .. UNITS[unit // 9 + 1] .. ")" or "")
  return move(ui, "h")
end

-- CHECK: strikes the player's digits that disagree with the answer. It changes nothing in the state, so no move.
local function check(state, ui)
  local ans, v = answer(state), state.v
  ui.panel = nil
  if not ans then
    ui.note = "Cannot check this grid"
    return nil
  end
  local bad, n = {}, 0
  for c = 1, 81 do
    local b = v:byte(c)
    if b >= 97 and b - 48 ~= ans:byte(c) then
      bad[c], n = true, n + 1
    end
  end
  ui.check = bad
  ui.msg = n == 0 and "All correct" or n .. (n == 1 and " wrong digit" or " wrong digits")
  return nil
end

local function toggle(ui, key)
  ui[key] = not ui[key]
  local s = read_store()
  s[key] = ui[key]
  ch.store.set(s)
end

local TOGGLES = { [4] = "rem", [5] = "shade", [6] = "dots" }

-- A tap on MENU row i.
local function menu_tap(state, ui, i)
  if not i then return nil end
  if i == 1 then return hint(state, ui) end
  if i == 2 then
    ui.panel = nil
    return move(ui, "f")
  end
  if i == 3 then return check(state, ui) end
  if TOGGLES[i] then
    toggle(ui, TOGGLES[i])
  elseif i == 7 then
    ui.panel = "help"
  else
    ui.panel = nil
  end
  return nil
end

-- A tap on pad key d.
local function pad_tap(state, ui, d)
  local c = ui.sel
  local b = c and state.v:byte(c)
  if c and not (b >= 49 and b <= 57) then
    if ui.pencil then
      if b ~= 48 or grid.peer_has(state.v, c, d) then
        ui.note = NOMARK
        return nil
      end
      ui.sel = nil
      return move(ui, "n", c, d)
    end
    ui.sel = nil
    if b == 48 or b - 96 ~= d then ui.foc = d end
    return move(ui, "w", c, d)
  end
  ui.foc = ui.foc ~= d and d or nil
  return nil
end

-- A tap on rail button i.
local function rail_tap(state, ui, i)
  if i == 1 then
    ui.pencil = not ui.pencil
  elseif i == 2 then
    local c = ui.sel
    if c and (state.v:byte(c) ~= 48 or notes_of(state.n, c) ~= 0) then return move(ui, "e", c) end
  elseif i == 3 then
    return move(ui, "u")
  else
    ui.panel = "menu"
  end
  return nil
end

-- The best time of the band, kept unless HINT was used; ui keeps what the end screen shows.
local function record(state, ui)
  local s = read_store()
  local best = type(s.best) == "table" and s.best or {}
  local cur = math.type(best[state.l]) == "integer" and best[state.l] or nil
  if not state.h and (not cur or state.t < cur) then
    cur, best[state.l], s.best = state.t, state.t, best
    ch.store.set(s)
  end
  ui.best = cur
end

function game.input(state, seat, ui, ev)
  fresh(ui, state)
  local kind = ev.kind
  if kind == "rejected" then
    ui.note, ui.last = ev.reason, ui.prev or ui.last
    ui.msg, ui.check = ui.was_msg, ui.was_check -- a refused move changed nothing, so HINT's and CHECK's marks stay
    return nil
  end
  if kind == "over" then
    record(state, ui)
    return nil
  end
  if kind ~= "tap" then return nil end
  ui.note = nil
  if ui.panel == "help" then
    ui.panel = nil
    return nil
  end
  if ui.panel == "menu" then return menu_tap(state, ui, layout.menu_at(ev.x, ev.y)) end
  local c = layout.cell_at(layout.snap_cell(ev.x, ev.y))
  if c then
    ui.sel = ui.sel ~= c and c or nil
    return nil
  end
  local d = layout.key_at(layout.snap_key(ev.x, ev.y))
  if d then return pad_tap(state, ui, d) end
  local i = layout.rail_at(layout.snap_rail(ev.x, ev.y))
  if i then return rail_tap(state, ui, i) end
  return nil
end

function game.draw(state, seat, ui)
  fresh(ui, state)
  require("view").draw(state, ui, game.status(state).over)
end

-- Exposed for the game's own checks, which call these without the engine around them.
game.symmetry = grid.symmetry
game.RING = RING

return game
