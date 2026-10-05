-- Ultimate Tic-Tac-Toe: nine small boards in a 3 x 3 grid. Where you play in a small board decides which small board
-- the other player must play in next; win three small boards in a line to win the game. Two seats pass the device
-- (seat 1 plays X, seat 2 plays O) and both see the same board.
--
-- state = {
--   c = 81 chars, "0" empty, "1" X, "2" O; cell c of small board b is char (b - 1) * 9 + c,
--   w = 9 chars, one per small board: "0" open, "1" or "2" won by that seat, "3" full with no line,
--   n = the small board the next move must be in (1..9), or 0 for any open one,
--   m = moves played }
-- A move is { b, c }: small board b, cell c, both 1..9, numbered row by row.
local board = require("board")

local game = {}

local MARKS = { "X", "O" }
local ICONS = { "x", "circle" }
local LINES = {
  { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 },
  { 1, 4, 7 }, { 2, 5, 8 }, { 3, 6, 9 },
  { 1, 5, 9 }, { 3, 5, 7 },
}
local WRONG_BOARD = "Play in the highlighted board"
local TAKEN = "That cell is taken"
local HELP_X = 80 -- the question button's tap area: this far from the box's right edge, and
local HELP_Y = 100 -- this far from the box's top
-- Both are in the box: the Sticky's 4 px side margins and anything outside the box are a miss.

-- The game lays out one fixed 466 x 788 box (board.lua), centred in the canvas, and never adapts to a smaller canvas:
-- it says so once per VM.
local said = false
local function sayUnsupported()
  if said or board.fits(ch.screen.w, ch.screen.h) then return end
  said = true
  ch.log("Ultimate Tic-Tac-Toe: the canvas is " .. ch.screen.w .. " x " .. ch.screen.h
    .. " and the game needs " .. board.W .. " x " .. board.H .. ": unsupported, laid out from the canvas's corner")
end

local HELP_PARAGRAPHS = {
  "Seat 1 plays X and seat 2 plays O. Take turns: tap an empty cell to put your mark in it.",
  "Where you play in a small board decides where the other player must play next: the cell's place in its small "
    .. "board is the small board they play in.",
  "When that small board is already won or full, they may play in any open small board.",
  "Three marks in a row win a small board, which then shows as one big mark.",
  "Three won small boards in a row win the game. When no row can be completed any more, it is a draw.",
  "The highlighted small boards are the ones you may play in.",
  "The ? button opens this page. Tap anywhere to close it.",
}

local function digit(s, i) return s:sub(i, i) end

-- "1" or "2" when `s` (nine chars) holds three of that mark in a line, else nil.
local function lineMark(s)
  for _, line in ipairs(LINES) do
    local a = digit(s, line[1])
    if (a == "1" or a == "2") and digit(s, line[2]) == a and digit(s, line[3]) == a then return a end
  end
  return nil
end

-- Whether a line of `w` can still become three of `mark`: each of its boards is open or already that mark's.
local function lineLive(w, line, mark)
  for _, i in ipairs(line) do
    local d = digit(w, i)
    if d ~= "0" and d ~= mark then return false end
  end
  return true
end

local function anyLive(w)
  for _, line in ipairs(LINES) do
    if lineLive(w, line, "1") or lineLive(w, line, "2") then return true end
  end
  return false
end

function game.setup(ctx)
  return { c = string.rep("0", 81), w = string.rep("0", 9), n = 0, m = 0 }
end

function game.status(state)
  local mark = lineMark(state.w)
  if mark then return { over = true, winners = { tonumber(mark) } } end
  if not anyLive(state.w) then return { over = true, winners = {} } end
  return { turn = state.m % 2 + 1 }
end

-- The small boards the next move may be in, as a set { [b] = true }: none once the round is over.
function game.playable(state)
  local set = {}
  if game.status(state).over then return set end
  for b = 1, 9 do
    if digit(state.w, b) == "0" and (state.n == 0 or state.n == b) then set[b] = true end
  end
  return set
end

local function isIndex(v) return math.type(v) == "integer" and v >= 1 and v <= 9 end

function game.apply(state, seat, move)
  local b = type(move) == "table" and move[1] or nil
  local c = type(move) == "table" and move[2] or nil
  if not isIndex(b) or not isIndex(c) or not game.playable(state)[b] then return nil, WRONG_BOARD end
  local at = (b - 1) * 9 + c
  if digit(state.c, at) ~= "0" then return nil, TAKEN end

  local mark = tostring(state.m % 2 + 1)
  state.c = state.c:sub(1, at - 1) .. mark .. state.c:sub(at + 1)
  local small = state.c:sub((b - 1) * 9 + 1, b * 9)
  local result = lineMark(small)
  if not result and not small:find("0", 1, true) then result = "3" end
  if result then state.w = state.w:sub(1, b - 1) .. result .. state.w:sub(b + 1) end
  -- The next move goes to the board of the cell just played, if that board is still open.
  state.n = digit(state.w, c) == "0" and c or 0
  state.m = state.m + 1
  return state
end

local function tapIsHelp(ev)
  local ox, oy = board.origin(ch.screen.w, ch.screen.h)
  return ev.x >= ox + board.W - HELP_X and ev.x < ox + board.W and ev.y >= oy and ev.y < oy + HELP_Y
end

function game.input(state, seat, ui, ev)
  if ev.kind == "rejected" then
    ui.message = ev.reason
    return nil
  end
  if ev.kind ~= "tap" then return nil end
  if ui.help then
    ui.help = nil
    return nil
  end
  ui.message = nil
  if tapIsHelp(ev) then
    ui.help = true
    return nil
  end
  local L = board.layout(ch.screen.w, ch.screen.h)
  local row, col = board.cell_at(L, ev.x, ev.y)
  if not row then return nil end
  local brow, crow = (row - 1) // 3 + 1, (row - 1) % 3 + 1
  local bcol, ccol = (col - 1) // 3 + 1, (col - 1) % 3 + 1
  return { (brow - 1) * 3 + bcol, (crow - 1) * 3 + ccol }
end

-- Words of `text` in lines no wider than `maxw` pixels at `size`.
local function wrap(text, size, maxw)
  local lines, line = {}, ""
  for word in text:gmatch("%S+") do
    local try = line == "" and word or line .. " " .. word
    if line ~= "" and ch.text_width(try, size) > maxw then
      lines[#lines + 1] = line
      line = word
    else
      line = try
    end
  end
  if line ~= "" then lines[#lines + 1] = line end
  return lines
end

-- The HOW TO PLAY page's lines as { y, text } and the y below the last one, wrapped to the box's width.
function game.help_lines()
  local _, oy = board.origin(ch.screen.w, ch.screen.h)
  local out, y = {}, oy + 100
  for _, paragraph in ipairs(HELP_PARAGRAPHS) do
    for _, line in ipairs(wrap(paragraph, "small", board.W - 48)) do
      out[#out + 1] = { y = y, text = line }
      y = y + 28
    end
    y = y + 12
  end
  return out, y
end

local function drawHelp()
  local ox, oy = board.origin(ch.screen.w, ch.screen.h)
  ch.gfx.clear("white")
  ch.gfx.text(ox + 24, oy + 30, "HOW TO PLAY", "large", "black")
  local lines = game.help_lines()
  for _, line in ipairs(lines) do ch.gfx.text(ox + 24, line.y, line.text, "small", "black") end
end

local function header(state)
  local st = game.status(state)
  if st.over then
    if #st.winners > 0 then return "Player " .. st.winners[1] .. " (" .. MARKS[st.winners[1]] .. ") wins" end
    return "Draw"
  end
  return "Player " .. st.turn .. " (" .. MARKS[st.turn] .. ") to move"
end

function game.draw(state, seat, ui)
  sayUnsupported()
  if ui.help then
    drawHelp()
    return
  end
  local L = board.layout(ch.screen.w, ch.screen.h)
  ch.gfx.clear("white")
  ch.gfx.text(L.x, L.oy + 40, header(state), "medium", "black")
  ch.gfx.icon("question", L.ox + board.W - 72, L.oy + 12, "medium", "black")

  -- Under the grid: the playable boards and the full ones in light gray.
  local playable = game.playable(state)
  for b = 1, 9 do
    if playable[b] or digit(state.w, b) == "3" then
      local x, y, w, h = board.block_rect(L, (b - 1) // 3 + 1, (b - 1) % 3 + 1)
      ch.gfx.rect(x, y, w, h, "light", true)
    end
  end
  board.draw_grid(L)

  local won = {}
  for b = 1, 9 do
    local d = digit(state.w, b)
    won[b] = d == "1" or d == "2"
  end
  local offset = (L.cell - 32) // 2
  for b = 1, 9 do
    for c = 1, 9 do
      local d = digit(state.c, (b - 1) * 9 + c)
      -- A won board's cells are covered by its big mark below, so they are not drawn.
      if d ~= "0" and not won[b] then
        local row = ((b - 1) // 3) * 3 + (c - 1) // 3 + 1
        local col = ((b - 1) % 3) * 3 + (c - 1) % 3 + 1
        local x, y = board.cell_rect(L, row, col)
        ch.gfx.icon(ICONS[tonumber(d)], x + offset, y + offset, "small", "black")
      end
    end
  end

  -- A won board: a white fill inside its block lines, then one big mark over its cells.
  for b = 1, 9 do
    if won[b] then
      local x, y, w = board.block_rect(L, (b - 1) // 3 + 1, (b - 1) % 3 + 1)
      ch.gfx.rect(x + 2, y + 2, w - 3, w - 3, "white", true)
      -- The biggest icon that fits the block (128, 64, or 32 px: the sizes the API has).
      local size, side = "large", 128
      if w < 128 then size, side = "medium", 64 end
      if w < 64 then size, side = "small", 32 end
      local pad = (w - side) // 2
      ch.gfx.icon(ICONS[tonumber(digit(state.w, b))], x + pad, y + pad, size, "black")
    end
  end

  if ui.message then ch.gfx.text(L.x, L.y + L.size + 12, ui.message, "small", "black") end
end

return game
