-- marks.lua: the pins on the marks a draw adds to the board as each toggle and each change of screen set them
-- (draw_marks): SHADE PEERS, SHOW REMAINING, the strokes, and the full refresh. A round's `steps(state)` calls it;
-- rules.lua says why it lives in a round's VM, and drawn.lua holds the recorder it reads through.
local game = require("main")
local pins = require("pins")
local eq, fresh, empties, answer, mv = pins.eq, pins.fresh, pins.empties, pins.answer, pins.mv
local candidate, tap, on_cell, on_key, on_rail = pins.candidate, pins.tap, pins.on_cell, pins.on_key, pins.on_rail
local on_menu, digit_of, units_of = pins.on_menu, pins.digit_of, pins.units_of
local drawn = require("drawn")
local record, watch, expect_marks = drawn.record, drawn.watch, drawn.expect_marks

local marks = {}

-- The marks `game.draw` of state `st` with `u` makes, as watch gathers them.
local function marks_of(st, u)
  local seen, on_call = watch()
  record(function() game.draw(st, 1, u) end, nil, on_call)
  return seen
end

-- The marks a draw adds to the board, each on a grid the test sets up and checked against expect_marks's own
-- arithmetic: SHADE PEERS and the focus ground (a legal digit written in a peer of the selected cell, which is then not
-- shaded), SHOW REMAINING with a digit past its nine, the strokes of a clash (rising) and of CHECK (falling) on cells
-- that overlap, the toggles' off states, and the full refresh: once on the first frame and on each change between the
-- board, the MENU, the HOW TO PLAY page, and the end screen, never on a tap of the board.
function marks.draw_marks(state)
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
  assert(next(seen.dark) and clue_peers > 0 and not seen.dark[peer], "SHADE PEERS has nothing to tell apart here")
  assert(next(seen.light), "no clue is filled light")
  -- Off, nothing selected, nothing focused: the fills follow (and the counts and strokes stay as they are).
  ui.shade = false
  assert(next(marks_of(s, ui).dark) == nil, "SHADE PEERS off still fills")
  expect_marks(marks_of(s, ui), s, ui)
  ui.shade, ui.sel = true, nil
  assert(next(marks_of(s, ui).dark) == nil, "no selection still fills")
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

return marks
