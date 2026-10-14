-- nearby fixture: noughts and crosses for two seats, the game the nearby match's host suites play
-- (epic-play-nearby entry 9). Seat 1 plays X, seat 2 plays O; the board and the log lines read as pass-open's do. A tap
-- on a free square is the move; a taken one is rejected in apply ("That square is taken"); a tap on the boom square
-- under the board returns a move that apply answers with a script error; every accepted apply writes
-- ch.store.set({ moves = n, last = seat }). Each call logs the seat it ran for (apply seat S cell C, rejected for seat
-- S, over for seat S board B), so a suite reads from the log which device ran which call. There is no timer. The game
-- reads ctx.mode only to log it.
local game = {}

local CELL = 140
local LEFT = 27 -- (474 - 3 * 140) / 2: centred on the Sticky's 474 px canvas (the test canvas; the X4 Pro's is 466)
local TOP = 200
local BOOM_TOP = 700 -- the boom square: BOOM_SIZE px under the board, at the left edge of the board
local BOOM_SIZE = 60
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

-- The board as nine digits (0 free, 1 X, 2 O), for the log.
local function board(cells)
  return table.concat(cells)
end

function game.setup(ctx)
  ch.log("setup mode " .. tostring(ctx.mode) .. " seats " .. tostring(ctx.seats))
  return { moves = 0, cells = { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
end

function game.status(state)
  local mark = lineMark(state.cells)
  if mark then return { over = true, winners = { mark } } end
  if state.moves >= 9 then return { over = true, winners = {} } end
  return { turn = state.moves % 2 + 1 }
end

function game.apply(state, seat, move)
  if move.boom then error("boom") end
  ch.log("apply seat " .. seat .. " cell " .. tostring(move.cell))
  -- An unknown cell reads nil, which is not 0 either.
  if state.cells[move.cell] ~= 0 then return nil, "That square is taken" end
  state.cells[move.cell] = state.moves % 2 + 1
  state.moves = state.moves + 1
  ch.store.set({ moves = state.moves, last = seat })
  return state
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("tap for seat " .. seat)
    ui.message = nil
    if ev.x >= LEFT and ev.x < LEFT + BOOM_SIZE and ev.y >= BOOM_TOP and ev.y < BOOM_TOP + BOOM_SIZE then
      return { boom = true }
    end
    if ev.x < LEFT or ev.y < TOP then return nil end
    local col = (ev.x - LEFT) // CELL
    local row = (ev.y - TOP) // CELL
    if col > 2 or row > 2 then return nil end
    return { cell = row * 3 + col + 1 }
  elseif ev.kind == "rejected" then
    ch.log("rejected for seat " .. seat)
    ui.message = ev.reason
  elseif ev.kind == "over" then
    ui.overs = (ui.overs or 0) + 1
    ch.log("over for seat " .. seat .. " board " .. board(state.cells))
  end
  return nil
end

-- The line at the top: whose move it is, or how the round ended.
local function headline(state, seat)
  local st = game.status(state)
  local mark = MARKS[seat] or "?"
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
  ch.gfx.text(LEFT, TOP + 3 * CELL + 70, "Overs " .. (ui.overs or 0), "small", "black")
  ch.gfx.rect(LEFT, BOOM_TOP, BOOM_SIZE, BOOM_SIZE, "black", true)
end

return game
