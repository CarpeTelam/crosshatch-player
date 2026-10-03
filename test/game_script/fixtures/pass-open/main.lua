-- pass-open fixture: noughts and crosses, the open pass match's game (epic-pass-and-play
-- entry 1). In pass, seat 1 plays X and seat 2 plays O, and each sees the board on its
-- turn; in solo, seat 1 places both marks in turn. A tap on a square is a move (a taken
-- one is rejected); each seat's ui keeps its own last rejection, `over` count, and timer
-- nudges. Seat 0's frame is the one for everyone that the end-of-round menu sits over.
-- The log lines name the seat each call was for.
local game = {}

local CELL = 140
local LEFT = 27 -- (474 - 3 * 140) / 2: centred on the X4 Pro's 474 px canvas
local TOP = 200
local DELAY_MS = 10000
local MARKS = { "X", "O" }
local ICONS = { "x", "circle" }
local LINES = {
  { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 },
  { 1, 4, 7 }, { 2, 5, 8 }, { 3, 6, 9 },
  { 1, 5, 9 }, { 3, 5, 7 },
}

-- The mark on a line of three, or nil.
local function lineMark(cells)
  for _, line in ipairs(LINES) do
    local mark = cells[line[1]]
    if mark ~= 0 and cells[line[2]] == mark and cells[line[3]] == mark then return mark end
  end
  return nil
end

function game.setup(ctx)
  ch.timer.after(DELAY_MS)
  return { seats = ctx.seats, moves = 0, cells = { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
end

function game.status(state)
  local mark = lineMark(state.cells)
  if mark then
    if state.seats >= 2 then return { over = true, winners = { mark } } end
    return { over = true, winners = { 1 } }
  end
  if state.moves >= 9 then return { over = true, winners = {} } end
  if state.seats >= 2 then return { turn = state.moves % 2 + 1 } end
  return { turn = 1 }
end

function game.apply(state, seat, move)
  ch.log("apply seat " .. seat .. " cell " .. tostring(move.cell))
  -- An unknown cell reads nil, which is not 0 either.
  if state.cells[move.cell] ~= 0 then return nil, "That square is taken" end
  state.cells[move.cell] = state.moves % 2 + 1
  state.moves = state.moves + 1
  return state
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("tap for seat " .. seat)
    ui.message = nil
    ch.timer.after(DELAY_MS)
    if ev.x < LEFT or ev.y < TOP then return nil end
    local col = (ev.x - LEFT) // CELL
    local row = (ev.y - TOP) // CELL
    if col > 2 or row > 2 then return nil end
    return { cell = row * 3 + col + 1 }
  elseif ev.kind == "rejected" then
    ui.message = ev.reason
  elseif ev.kind == "over" then
    ui.overs = (ui.overs or 0) + 1
    ch.timer.cancel()
    ch.log("over for seat " .. seat)
    return { cell = 5 } -- an answer to over: the runtime discards it
  elseif ev.kind == "timer" then
    ui.nudges = (ui.nudges or 0) + 1
    ch.log("timer for seat " .. seat)
    ch.timer.after(DELAY_MS)
  end
  return nil
end

-- The line at the top: whose move it is, or how the round ended.
local function headline(state, seat)
  local st = game.status(state)
  if seat == 0 then
    if not st.over then return "Everyone: Player " .. st.turn .. " to move" end
    if #st.winners > 0 then return "Everyone: Player " .. st.winners[1] .. " wins" end
    return "Everyone: a draw"
  end
  local mark = MARKS[state.seats >= 2 and seat or state.moves % 2 + 1] or "?"
  local who = "Player " .. seat .. " (" .. mark .. ")"
  if st.over then
    if #st.winners > 0 then return who .. ": Player " .. st.winners[1] .. " wins" end
    return who .. ": a draw"
  end
  if st.turn == seat then return who .. " to move" end
  return who .. ": Player " .. st.turn .. " to move"
end

function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.text(LEFT, 40, "Noughts and crosses", "large", "black")
  ch.gfx.text(LEFT, 120, headline(state, seat), "medium", "black")
  for i = 1, 9 do
    local x = LEFT + (i - 1) % 3 * CELL
    local y = TOP + (i - 1) // 3 * CELL
    ch.gfx.rect(x, y, CELL, CELL, "black", false)
    local mark = state.cells[i]
    if mark ~= 0 then ch.gfx.icon(ICONS[mark], x + (CELL - 128) // 2, y + (CELL - 128) // 2, "large", "black") end
  end
  if ui.message then ch.gfx.text(LEFT, TOP + 3 * CELL + 30, ui.message, "small", "black") end
  ch.gfx.text(LEFT, TOP + 3 * CELL + 70, "Nudges " .. (ui.nudges or 0) .. ", overs " .. (ui.overs or 0), "small",
    "black")
end

return game
