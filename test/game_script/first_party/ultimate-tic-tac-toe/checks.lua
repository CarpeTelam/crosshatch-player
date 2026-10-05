-- The game's own checks (first_party/README.md, `checks.lua`): the board module's geometry and the rules, called
-- directly. The rounds prove the rules play out through taps; these pin each rule by itself.
local board = require("board")
local game = require("main")

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

local SIZES = { { 474, 788, 51, 7 }, { 480, 800, 52, 6 }, { 320, 480, 34, 7 } }

local function geometry()
  for _, size in ipairs(SIZES) do
    local w, h, cell, x = size[1], size[2], size[3], size[4]
    local L = board.layout(w, h)
    local at = w .. "x" .. h
    eq(L.cell, cell, at .. " cell")
    eq(L.size, 9 * cell, at .. " size")
    eq(L.block, 3 * cell, at .. " block")
    eq(L.x, x, at .. " x")
    eq(L.y, 120, at .. " y")
  end
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
  assert(#lines > 0, "the page has text")
  assert(bottom <= ch.screen.h - 40, "the page fits the canvas under the harness metrics")
end

return {
  { name = "board layout at three canvas sizes", run = geometry },
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
}
