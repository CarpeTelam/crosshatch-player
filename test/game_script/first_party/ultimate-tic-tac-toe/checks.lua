-- The game's own checks (first_party/README.md, `checks.lua`): the board module's geometry and the rules, called
-- directly, and what the draw puts on the canvas beyond text (the highlight, the icons, a won board's mark), read
-- through trace.lua's recording `ch.gfx`. The rounds prove the rules play out through taps; these pin each rule by
-- itself.
local board = require("board")
local game = require("main")
local trace = require("trace")

local LINES = {
  { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 },
  { 1, 4, 7 }, { 2, 5, 8 }, { 3, 6, 9 },
  { 1, 5, 9 }, { 3, 5, 7 },
}
local WRONG_BOARD = "Play in the highlighted board"
local TAKEN = "That cell is taken"

local function eq(got, want, what)
  if got ~= want then error(what .. ": got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- A state with the given pieces; the rest as a new round has it.
local function stateWith(fields)
  local s = game.setup({ seats = 2, mode = "pass", api = 1, settings = {} })
  for key, value in pairs(fields) do s[key] = value end
  return s
end

local function setChar(s, i, ch1) return s:sub(1, i - 1) .. ch1 .. s:sub(i + 1) end

-- `n` chars of `fill` with the given { [index] = char } set.
local function chars(n, fill, set)
  local s = string.rep(fill, n)
  for i, v in pairs(set or {}) do s = setChar(s, i, v) end
  return s
end

local function rejects(state, move, reason, what)
  local before = state.c .. state.w .. state.n .. state.m
  local result, got = game.apply(state, 1, move)
  eq(result, nil, what .. " is rejected")
  eq(got, reason, what .. " reason")
  assert(#got <= 64, what .. ": reason over reject_reason_bytes")
  eq(state.c .. state.w .. state.n .. state.m, before, what .. " leaves the state as it was")
end

-- w, h, cell, x, y, ox, oy: the game lays out one 466 x 788 box (board.W x board.H) centred in the canvas, so on the
-- X4 Pro's 466 x 788 the box is the canvas, on the Sticky's 474 x 788 it is 4 px in, and on the 480 x 800 panel (no
-- device) it is at 7, 6; the cell is 50 px on all three.
local SIZES = {
  { 474, 788, 50, 12, 120, 4, 0 },
  { 466, 788, 50, 8, 120, 0, 0 },
  { 480, 800, 50, 15, 126, 7, 6 },
}

local function geometry()
  for _, size in ipairs(SIZES) do
    local w, h, cell, x, y, ox, oy = table.unpack(size)
    local L = board.layout(w, h)
    local at = w .. "x" .. h
    eq(L.cell, cell, at .. " cell")
    eq(L.size, 9 * cell, at .. " size")
    eq(L.block, 3 * cell, at .. " block")
    eq(L.x, x, at .. " x")
    eq(L.y, y, at .. " y")
    eq(L.ox, ox, at .. " box x")
    eq(L.oy, oy, at .. " box y")
    eq(select(2, board.origin(w, h)), oy, at .. " origin y")
    eq(board.fits(w, h), true, at .. " fits the box")
  end
end

-- Runs f() with ch.screen as w x h and puts it back, also when f raises.
local function withCanvas(w, h, f)
  local rw, rh = ch.screen.w, ch.screen.h
  ch.screen.w, ch.screen.h = w, h
  local ok, err = pcall(f)
  ch.screen.w, ch.screen.h = rw, rh
  if not ok then error(err, 0) end
end

-- The lines f() sends to ch.log.
local function logged(f)
  local real, lines = ch.log, {}
  ch.log = function(...) lines[#lines + 1] = table.concat({ ... }, " ") end
  local ok, err = pcall(f)
  ch.log = real
  if not ok then error(err, 0) end
  return lines
end

local function cellAtInvertsCellRect()
  for _, size in ipairs(SIZES) do
    local L = board.layout(size[1], size[2])
    for row = 1, 9 do
      for col = 1, 9 do
        local x, y, w, h = board.cell_rect(L, row, col)
        eq(w, L.cell, "cell width")
        eq(h, L.cell, "cell height")
        for _, point in ipairs({ { x, y }, { x + w - 1, y + h - 1 }, { x + w // 2, y + h // 2 } }) do
          local r, c = board.cell_at(L, point[1], point[2])
          assert(r == row and c == col, "cell_at disagrees with cell_rect at " .. row .. "," .. col)
        end
      end
    end
    local right, bottom = L.x + L.size, L.y + L.size
    for _, point in ipairs({ { L.x - 1, L.y }, { L.x, L.y - 1 }, { right, L.y }, { L.x, bottom }, { right, bottom },
                             { 0, 0 }, { size[1] - 1, size[2] - 1 } }) do
      eq(board.cell_at(L, point[1], point[2]), nil, "cell_at outside the grid at " .. point[1] .. "," .. point[2])
    end
  end
end

local function blockRectCoversItsCells()
  for _, size in ipairs(SIZES) do
    local L = board.layout(size[1], size[2])
    for brow = 1, 3 do
      for bcol = 1, 3 do
        local bx, by, bw, bh = board.block_rect(L, brow, bcol)
        eq(bw, 3 * L.cell, "block width")
        eq(bh, 3 * L.cell, "block height")
        for crow = 1, 3 do
          for ccol = 1, 3 do
            local x, y, w, h = board.cell_rect(L, (brow - 1) * 3 + crow, (bcol - 1) * 3 + ccol)
            assert(x >= bx and y >= by and x + w <= bx + bw and y + h <= by + bh, "a cell is outside its block")
          end
        end
      end
    end
  end
end

-- Two cells of a line set to a mark and the third played: the small board is won.
local function smallLineWins()
  for b = 1, 9 do
    for _, line in ipairs(LINES) do
      for mark = 1, 2 do
        local set = {}
        for k = 1, 2 do set[(b - 1) * 9 + line[k]] = tostring(mark) end
        local s = stateWith({ c = chars(81, "0", set), m = mark - 1 })
        local twoOnly = game.status(s)
        eq(twoOnly.over, nil, "two in a line do not end the round")
        local moved = assert(game.apply(s, mark, { b, line[3] }))
        eq(moved.w:sub(b, b), tostring(mark), "board " .. b .. " is won by mark " .. mark)
        eq(moved.w:gsub("0", ""):len(), 1, "only that board closes")
      end
    end
  end
end

local function bigLineWins()
  for _, line in ipairs(LINES) do
    for mark = 1, 2 do
      local st = game.status(stateWith({ w = chars(9, "0", { [line[1]] = tostring(mark), [line[2]] = tostring(mark),
                                                               [line[3]] = tostring(mark) }) }))
      assert(st.over and #st.winners == 1 and st.winners[1] == mark, "three won boards in a line win the game")
      local two = game.status(stateWith({ w = chars(9, "0", { [line[1]] = tostring(mark), [line[2]] = tostring(mark) }) }))
      eq(two.over, nil, "two won boards in a line do not")
    end
  end
end

local function drawRule()
  -- Every line holds both marks or a full board.
  local st = game.status(stateWith({ w = "121122211" }))
  assert(st.over and #st.winners == 0, "no live line is a draw")
  st = game.status(stateWith({ w = "333333333" }))
  assert(st.over and #st.winners == 0, "nine full boards are a draw")
  -- A draw does not wait for the last board: every line is dead while the centre board is still open.
  st = game.status(stateWith({ w = "121201212" }))
  assert(st.over and #st.winners == 0, "no live line is a draw with a small board still open")
  -- One line (the second row, 2 2 and an open board) can still be completed by seat 2.
  st = game.status(stateWith({ w = "121022211" }))
  eq(st.over, nil, "one live line is not a draw")
  eq(st.turn, 1, "turn")
  eq(game.status(stateWith({ m = 5 })).turn, 2, "turn after five moves")
  eq(game.status(stateWith({})).over, nil, "a new round is not over")
end

local function taken()
  local s = stateWith({ c = chars(81, "0", { [(5 - 1) * 9 + 5] = "1" }), n = 5, m = 1 })
  rejects(s, { 5, 5 }, TAKEN, "an occupied cell")
  local free = stateWith({ c = chars(81, "0", { [(2 - 1) * 9 + 3] = "2" }) })
  rejects(free, { 2, 3 }, TAKEN, "an occupied cell with any board open")
end

local function wrongBoard()
  rejects(stateWith({ n = 5 }), { 1, 1 }, WRONG_BOARD, "a move outside the forced board")
  rejects(stateWith({ w = chars(9, "0", { [1] = "1" }) }), { 1, 1 }, WRONG_BOARD, "a move in a won board")
  rejects(stateWith({ w = chars(9, "0", { [4] = "3" }) }), { 4, 1 }, WRONG_BOARD, "a move in a full board")
  rejects(stateWith({ w = chars(9, "0", { [1] = "1" }), n = 1 }), { 2, 1 }, WRONG_BOARD, "a move with a stale target")
end

local function badMoves()
  local s = stateWith({})
  for _, move in ipairs({ { 0, 1 }, { 10, 1 }, { 1, 0 }, { 1, 10 }, { 1.5, 1 }, { 1, 2.5 }, { "1", 1 }, { 1, "1" },
                          { 1 }, { nil, 1 }, {}, { -1, 1 } }) do
    local result, reason = game.apply(s, 1, move)
    eq(result, nil, "a bad move is rejected")
    assert(type(reason) == "string" and #reason > 0 and #reason <= 64, "a bad move names a short reason")
  end
  eq(game.apply(s, 1, "5"), nil, "a move that is not a table")
  eq(select(2, game.apply(s, 1, 5)), WRONG_BOARD, "a move that is a number")
  eq(s.m, 0, "bad moves leave the state")
end

local function forcedBoardHandOver()
  local s = assert(game.apply(stateWith({}), 1, { 2, 6 }))
  eq(s.n, 6, "the next move is in board 6")
  eq(s.m, 1, "moves")
  eq(s.c:sub(15, 15), "1", "seat 1 plays X")
  eq(game.status(s).turn, 2, "seat 2 is next")
  local playable = game.playable(s)
  assert(playable[6] and not playable[1] and not playable[2], "only board 6 is playable")
  s = assert(game.apply(s, 2, { 6, 2 }))
  eq(s.c:sub(47, 47), "2", "seat 2 plays O")
  eq(s.n, 2, "the next move is in board 2")
  -- Board 6 won: a move into its cell 6 sends the opponent anywhere.
  local won = stateWith({ w = chars(9, "0", { [6] = "1" }) })
  s = assert(game.apply(won, 1, { 2, 6 }))
  eq(s.n, 0, "a won target frees the choice")
  local open = game.playable(s)
  for b = 1, 9 do eq(open[b] == true, b ~= 6, "board " .. b .. " playable after a free choice") end
  -- A full target frees it too.
  s = assert(game.apply(stateWith({ w = chars(9, "0", { [3] = "3" }) }), 1, { 7, 3 }))
  eq(s.n, 0, "a full target frees the choice")
  -- A move in cell b of board b that closes board b sends nobody to it.
  local about = stateWith({ c = chars(81, "0", { [2] = "1", [3] = "1" }), m = 0 })
  s = assert(game.apply(about, 1, { 1, 1 }))
  eq(s.w:sub(1, 1), "1", "board 1 is won")
  eq(s.n, 0, "the move that wins board 1 in its own cell frees the choice")
  -- A full board without a line closes as "3".
  -- X O X / X O O / O X X, the last X played now.
  local full = stateWith({ c = chars(81, "0", { [1] = "1", [2] = "2", [3] = "1", [4] = "1", [5] = "2", [6] = "2",
                                                [7] = "2", [8] = "1" }), m = 0 })
  s = assert(game.apply(full, 1, { 1, 9 }))
  eq(s.w:sub(1, 1), "3", "nine marks and no line close a board as full")
  -- The ninth cell that completes a line wins the board: a line is checked before "full".
  -- X O O / O X X / X O _, the last X played now, on the diagonal.
  local last = stateWith({ c = chars(81, "0", { [1] = "1", [2] = "2", [3] = "2", [4] = "2", [5] = "1", [6] = "1",
                                               [7] = "1", [8] = "2" }), m = 8 })
  eq(game.status(last).over, nil, "no line yet")
  s = assert(game.apply(last, 1, { 1, 9 }))
  eq(s.w:sub(1, 1), "1", "the ninth cell that completes a line wins the board, it does not close it as full")
  -- No move once the round is over.
  rejects(stateWith({ w = "111000000" }), { 4, 1 }, WRONG_BOARD, "a move after the round is over")
end

local function helpPageFits()
  local lines, bottom = game.help_lines()
  local _, oy = board.origin(ch.screen.w, ch.screen.h)
  assert(#lines > 0, "the page has text")
  assert(bottom <= oy + board.H - 40, "the page fits the box under the harness metrics")
end

-- ---- what the draw puts on the canvas (trace.lua) ----

-- The commands game.draw makes for state `s` and `ui`, seat 1: a list of { name = "rect", <arguments in order> }.
-- The commands of a frame of state s, in order. Each rect, line, text, and image starts on the canvas the frame is drawn for
-- (ch.screen), and a rect or a line ends on it too: a text's width is the device's metrics, which this check does not have.
local function frame(s, ui)
  local events = {}
  trace.record(function() game.draw(s, 1, ui or {}) end, function(name, ...)
    local e = { name = name, ... }
    local k = name == "image" and 2 or 1
    if ch.screen.w >= 466 and ch.screen.h >= 788 and (name == "rect" or name == "line" or name == "text" or name == "image") then
      local x, y = e[k], e[k + 1]
      local ex, ey = x, y
      if name == "rect" then ex, ey = x + e[3], y + e[4] elseif name == "line" then ex, ey = e[3], e[4] end
      assert(x >= 0 and y >= 0 and ex >= 0 and ey >= 0 and x <= ch.screen.w and ex <= ch.screen.w and y <= ch.screen.h
        and ey <= ch.screen.h, "a " .. name .. " command is outside the " .. ch.screen.w .. " x " .. ch.screen.h .. " canvas")
    end
    events[#events + 1] = e
  end)
  return events
end

local function layout() return board.layout(ch.screen.w, ch.screen.h) end

-- A canvas under the box is unsupported, never adapted: the box starts at 0, 0 (the layout is the 466 x 788 one), no
-- cell is under 44 px, and a draw says so in one line, once.
local function smallCanvas()
  local L = board.layout(320, 480)
  eq(board.fits(320, 480), false, "320 x 480 fits the box")
  eq(board.fits(466, 787), false, "466 x 787 fits the box")
  eq(L.ox, 0, "box x on a small canvas")
  eq(L.oy, 0, "box y on a small canvas")
  local home = board.layout(466, 788)
  eq(L.x, home.x, "x on a small canvas")
  eq(L.y, home.y, "y on a small canvas")
  eq(L.cell, home.cell, "cell on a small canvas")
  assert(L.cell >= 44, "a cell under 44 px on a small canvas")
  withCanvas(320, 480, function()
    local first = logged(function() frame(stateWith({})) end)
    eq(#first, 1, "log lines from the first draw on a 320 x 480 canvas")
    assert(first[1]:find("320 x 480", 1, true) and first[1]:find("466 x 788", 1, true),
      "the line names both canvases: " .. first[1])
    eq(#logged(function() frame(stateWith({})) end), 0, "log lines from the second draw")
    eq(#logged(function() frame(stateWith({}), { help = true }) end), 0, "log lines from a help draw")
  end)
end

-- The commands that carry an x, and which of their arguments are one.
local X_ARGS = { line = { 1, 3 }, rect = { 1 }, circle = { 1 }, text = { 1 }, icon = { 2 }, image = { 2 } }

-- Whether the commands of two frames are the same, the second with every x `dx` further right.
local function sameShifted(a, b, dx, what)
  eq(#b, #a, what .. ": commands")
  for i, e in ipairs(a) do
    local o = b[i]
    eq(o.name, e.name, what .. ": command " .. i)
    local x = {}
    for _, k in ipairs(X_ARGS[e.name] or {}) do x[k] = dx end
    assert(X_ARGS[e.name] or e.name == "clear" or e.name == "refresh", what .. ": a command with unknown coordinates: " .. e.name)
    local n = 0
    for k in pairs(e) do
      if math.type(k) == "integer" and k > n then n = k end
    end
    for k = 1, n do
      local want = e[k]
      if x[k] then want = want + x[k] end
      eq(o[k], want, what .. ": command " .. i .. " (" .. e.name .. ") argument " .. k)
    end
  end
end

-- One layout at every canvas: on the Sticky's 474 x 788 every rectangle and tap point of the X4 Pro's 466 x 788 is 4 px
-- right (the 4 px margins are white and miss), and the draw is the 466 draw shifted by 4: no second set of numbers.
local function sameLayoutEverywhere()
  local home, wide = board.layout(466, 788), board.layout(474, 788)
  eq(wide.x - home.x, 4, "x shift")
  eq(wide.y, home.y, "y")
  for row = 1, 9 do
    for col = 1, 9 do
      local hx, hy, hw, hh = board.cell_rect(home, row, col)
      local wx, wy, ww, wh = board.cell_rect(wide, row, col)
      eq(table.concat({ wx, wy, ww, wh }, ","), table.concat({ hx + 4, hy, hw, hh }, ","), "cell " .. row .. "," .. col)
    end
  end
  eq(board.cell_at(wide, wide.x - 1, wide.y), nil, "a tap left of the grid")
  eq(board.cell_at(wide, wide.x + wide.size, wide.y), nil, "a tap right of the grid")
  local taps = require("taps")
  local states = {
    stateWith({}),
    stateWith({ w = "120000000", n = 0, m = 9, c = chars(81, "0", { [1] = "1", [2] = "1", [3] = "1", [13] = "2", [14] = "2",
                                                                    [15] = "2", [23] = "1", [25] = "2", [81] = "1" }) }),
    stateWith({ n = 5, m = 1, c = chars(81, "0", { [41] = "1" }) }),
  }
  for i, st in ipairs(states) do
    for _, ui in ipairs({ {}, { message = "That cell is taken" }, { help = true } }) do
      local a, b
      withCanvas(466, 788, function() a = frame(st, ui) end)
      withCanvas(474, 788, function() b = frame(st, ui) end)
      sameShifted(a, b, 4, "state " .. i .. (ui.help and " help" or ui.message and " message" or ""))
    end
  end
  -- The question button's tap area is the box's: the Sticky's right margin is a miss, and every other tap moves with it.
  for _, w in ipairs({ 466, 474 }) do
    withCanvas(w, 788, function()
      local tx, ty = taps.help()
      local u = {}
      game.input(stateWith({}), 1, u, { kind = "tap", x = tx, y = ty })
      eq(u.help, true, "the help tap opens the page at " .. w)
      u = {}
      game.input(stateWith({}), 1, u, { kind = "tap", x = w - 1, y = ty })
      eq(u.help, w == 466 or nil, "a tap at the canvas's last column at " .. w)
    end)
  end
end

-- The small boards whose block a `light` fill covers, sorted, as "1,5" (each exactly: a light fill that is no block is
-- an error). rect(x, y, w, h, color, filled).
local function lit(events)
  local L, boards = layout(), {}
  for _, e in ipairs(events) do
    if e.name == "rect" and e[5] == "light" then
      local found
      for b = 1, 9 do
        local x, y, w, h = board.block_rect(L, (b - 1) // 3 + 1, (b - 1) % 3 + 1)
        if e[1] == x and e[2] == y and e[3] == w and e[4] == h and e[6] == true then found = b end
      end
      assert(found, "a light fill that is no small board's block: " .. e[1] .. "," .. e[2] .. " " .. e[3] .. "x" .. e[4])
      boards[#boards + 1] = found
    end
  end
  table.sort(boards)
  return table.concat(boards, ",")
end

local function indexOf(events, wanted)
  for i, e in ipairs(events) do
    if wanted(e) then return i end
  end
end

local function lastIndexOf(events, wanted)
  local at
  for i, e in ipairs(events) do
    if wanted(e) then at = i end
  end
  return at
end

local function isLine(e) return e.name == "line" end

local function icons(events)
  local out = {}
  for _, e in ipairs(events) do
    if e.name == "icon" then out[#out + 1] = e end
  end
  return out
end

-- The recorder counts as the engine does (ChBindings.cpp): board.draw_grid is documented as 28 commands (20 lines, 8 filled
-- rects), a `clear` is one command, a `refresh` is none (it asks for a refresh of the frame and appends nothing, so it is
-- neither counted nor in the text, and on_call still sees it), and a function the engine's ch.gfx lacks raises on the
-- recorder too, which is then gone.
local function recorderCounts()
  local text, count = trace.record(function() board.draw_grid(layout()) end)
  eq(count, 28, "draw_grid commands")
  local lines, rects = 0, 0
  for line in text:gmatch("[^\n]+") do
    if line:find("^line%(") then lines = lines + 1 end
    if line:find("^rect%(") then rects = rects + 1 end
  end
  eq(lines, 20, "draw_grid lines")
  eq(rects, 8, "draw_grid rects")
  local real = ch.gfx
  eq(select(2, trace.record(function() end)), 0, "an empty function draws nothing")
  local ok, err = pcall(trace.record, function() ch.gfx.sparkle(1, 2) end)
  eq(ok, false, "a function the engine lacks")
  assert(tostring(err):find("sparkle", 1, true), "the error names the function: " .. tostring(err))
  eq(ch.gfx, real, "ch.gfx after a raised error")
  local seen = {}
  trace.record(function() ch.gfx.rect(1, 2, 3, 4, "light", true) end, function(name, ...) seen = { name, ... } end)
  eq(table.concat({ seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], tostring(seen[7]) }, ","), "rect,1,2,3,4,light,true", "on_call")
  -- A frame with a clear and a refresh in it: the clear is one command, the refresh none; on_call sees all three calls.
  local names = {}
  local framed, framed_count = trace.record(function()
    ch.gfx.clear("white")
    ch.gfx.refresh("full")
    ch.gfx.rect(1, 2, 3, 4, "black", true)
  end, function(name) names[#names + 1] = name end)
  eq(framed_count, 2, "commands of a frame with a clear and a refresh")
  eq(framed, 'clear("white")\nrect(1, 2, 3, 4, "black", true)', "the text of a frame with a refresh")
  eq(table.concat(names, ","), "clear,refresh,rect", "on_call sees a refresh")
end

-- The highlight: `light` fills exactly the small boards the next move may be in (and the full ones), under the grid, and
-- none once the round is over.
local function highlight()
  local forced = frame(stateWith({ n = 5 }))
  eq(lit(forced), "5", "the forced board")
  eq(lit(frame(stateWith({}))), "1,2,3,4,5,6,7,8,9", "any board on a new round")
  eq(lit(frame(stateWith({ w = "102300000", n = 0 }))), "2,4,5,6,7,8,9", "open boards and the full one, not the won")
  eq(lit(frame(stateWith({ w = "000300000", n = 8 }))), "4,8", "the forced board and the full one")
  eq(lit(frame(stateWith({ w = "111000000", n = 0 }))), "", "no highlight once the round is over")
  eq(lit(frame(stateWith({ w = "121122211", n = 0 }))), "", "no highlight once the round is drawn")
  local first = indexOf(forced, isLine)
  local last = lastIndexOf(forced, function(e) return e.name == "rect" and e[5] == "light" end)
  assert(first and last and last < first, "the highlight is drawn under the grid")
end

-- A won board: a white fill inside its block lines, then one big mark (X for seat 1, O for seat 2) over its cells, and
-- the cells of a won board are not drawn; the cells of the others are small marks, one icon each.
local function wonBoards()
  local L = layout()
  local s = stateWith({ w = "120000000", n = 0, m = 9, c = chars(81, "0", {
    [1] = "1", [2] = "1", [3] = "1", [13] = "2", [14] = "2", [15] = "2", -- boards 1 and 2: their lines
    [23] = "1", [25] = "2", -- board 3, not won
    [81] = "1" }) })
  local events = frame(s)
  local all = icons(events)
  eq(#all, 1 + 2 + 3, "icons: the question mark, two big marks, three small")
  local grid = lastIndexOf(events, isLine)
  for b, want in ipairs({ "x", "circle" }) do
    local x, y, w = board.block_rect(L, 1, b)
    local fill = indexOf(events, function(e)
      return e.name == "rect" and e[5] == "white" and e[6] == true and e[1] == x + 2 and e[2] == y + 2 and e[3] == w - 3 and e[4] == w - 3
    end)
    assert(fill, "board " .. b .. " has no white fill inside its block lines")
    local big = indexOf(events, function(e)
      return e.name == "icon" and e[1] == want and e[4] == "large" and e[2] >= x and e[3] >= y and e[2] + 128 <= x + w and e[3] + 128 <= y + w
    end)
    assert(big, "board " .. b .. " has no large " .. want .. " inside its block")
    assert(grid < fill and fill < big, "board " .. b .. ": the fill comes after the grid and before the mark")
    local e = events[big]
    assert(math.abs((e[2] - x) - (x + w - (e[2] + 128))) <= 1 and math.abs((e[3] - y) - (y + w - (e[3] + 128))) <= 1,
      "board " .. b .. "'s mark is not centred")
  end
  -- Small marks: board 3 cell 5 (X) and cell 7 (O), board 9 cell 9 (X), each a 32 px icon centred in its cell, black; none
  -- in the won boards 1 and 2.
  local smalls = {}
  for _, e in ipairs(all) do
    if e[4] == "small" then smalls[#smalls + 1] = e end
  end
  eq(#smalls, 3, "small marks")
  for i, want in ipairs({ { "x", 3, 5 }, { "circle", 3, 7 }, { "x", 9, 9 } }) do
    local e = smalls[i]
    local row = ((want[2] - 1) // 3) * 3 + (want[3] - 1) // 3 + 1
    local col = ((want[2] - 1) % 3) * 3 + (want[3] - 1) % 3 + 1
    local x, y, w, h = board.cell_rect(L, row, col)
    eq(e[1], want[1], "small mark " .. i .. " icon")
    eq(e[5], "black", "small mark " .. i .. " colour")
    assert(e[2] >= x and e[3] >= y and e[2] + 32 <= x + w and e[3] + 32 <= y + h, "small mark " .. i .. " is outside its cell")
    assert(math.abs((e[2] - x) - (x + w - (e[2] + 32))) <= 1 and math.abs((e[3] - y) - (y + h - (e[3] + 32))) <= 1,
      "small mark " .. i .. " is not centred in its cell")
  end
  -- No won board, no white fill and no big mark.
  local plain = frame(stateWith({ c = chars(81, "0", { [5] = "1" }), m = 1, n = 5 }))
  eq(indexOf(plain, function(e) return e.name == "rect" and e[5] == "white" end), nil, "a white fill with no board won")
  eq(#icons(plain), 2, "icons with one mark and no board won")
  -- A mark in a board a draw closed as full (no line) is still a small mark: nothing covers it.
  local full = frame(stateWith({ w = "300000000", c = chars(81, "0", { [1] = "1", [2] = "2" }) }))
  eq(#icons(full), 3, "icons with a full board")
end

-- The question mark: one icon at the top right whose square holds the tap point the rounds use for it.
local function questionMark()
  local taps = require("taps")
  local q = icons(frame(stateWith({})))
  eq(#q, 1, "icons on an empty board")
  eq(q[1][1], "question", "the header icon")
  local tx, ty = taps.help()
  assert(q[1][2] <= tx and tx < q[1][2] + 64 and q[1][3] <= ty and ty < q[1][3] + 64, "the help tap is outside the question icon")
  assert(q[1][2] + 64 <= ch.screen.w and q[1][3] >= 0, "the question icon is off the canvas")
end

-- The help page draws text only: no board, no icon, no fill.
local function helpPageHasNoBoard()
  local events = frame(stateWith({ n = 5 }), { help = true })
  assert(#events > 5, "the help page draws its text")
  for _, e in ipairs(events) do
    assert(e.name == "clear" or e.name == "text", "the help page draws a " .. e.name)
  end
end

-- The ink of a frame: its first command is clear("white") (and no other), every line is black (the grid), every text and icon
-- is black (this game inverts nothing), and a rect is black (the grid's block lines), light (the highlight) or white (a
-- won board's fill). A game that cleared its page black or drew its grid white drew the right commands in the wrong ink,
-- which the commands alone do not show.
local function frameInkIsBlackOnWhite()
  local won = stateWith({ w = "120000000", n = 0, m = 9, c = chars(81, "0", {
    [1] = "1", [2] = "1", [3] = "1", [13] = "2", [14] = "2", [15] = "2", [23] = "1", [25] = "2", [81] = "1" }) })
  local states = { stateWith({}), stateWith({ n = 5 }), won, stateWith({ w = "111000000", n = 0 }) }
  local screens = { {}, { message = "That cell is taken" }, { help = true } }
  for i, st in ipairs(states) do
    for _, ui in ipairs(screens) do
      local what = "state " .. i .. (ui.help and " help" or ui.message and " message" or "")
      local events = frame(st, ui)
      assert(#events > 0, what .. ": an empty frame")
      eq(events[1].name, "clear", what .. ": the first command")
      eq(events[1][1], "white", what .. ": the page is cleared to")
      for k, e in ipairs(events) do
        if k > 1 then assert(e.name ~= "clear", what .. ": a clear after the first command") end
        if e.name == "line" or e.name == "text" then
          eq(e[5], "black", what .. ": a " .. e.name .. " (command " .. k .. ") is drawn")
        elseif e.name == "icon" then
          eq(e[5], "black", what .. ": the icon " .. tostring(e[1]) .. " (command " .. k .. ") is drawn")
        elseif e.name == "rect" then
          assert(e[5] == "black" or e[5] == "light" or e[5] == "white", what .. ": a rect (command " .. k .. ") in " .. tostring(e[5]))
          if e[5] == "black" then eq(e[6], true, what .. ": a black rect (command " .. k .. ") is a filled block line") end
        end
      end
    end
  end
end

-- The host draws its end-of-round dialog over the canvas from host.dialog_top (HostBounds.h) down to host.dialog_bottom
-- once the round is over, so no text of the frame the player is left with may sit in that band: every text box (2 *
-- DY[size] tall as the other first-party games' are: small 26, medium 30, large 54; the box's height is a device font
-- metric this host lacks) ends at or above the dialog or starts at or below it. The frames are the over frames: a won
-- game and a drawn one, with and without the "message" line. Sudoku's level name once sat at y 260 under the dialog.
local TEXT_BOX = { small = 26, medium = 30, large = 54 }
local function endFrameClearsTheDialog()
  local drawn = stateWith({ w = "121122211", n = 0 })
  local over = { stateWith({ w = "111000000", n = 0 }), stateWith({ w = "222000000", n = 0 }), drawn }
  for i, st in ipairs(over) do
    for _, ui in ipairs({ {}, { message = "That cell is taken" } }) do
      local events = frame(st, ui)
      local texts = 0
      for _, e in ipairs(events) do
        if e.name == "text" then
          texts = texts + 1
          local top, bottom = e[2], e[2] + TEXT_BOX[e[4]]
          assert(bottom <= host.dialog_top or top >= host.dialog_bottom,
            "over state " .. i .. ": the text '" .. tostring(e[3]) .. "' (y " .. top .. " to " .. bottom .. ") is under the dialog (y "
            .. host.dialog_top .. " to " .. host.dialog_bottom .. ")")
        end
      end
      assert(texts > 0, "over state " .. i .. " draws no text")
    end
  end
end

-- The Games launcher draws this game's row with the default Crosshatch mark: the manifest names no icon and the package
-- ships no icon.png. host.launcher_icon is GameRowIcon::choose over the installed game.
local function launcherIcon()
  local source = host.launcher_icon()
  eq(source, "fallback", "the launcher's icon source")
end

return {
  { name = "board layout at the X4 Pro's, the Sticky's, and a larger canvas", run = geometry },
  { name = "a canvas under 466 x 788 is laid out from its corner and logged once", run = smallCanvas },
  { name = "the Sticky's layout and draw are the X4 Pro's shifted by 4", run = sameLayoutEverywhere },
  { name = "cell_at inverts cell_rect for all 81 cells and is nil outside", run = cellAtInvertsCellRect },
  { name = "block_rect covers its nine cells", run = blockRectCoversItsCells },
  { name = "every line wins a small board, for both marks", run = smallLineWins },
  { name = "every line of won boards wins the game, for both marks", run = bigLineWins },
  { name = "no live line is a draw, one live line is not", run = drawRule },
  { name = "an occupied cell is rejected", run = taken },
  { name = "a closed or wrong board is rejected", run = wrongBoard },
  { name = "an out-of-range or non-integer move is rejected", run = badMoves },
  { name = "the forced board passes to the played cell's board, or to any open one", run = forcedBoardHandOver },
  { name = "the HOW TO PLAY page fits the canvas", run = helpPageFits },
  { name = "the recorder counts as the engine does: draw_grid is 28 commands", run = recorderCounts },
  { name = "the highlight fills exactly the boards the next move may be in, under the grid", run = highlight },
  { name = "a won board has a white fill and one big mark, a small mark is one icon", run = wonBoards },
  { name = "the question mark holds its tap point", run = questionMark },
  { name = "the help page draws text only", run = helpPageHasNoBoard },
  { name = "a frame is cleared white, its grid and text and icons are black", run = frameInkIsBlackOnWhite },
  { name = "the end frame's text is clear of the host's end-of-round dialog", run = endFrameClearsTheDialog },
  { name = "the launcher draws the default Crosshatch mark", run = launcherIcon },
}
