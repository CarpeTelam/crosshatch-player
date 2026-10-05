-- The game's own checks (first_party/README.md, `checks.lua`): the layout, the fleet rules, and `apply` called
-- directly, against a fleet validator written here and not shared with the game. The rounds prove the rules play out
-- through taps; these pin each rule by itself. What the draw puts on the canvas beyond text (secrecy at the level of
-- the commands, icon and shape kinds, seat 0's Over frame) is pinned by draws.lua, which the rounds call from their
-- `steps` functions: this VM has no room to draw frames (first_party/README.md, "The checks VM heap").
local fleet = require("fleet")
local layout = require("layout")
local game = require("main")
local taps = require("taps")

local OVER = "The game is over"
local PLACE_FIRST = "Place your ships first"
local PLACED = "The ships are placed. Fire at the other board"
local TAP_BOARD = "Tap a square on the board"
local NO_SHIPS = "No ships to clear"
local NOT_READY = "Place all five ships first"
local ALL_PLACED = "All five ships are placed"
local OFF_BOARD = "That ship would go off the board"
local OVERLAP = "Ships cannot overlap"
local NO_ROOM = "No room for the other ships; tap Clear"
local ALREADY = "You already fired there"

local SMALL_LINE = 24 -- the height of a line of small text: the game's own step for a wrapped message

local KEYS = { "a", "b", "c", "d", "e" }
local LENGTHS = { a = 5, b = 4, c = 3, d = 3, e = 2 }
local NAMES = { "Carrier", "Battleship", "Cruiser", "Submarine", "Destroyer" }

local function eq(got, want, what)
  if got ~= want then error(what .. ": got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- Whether a fleet string is a legal fleet: five ships of the right lengths, each straight and contiguous, nothing
-- else on the board. Returns true, or false and why. Written here, with no use of the game's own code.
local function validFleet(f)
  if type(f) ~= "string" or #f ~= 100 then return false, "not 100 cells" end
  if f:find("[^.a-e]") then return false, "a cell that is no water or ship" end
  for _, key in ipairs(KEYS) do
    local cells = {}
    for i = 1, 100 do
      if f:sub(i, i) == key then cells[#cells + 1] = i end
    end
    if #cells ~= LENGTHS[key] then return false, "ship " .. key .. " has " .. #cells .. " cells" end
    local row0, col0 = (cells[1] - 1) // 10, (cells[1] - 1) % 10
    local across, down = true, true
    for k, at in ipairs(cells) do
      local row, col = (at - 1) // 10, (at - 1) % 10
      if row ~= row0 or col ~= col0 + k - 1 then across = false end
      if col ~= col0 or row ~= row0 + k - 1 then down = false end
    end
    if not (across or down) then return false, "ship " .. key .. " is not straight and contiguous" end
  end
  return true
end

-- The independent reading of what the other seat may see: water, intact ships ".", misses "o", a hit cell "x" or, with
-- its ship sunk, "s".
local function expectedMask(f)
  local out = {}
  for i = 1, 100 do
    local char = f:sub(i, i)
    if char == "." or char:find("[a-e]") then
      out[i] = "."
    elseif char == "o" then
      out[i] = "o"
    else
      out[i] = f:find(char:lower(), 1, true) and "x" or "s"
    end
  end
  return table.concat(out)
end

-- A fleet from { row, col, "H" | "V" } ships placed in the game's order, through fleet.place.
local function build(ships, from)
  local f = from or fleet.EMPTY
  for i, ship in ipairs(ships) do
    local got, reason = fleet.place(f, ship[1], ship[2], ship[3] == "V")
    if not got then error("ship " .. i .. " was not placed: " .. tostring(reason), 2) end
    f = got
  end
  return f
end

local ACROSS = { { 1, 1, "H" }, { 2, 1, "H" }, { 3, 1, "H" }, { 4, 1, "H" }, { 5, 1, "H" } }
local DOWN = { { 1, 10, "V" }, { 1, 9, "V" }, { 1, 8, "V" }, { 1, 7, "V" }, { 1, 6, "V" } }

-- The cells of ship `index` in a fleet string, as { row, col }.
local function cellsOf(f, index)
  local out = {}
  for i = 1, 100 do
    if f:sub(i, i):lower() == KEYS[index] then out[#out + 1] = { (i - 1) // 10 + 1, (i - 1) % 10 + 1 } end
  end
  return out
end

-- A new round's state with the given fields replaced.
local function stateWith(fields)
  local s = game.setup({ seats = 2, mode = "pass", api = 1, settings = {} })
  for key, value in pairs(fields) do s[key] = value end
  return s
end

-- A state in the firing phase with the given fleets and the shooter `t`.
local function firing(f1, f2, t)
  return stateWith({ f = { f1, f2 }, p = 3, t = t or 1 })
end

local function dump(s)
  local l = s.l and table.concat(s.l, ",") or "none"
  return table.concat({ s.f[1], s.f[2], s.p, s.t, l }, "|")
end

local function rejects(state, move, reason, what)
  local before = dump(state)
  local result, got = game.apply(state, 1, move)
  eq(result, nil, what .. " is rejected")
  eq(got, reason, what .. " reason")
  assert(#got <= 64, what .. ": reason over reject_reason_bytes")
  assert(ch.text_width(got, "small") <= layout.compute(ch.screen.w, ch.screen.h).big.cell * 10,
    what .. ": reason wider than the board")
  eq(dump(state), before, what .. " leaves the state as it was")
end

-- ---- layout ----

local SIZES = { { 474, 788 }, { 480, 800 } }

local function boardRect(B) return { x = B.x, y = B.y, w = 10 * B.cell, h = 10 * B.cell } end

local function insideCanvas(r, w, h, what)
  assert(r.x >= 0 and r.y >= 0 and r.x + r.w <= w and r.y + r.h <= h, what .. " is outside the " .. w .. "x" .. h .. " canvas")
end

local function apart(a, b, what)
  assert(a.x + a.w <= b.x or b.x + b.w <= a.x or a.y + a.h <= b.y or b.y + b.h <= a.y, what .. " overlap")
end

local function geometry()
  for _, size in ipairs(SIZES) do
    local w, h = size[1], size[2]
    local L = layout.compute(w, h)
    local at = w .. "x" .. h
    eq(L.big.cell, 44, at .. " big cell")
    eq(L.big.x, (w - 440) // 2, at .. " big x")
    eq(L.big.y, 52, at .. " big y")
    eq(L.small.cell, 28, at .. " small cell")
    eq(L.small.x, L.big.x, at .. " small x")
    eq(L.small.y, 500, at .. " small y")
    eq(L.over[1].cell, 18, at .. " over cell")
    eq(L.over[1].y, 72, at .. " over y")
    eq(L.over[2].y, 72, at .. " over y of the second board")
    eq(L.over_label_y, 44, at .. " over label y")
    -- Each label's text box (small text: the game's 24 px line step) ends 4 px or more above the boards it names.
    assert(L.over_label_y + SMALL_LINE + 4 <= L.over[1].y, at .. " the over labels must end at least 4 px above their boards")
    eq(L.over[2].x - L.over[1].x, 194, at .. " over boards are 14 px apart")
    eq(L.over[1].x - (w - (L.over[2].x + 180)), 0, at .. " over boards are centred")
    local boards = { big = boardRect(L.big), small = boardRect(L.small), over1 = boardRect(L.over[1]),
                     over2 = boardRect(L.over[2]) }
    for name, rect in pairs(boards) do insideCanvas(rect, w, h, at .. " " .. name) end
    -- The dialog's top edge measured at y 268 of the panel in the simulator, and the canvas starts 9 px down the panel
    -- on the X4 Pro (474 x 788 in 480 x 800), so 259 in canvas pixels.
    assert(boards.over1.y + boards.over1.h <= 259, at .. " the over boards reach into the end-of-round dialog")
    apart(boards.big, boards.small, at .. " the big and small boards")
    apart(boards.over1, boards.over2, at .. " the over boards")
    -- The firing column: right of the small board, inside the canvas, above Result's banner (about y 649).
    local col = L.column
    insideCanvas(col, w, h, at .. " column")
    apart(col, boards.small, at .. " the column and the small board")
    assert(col.y + col.h <= 649, at .. " the column reaches Result's banner")
    -- The question button.
    local q = L.question_rect
    eq(q.w, 72, at .. " question width")
    eq(q.h, 52, at .. " question height")
    insideCanvas(q, w, h, at .. " question")
    insideCanvas({ x = L.question.x, y = L.question.y, w = 32, h = 32 }, w, h, at .. " question icon")
    assert(layout.inside(q, L.question.x, L.question.y) and layout.inside(q, L.question.x + 31, L.question.y + 31),
      at .. " the question icon is outside its tap rectangle")
    apart(q, boards.big, at .. " the question button and the big board")
    -- The placement controls: four buttons below the big board, clear of each other.
    local rects = { q }
    for _, name in ipairs(layout.BUTTONS) do
      local b = L.buttons[name]
      eq(b.w, 104, at .. " " .. name .. " width")
      eq(b.h, 84, at .. " " .. name .. " height")
      insideCanvas(b, w, h, at .. " " .. name)
      assert(b.y >= boards.big.y + boards.big.h, at .. " " .. name .. " is over the big board")
      for _, other in ipairs(rects) do apart(b, other, at .. " " .. name .. " and another control") end
      rects[#rects + 1] = b
    end
    eq(L.buttons.rotate.y, L.buttons.ready.y, at .. " the buttons share a row")
    assert(L.message.y + 24 <= h and L.message.y > L.buttons.ready.y + 84, at .. " the message is not below the buttons")
    assert(L.tray.y + L.tray.square <= L.buttons.rotate.y, at .. " the tray is over the buttons")
    local trayW = -L.tray.gap
    for _, key in ipairs(KEYS) do trayW = trayW + LENGTHS[key] * L.tray.square + L.tray.gap end
    assert(trayW <= 10 * L.big.cell, at .. " the ship tray is wider than the board")
    eq(L.over_counts_y + 28 + 24 <= h, true, at .. " the shot counts are off the canvas")
  end
end

local function cellAtInvertsCellRect()
  for _, size in ipairs(SIZES) do
    local L = layout.compute(size[1], size[2])
    for name, B in pairs({ big = L.big, small = L.small, over1 = L.over[1], over2 = L.over[2] }) do
      for row = 1, 10 do
        for col = 1, 10 do
          local x, y, w, h = layout.cell_rect(B, row, col)
          eq(w, B.cell, name .. " cell width")
          eq(h, B.cell, name .. " cell height")
          for _, point in ipairs({ { x, y }, { x + w - 1, y + h - 1 }, { x + w // 2, y + h // 2 } }) do
            local r, c = layout.cell_at(B, point[1], point[2])
            assert(r == row and c == col, name .. ": cell_at disagrees with cell_rect at " .. row .. "," .. col)
          end
        end
      end
      local side = 10 * B.cell
      for _, point in ipairs({ { B.x - 1, B.y }, { B.x, B.y - 1 }, { B.x + side, B.y }, { B.x, B.y + side },
                               { B.x + side - 1, B.y + side }, { B.x + side, B.y + side - 1 }, { B.x - 1, B.y + side - 1 } }) do
        eq(layout.cell_at(B, point[1], point[2]), nil, name .. ": cell_at one pixel outside at " .. point[1] .. "," .. point[2])
      end
    end
  end
end

local function tapTargets()
  local L = layout.compute(ch.screen.w, ch.screen.h)
  for row = 1, 10 do
    for col = 1, 10 do
      local r, c = layout.cell_at(L.big, taps.cell(row, col))
      assert(r == row and c == col, "taps.cell misses the cell " .. row .. "," .. col)
    end
  end
  for _, name in ipairs(layout.BUTTONS) do
    local x, y = taps.button(name)
    for _, other in ipairs(layout.BUTTONS) do
      eq(layout.inside(L.buttons[other], x, y), other == name, "the " .. name .. " tap is in the " .. other .. " button")
    end
    eq(layout.inside(L.question_rect, x, y), false, "the " .. name .. " tap is on the question button")
    eq(layout.cell_at(L.big, x, y), nil, "the " .. name .. " tap is on the big board")
  end
  local qx, qy = taps.help()
  eq(layout.inside(L.question_rect, qx, qy), true, "the question tap")
  eq(layout.cell_at(L.big, qx, qy), nil, "the question tap is on the big board")
  local ox, oy = taps.off_target()
  eq(layout.cell_at(L.big, ox, oy), nil, "the off-target tap is on the big board")
  eq(layout.inside(L.question_rect, ox, oy), false, "the off-target tap is on the question button")
  for _, name in ipairs(layout.BUTTONS) do
    eq(layout.inside(L.buttons[name], ox, oy), false, "the off-target tap is on the " .. name .. " button")
  end
  -- Every tap target is at least 44 px each way.
  assert(L.big.cell >= 44 and L.question_rect.w >= 44 and L.question_rect.h >= 44, "a target under 44 px")
  for _, name in ipairs(layout.BUTTONS) do
    assert(L.buttons[name].w >= 44 and L.buttons[name].h >= 44, "the " .. name .. " button is under 44 px")
  end
end

-- ---- the fleet rules ----

local function shipTable()
  eq(#fleet.SHIPS, 5, "ships")
  for i, ship in ipairs(fleet.SHIPS) do
    eq(ship.key, KEYS[i], "ship " .. i .. " letter")
    eq(ship.len, LENGTHS[KEYS[i]], "ship " .. i .. " length")
    eq(ship.name, NAMES[i], "ship " .. i .. " name")
  end
  eq(#fleet.EMPTY, 100, "an empty fleet")
  eq(fleet.EMPTY:find("[^.]"), nil, "an empty fleet is water")
end

local function placementOrderAndShape()
  local f = fleet.EMPTY
  for i, ship in ipairs(ACROSS) do
    eq(fleet.next_ship(f), i, "the next ship before ship " .. i)
    f = assert(fleet.place(f, ship[1], ship[2], false))
    eq(select(2, f:gsub(KEYS[i], "")), LENGTHS[KEYS[i]], "ship " .. i .. " cells")
  end
  eq(fleet.next_ship(f), nil, "no ship left")
  assert(validFleet(f))
  local down = build(DOWN)
  assert(validFleet(down))
  eq(select(2, down:sub(10, 10):gsub("a", "")), 1, "the Carrier is down column 10")
  -- A ship sits where it was tapped: first square at the anchor, across or down.
  local one = assert(fleet.place(fleet.EMPTY, 4, 3, false))
  eq(one:sub(33, 37), "aaaaa", "an across ship's cells")
  local two = assert(fleet.place(fleet.EMPTY, 4, 3, true))
  eq(two:sub(33, 33) .. two:sub(43, 43) .. two:sub(53, 53) .. two:sub(63, 63) .. two:sub(73, 73), "aaaaa", "a down ship's cells")
  eq(select(2, two:gsub("a", "")), 5, "a down ship has five cells")
end

local function touchingAndOverlap()
  -- Edge to edge, side by side and end to end: accepted.
  local a = assert(fleet.place(fleet.EMPTY, 1, 1, false))
  assert(fleet.place(a, 2, 1, false), "ships side by side are accepted")
  assert(fleet.place(a, 1, 6, false), "ships end to end are accepted")
  assert(fleet.place(a, 2, 5, true), "a ship against the end of another is accepted")
  -- Over another: rejected, every way.
  for _, try in ipairs({ { 1, 5, false }, { 1, 1, false }, { 1, 3, true }, { 1, 5, true }, { 1, 2, true } }) do
    local got, reason = fleet.place(a, try[1], try[2], try[3])
    eq(got, nil, "a ship over another at " .. try[1] .. "," .. try[2])
    eq(reason, OVERLAP, "overlap reason")
  end
end

local function offBoard()
  local f = fleet.EMPTY
  for i, ship in ipairs(fleet.SHIPS) do
    local len = ship.len
    -- The last anchor that fits is accepted, the next one is off the board.
    assert(fleet.place(f, 1, 11 - len, false), "ship " .. i .. " fits against the right edge")
    assert(fleet.place(f, 11 - len, 1, true), "ship " .. i .. " fits against the bottom edge")
    eq(select(2, fleet.place(f, 1, 12 - len, false)), OFF_BOARD, "ship " .. i .. " off the right edge")
    eq(select(2, fleet.place(f, 12 - len, 1, true)), OFF_BOARD, "ship " .. i .. " off the bottom edge")
    eq(select(2, fleet.place(f, 0, 1, false)), OFF_BOARD, "ship " .. i .. " above the board")
    eq(select(2, fleet.place(f, 1, 0, true)), OFF_BOARD, "ship " .. i .. " left of the board")
    eq(select(2, fleet.place(f, 11, 1, false)), OFF_BOARD, "ship " .. i .. " below the board")
    f = assert(fleet.place(f, i, 1, false))
  end
  local reason
  _, reason = fleet.place(f, 1, 1, false)
  eq(reason, ALL_PLACED, "a sixth ship")
end

local function legal(f, manual, what)
  local ok, why = validFleet(f)
  assert(ok, what .. ": " .. tostring(why))
  -- The ships placed before are where they were.
  for i = 1, 100 do
    local char = manual:sub(i, i)
    if char ~= "." then eq(f:sub(i, i), char, what .. ": a kept ship moved at " .. i) end
  end
end

-- Random from `count` manual ships (0 to 4), one seed each of `first` to `last`.
local function randomFrom(count, first, last)
  local manual = fleet.EMPTY
  for i = 1, count do manual = build({ ACROSS[i] }, manual) end
  for seed = first, last do
    math.randomseed(seed)
    local f, reason = fleet.random_fill(manual)
    assert(f, "random_fill failed: " .. tostring(reason))
    legal(f, manual, "seed " .. seed .. " from " .. count .. " ships")
  end
end

local function randomFromEmpty() randomFrom(0, 1, 20) end
local function randomFromOne() randomFrom(1, 21, 32) end
local function randomFromTwo() randomFrom(2, 33, 44) end
local function randomFromThree() randomFrom(3, 45, 56) end
local function randomFromFour() randomFrom(4, 57, 68) end

local function randomVaries()
  local seen = {}
  local count = 0
  for seed = 1, 6 do
    math.randomseed(seed)
    local f = fleet.random_fill(fleet.EMPTY)
    if not seen[f] then
      seen[f] = true
      count = count + 1
    end
  end
  assert(count >= 4, "Random gave only " .. count .. " different fleets in 6 seeds")
end

local function randomNeedsRoom()
  local full = build(ACROSS)
  local got, reason = fleet.random_fill(full)
  eq(got, nil, "random_fill on five ships")
  eq(reason, ALL_PLACED, "random_fill reason with five ships")
  -- Not a reachable fleet, only a board with no room: the diagonal cells (row + col) % 4 == 0 are taken by Carrier
  -- cells, so no run of four free squares is left for the Battleship, across or down.
  local cells = {}
  for i = 1, 100 do
    local row, col = (i - 1) // 10, (i - 1) % 10
    cells[i] = (row + col) % 4 == 0 and "a" or "."
  end
  local blocked = table.concat(cells)
  local result, why = fleet.random_fill(blocked)
  eq(result, nil, "random_fill with no room")
  eq(why, NO_ROOM, "no room reason")
  rejects(stateWith({ f = { blocked, fleet.EMPTY } }), { "R" }, NO_ROOM, "Random with no room")
end

-- ---- apply, phase by phase ----

local function placeMoves()
  local s = stateWith({})
  assert(game.apply(s, 1, { "P", 1, 1, "H" }))
  eq(s.f[1]:sub(1, 5), "aaaaa", "the Carrier across")
  eq(s.f[2], fleet.EMPTY, "the other fleet is untouched")
  eq(s.p, 1, "the turn stays with the placing seat")
  eq(game.status(s).turn, 1, "status turn")
  assert(game.apply(s, 1, { "P", 2, 1, "V" }))
  eq(s.f[1]:sub(11, 11) .. s.f[1]:sub(21, 21) .. s.f[1]:sub(31, 31) .. s.f[1]:sub(41, 41), "bbbb", "the Battleship down")
  rejects(s, { "P", 1, 1, "H" }, OVERLAP, "a ship over the Carrier")
  rejects(s, { "P", 1, 9, "H" }, OFF_BOARD, "a ship off the right edge")
  rejects(s, { "P", 9, 10, "V" }, OFF_BOARD, "a ship off the bottom edge")
  -- Seat 2 places on its own fleet.
  local t = stateWith({ p = 2 })
  assert(game.apply(t, 2, { "P", 10, 1, "H" }))
  eq(t.f[1], fleet.EMPTY, "seat 1's fleet is untouched")
  eq(t.f[2]:sub(91, 95), "aaaaa", "seat 2's Carrier")
  -- Five placed: no sixth.
  local five = stateWith({ f = { build(ACROSS), fleet.EMPTY } })
  rejects(five, { "P", 7, 7, "H" }, ALL_PLACED, "a sixth ship")
end

local function randomClearReady()
  local s = stateWith({})
  assert(game.apply(s, 1, { "P", 1, 1, "H" }))
  assert(game.apply(s, 1, { "R" }))
  assert(validFleet(s.f[1]))
  eq(s.f[1]:sub(1, 5), "aaaaa", "Random keeps the Carrier")
  eq(s.p, 1, "Random keeps the turn")
  rejects(s, { "R" }, ALL_PLACED, "Random with all five placed")
  assert(game.apply(s, 1, { "C" }))
  eq(s.f[1], fleet.EMPTY, "Clear lifts every ship")
  eq(s.p, 1, "Clear keeps the turn")
  rejects(s, { "C" }, NO_SHIPS, "Clear with nothing placed")
  rejects(s, { "Y" }, NOT_READY, "Ready with nothing placed")
  assert(game.apply(s, 1, { "P", 1, 1, "H" }))
  rejects(s, { "Y" }, NOT_READY, "Ready with one ship")
  -- Ready needs five; seat 1's Ready passes the turn to seat 2, seat 2's starts the firing with seat 1.
  local r = stateWith({ f = { build(ACROSS), fleet.EMPTY } })
  assert(game.apply(r, 1, { "Y" }))
  eq(r.p, 2, "seat 1's Ready")
  eq(game.status(r).turn, 2, "the turn passes to seat 2")
  rejects(r, { "Y" }, NOT_READY, "seat 2's Ready with no ships")
  r.f[2] = build(DOWN)
  assert(game.apply(r, 2, { "Y" }))
  eq(r.p, 3, "seat 2's Ready starts the firing")
  eq(r.t, 1, "seat 1 fires first")
  eq(game.status(r).turn, 1, "status turn at the start of the firing")
  eq(game.status(r).over, nil, "not over")
end

local function badMoves()
  local placing = stateWith({})
  local shooting = firing(build(ACROSS), build(DOWN))
  for _, s in ipairs({ placing, shooting }) do
    for _, move in ipairs({ {}, { "Z" }, { 5 }, { "p", 1, 1, "H" }, { "" } }) do
      rejects(s, move, TAP_BOARD, "the move {" .. tostring(move[1]) .. "}")
    end
    eq(select(2, game.apply(s, 1, "P")), TAP_BOARD, "a move that is a string")
    eq(select(2, game.apply(s, 1, 5)), TAP_BOARD, "a move that is a number")
    eq(select(2, game.apply(s, 1, nil)), TAP_BOARD, "a missing move")
  end
  for _, move in ipairs({ { "P", 0, 1, "H" }, { "P", 11, 1, "H" }, { "P", 1, 0, "H" }, { "P", 1, 11, "H" },
                          { "P", 1.5, 1, "H" }, { "P", 1, 2.5, "H" }, { "P", "1", 1, "H" }, { "P", 1, "1", "H" },
                          { "P", -1, 1, "H" }, { "P", 1, 1, "X" }, { "P", 1, 1, "h" }, { "P", 1, 1, 1 }, { "P", 1, 1 },
                          { "P", 1 }, { "P" }, { "P", nil, 1, "H" } }) do
    rejects(placing, move, TAP_BOARD, "the placement {" .. table.concat({ tostring(move[1]), tostring(move[2]),
                                                                       tostring(move[3]), tostring(move[4]) }, ",") .. "}")
  end
  for _, move in ipairs({ { "F", 0, 1 }, { "F", 11, 1 }, { "F", 1, 0 }, { "F", 1, 11 }, { "F", 1.5, 1 },
                          { "F", 1, 2.5 }, { "F", "1", 1 }, { "F", 1, "1" }, { "F", 1 }, { "F" }, { "F", nil, 1 } }) do
    rejects(shooting, move, TAP_BOARD, "the shot {" .. table.concat({ tostring(move[1]), tostring(move[2]),
                                                                    tostring(move[3]) }, ",") .. "}")
  end
end

local function wrongPhase()
  rejects(stateWith({}), { "F", 1, 1 }, PLACE_FIRST, "a shot while seat 1 places")
  rejects(stateWith({ p = 2 }), { "F", 1, 1 }, PLACE_FIRST, "a shot while seat 2 places")
  local s = firing(build(ACROSS), build(DOWN))
  for _, move in ipairs({ { "P", 1, 1, "H" }, { "R" }, { "C" }, { "Y" } }) do
    rejects(s, move, PLACED, "the move " .. move[1] .. " while firing")
  end
end

-- ---- firing ----

local function missHitSunk()
  local f1, f2 = build(ACROSS), build(DOWN)
  -- A miss: water, then the turn passes.
  local s = firing(f1, f2)
  assert(game.apply(s, 1, { "F", 9, 1 }))
  eq(s.f[2]:sub(81, 81), "o", "a miss is marked")
  eq(s.f[1], f1, "the shooter's own fleet is untouched")
  eq(table.concat(s.l, ","), "1,9,1,0,0", "the last shot of a miss")
  eq(s.t, 2, "the turn passes after a miss")
  eq(game.status(s).turn, 2, "status turn")
  -- A hit: the cell turns uppercase, the ship is still afloat.
  assert(game.apply(s, 2, { "F", 1, 1 }))
  eq(s.f[1]:sub(1, 1), "A", "a hit is marked")
  eq(table.concat(s.l, ","), "2,1,1,1,1", "the last shot of a hit")
  eq(s.t, 1, "the turn passes after a hit")
  -- Each ship sunk by its last cell, in both fleets.
  for index = 1, 5 do
    for _, shooter in ipairs({ 1, 2 }) do
      local own, other = build(ACROSS), build(DOWN)
      local target = shooter == 1 and other or own
      local cells = cellsOf(target, index)
      local state = shooter == 1 and firing(own, other, 1) or firing(own, other, 2)
      for k, cell in ipairs(cells) do
        state.t = shooter
        assert(game.apply(state, shooter, { "F", cell[1], cell[2] }))
        local want = k == #cells and 2 or 1
        eq(state.l[4], want, "ship " .. index .. " cell " .. k .. " result")
        eq(state.l[5], index, "ship " .. index .. " number")
      end
      eq(fleet.ships_left(state.f[3 - shooter]), 4, "ship " .. index .. " is the one sunk")
      eq(game.status(state).over, nil, "one ship sunk does not end the round")
    end
  end
end

local function repeatedShot()
  local s = firing(build(ACROSS), build(DOWN))
  assert(game.apply(s, 1, { "F", 9, 1 })) -- a miss
  s.t = 1
  rejects(s, { "F", 9, 1 }, ALREADY, "a repeated miss")
  assert(game.apply(s, 1, { "F", 1, 10 })) -- a hit
  s.t = 1
  rejects(s, { "F", 1, 10 }, ALREADY, "a repeated hit")
  -- A sunk ship's cell too.
  local sunk = firing(build(ACROSS), build(DOWN))
  for _, cell in ipairs(cellsOf(sunk.f[2], 5)) do
    sunk.t = 1
    assert(game.apply(sunk, 1, { "F", cell[1], cell[2] }))
  end
  local cell = cellsOf(sunk.f[2], 5)[1]
  sunk.t = 1
  rejects(sunk, { "F", cell[1], cell[2] }, ALREADY, "a repeated shot at a sunk ship")
  -- The turn stays where it was after a rejection.
  eq(sunk.t, 1, "the turn after a rejected shot")
end

local function winning()
  for shooter = 1, 2 do
    local own, other = build(ACROSS), build(DOWN)
    local state = shooter == 1 and firing(own, other, 1) or firing(own, other, 2)
    local target = 3 - shooter
    -- Every cell of the target's fleet but the last.
    local all = {}
    for index = 1, 5 do
      for _, cell in ipairs(cellsOf(state.f[target], index)) do all[#all + 1] = cell end
    end
    eq(#all, 17, "a fleet has 17 cells")
    for k = 1, #all - 1 do
      state.t = shooter
      assert(game.apply(state, shooter, { "F", all[k][1], all[k][2] }))
      eq(game.status(state).over, nil, "shot " .. k .. " does not end the round")
    end
    state.t = shooter
    assert(game.apply(state, shooter, { "F", all[17][1], all[17][2] }))
    local st = game.status(state)
    eq(st.over, true, "the 17th hit ends the round for shooter " .. shooter)
    eq(#st.winners, 1, "one winner")
    eq(st.winners[1], shooter, "the shooter wins")
    eq(state.t, shooter, "the winner stays the turn seat")
    eq(state.l[4], 2, "the last shot sinks")
    eq(game.headline(state, 1), "Player " .. shooter .. " wins!", "the header for seat 1")
    eq(game.headline(state, 2), "Player " .. shooter .. " wins!", "the header for seat 2")
    -- No move once the round is over.
    rejects(state, { "F", 10, 10 }, OVER, "a shot after the round is over")
    rejects(state, { "Y" }, OVER, "Ready after the round is over")
    rejects(state, { "P", 1, 1, "H" }, OVER, "a placement after the round is over")
  end
end

local function statusByPhase()
  eq(game.status(stateWith({})).turn, 1, "seat 1 places first")
  eq(game.status(stateWith({ p = 2 })).turn, 2, "then seat 2")
  eq(game.status(firing(build(ACROSS), build(DOWN), 1)).turn, 1, "seat 1 fires")
  eq(game.status(firing(build(ACROSS), build(DOWN), 2)).turn, 2, "seat 2 fires")
  -- Beaten means no lowercase ship letter: a fleet of hits and misses only.
  local beaten = string.rep("o", 83) .. "AAAAABBBBCCCDDDEE"
  eq(#beaten, 100, "the beaten fleet is 100 cells")
  local st = game.status(firing(build(ACROSS), beaten, 1))
  eq(st.over, true, "a beaten fleet 2 ends the round")
  eq(st.winners[1], 1, "seat 1 wins")
  st = game.status(firing(beaten, build(DOWN), 2))
  eq(st.over, true, "a beaten fleet 1 ends the round")
  eq(st.winners[1], 2, "seat 2 wins")
  -- One intact cell keeps the round going.
  local alive = string.rep("o", 83) .. "AAAAABBBBCCCDDDEe"
  eq(game.status(firing(build(ACROSS), alive, 1)).over, nil, "one intact cell is not beaten")
end

-- ---- what a seat may see ----

local function playRandomGame(seed, visit)
  math.randomseed(seed)
  local s = stateWith({})
  assert(game.apply(s, 1, { "R" }))
  assert(game.apply(s, 1, { "Y" }))
  assert(game.apply(s, 2, { "R" }))
  assert(game.apply(s, 2, { "Y" }))
  visit(s)
  local shots = 0
  while not game.status(s).over do
    local move = { "F", math.random(10), math.random(10) }
    local seat = s.t
    if game.apply(s, seat, move) then
      shots = shots + 1
      visit(s)
    end
    assert(shots <= 200, "the game does not end")
  end
  return s, shots
end

local function secrecy()
  local mine = build(ACROSS)
  local a = build(ACROSS)
  -- The same ships in other places: the Carrier on the bottom row, the Battleship at the corner.
  local c = build({ { 10, 6, "H" }, { 1, 1, "H" }, { 3, 1, "H" }, { 4, 1, "H" }, { 5, 1, "H" } })
  assert(a ~= c and validFleet(c))
  -- No shot yet: nothing of the other fleet shows, whichever it is.
  eq(game.views(firing(mine, a), 1).target, fleet.EMPTY, "a target with no shots, fleet one")
  eq(game.views(firing(mine, c), 1).target, fleet.EMPTY, "a target with no shots, fleet two")
  -- The same shots at both: a miss at (7, 7) and a hit at (1, 1), which is the Carrier in one and the Battleship in
  -- the other. The two targets are the same.
  local sa, sc = firing(mine, a), firing(mine, c)
  for _, s in ipairs({ sa, sc }) do
    assert(game.apply(s, 1, { "F", 7, 7 }))
    s.t = 1
    assert(game.apply(s, 1, { "F", 1, 1 }))
    s.t = 1
  end
  local ta, tc = game.views(sa, 1).target, game.views(sc, 1).target
  eq(ta, tc, "the target after the same shots at two fleets")
  eq(ta:sub(1, 1), "x", "a hit shows as a hit, not as a ship")
  eq(ta:sub(67, 67), "o", "a miss shows as a miss")
  eq(game.views(sa, 1).left, game.views(sc, 1).left, "the ships left")
  -- A sunk ship's cells show as sunk, the rest of the fleet not at all.
  local sunk = firing(mine, a)
  for _, cell in ipairs(cellsOf(a, 5)) do
    sunk.t = 1
    assert(game.apply(sunk, 1, { "F", cell[1], cell[2] }))
  end
  local target = game.views(sunk, 1).target
  eq(select(2, target:gsub("s", "")), 2, "the Destroyer's two cells show as sunk")
  eq(select(2, target:gsub("[.]", "")), 98, "nothing else shows")
  eq(game.views(sunk, 1).left, 4, "four ships left")
end

local function viewsPerSeat()
  local f1, f2 = build(ACROSS), build(DOWN)
  local s = firing(f1, f2)
  local v1, v2, v0 = game.views(s, 1), game.views(s, 2), game.views(s, 0)
  eq(v1.own, f1, "seat 1's own fleet")
  eq(v2.own, f2, "seat 2's own fleet")
  eq(v1.target, fleet.EMPTY, "seat 1's target")
  eq(v2.target, fleet.EMPTY, "seat 2's target")
  eq(v1.left, 5, "ships left on seat 2's side")
  eq(v0.fleets[1], f1, "seat 0 sees fleet 1")
  eq(v0.fleets[2], f2, "seat 0 sees fleet 2")
  eq(v0.own, nil, "seat 0 has no own fleet")
  -- No target while ships are placed.
  local p = stateWith({ f = { f1, f2 }, p = 1 })
  eq(game.views(p, 1).target, nil, "no target while placing, seat 1")
  eq(game.views(p, 2).target, nil, "no target while placing, seat 2")
  eq(game.views(p, 2).own, f2, "seat 2 sees its own fleet while seat 1 places")
end

local function maskOverAWholeGame()
  for _, seed in ipairs({ 5, 6 }) do
    local last, shots = playRandomGame(seed, function(s)
      for seat = 1, 2 do
        local v = game.views(s, seat)
        if s.p == 3 then
          local other = s.f[3 - seat]
          eq(v.target, expectedMask(other), "seat " .. seat .. "'s target against the independent mask")
          assert(not v.target:find("[^.oxs]"), "the target holds a ship letter")
          eq(v.own, s.f[seat], "own fleet")
          local afloat = 0
          for _, key in ipairs(KEYS) do
            if other:find(key, 1, true) then afloat = afloat + 1 end
          end
          eq(v.left, afloat, "ships left")
        end
      end
    end)
    assert(shots >= 17, "a game needs 17 hits")
    local st = game.status(last)
    eq(st.over, true, "the game ended")
    eq(last.t, st.winners[1], "the winner is the last shooter")
    eq(fleet.beaten(last.f[3 - st.winners[1]]), true, "the loser's fleet is beaten")
    eq(fleet.beaten(last.f[st.winners[1]]), false, "the winner's fleet is not")
  end
end

local function shotCounts()
  local f = "oo" .. string.rep(".", 90) .. "AB" .. "..." .. "ccc"
  f = f:sub(1, 100)
  eq(fleet.count_shots(f), 4, "shots counted from misses and hits")
  eq(fleet.count_shots(fleet.EMPTY), 0, "no shots")
  eq(fleet.ships_left(build(ACROSS)), 5, "five ships left")
  eq(fleet.ships_left(string.rep("o", 100)), 0, "none left")
end

-- ---- input ----

local function tapEvent(x, y) return { kind = "tap", x = x, y = y } end

local function tapCell(state, seat, ui, row, col)
  local x, y = taps.cell(row, col)
  return game.input(state, seat, ui, tapEvent(x, y))
end

local function tapButton(state, seat, ui, name)
  local x, y = taps.button(name)
  return game.input(state, seat, ui, tapEvent(x, y))
end

local function sameMove(got, want, what)
  assert(type(got) == "table", what .. ": no move")
  eq(#got, #want, what .. " length")
  for i = 1, #want do eq(got[i], want[i], what .. " field " .. i) end
end

local function inputPlacing()
  local s, ui = stateWith({}), {}
  sameMove(tapCell(s, 1, ui, 3, 4), { "P", 3, 4, "H" }, "an across tap")
  -- A ship at the edge is shifted back to fit: the Carrier across from column 10 starts at column 6.
  sameMove(tapCell(s, 1, ui, 2, 10), { "P", 2, 6, "H" }, "an across tap at the right edge")
  eq(tapButton(s, 1, ui, "rotate"), nil, "Rotate is no move")
  eq(ui.vertical, true, "Rotate flips the direction")
  sameMove(tapCell(s, 1, ui, 9, 2), { "P", 6, 2, "V" }, "a down tap at the bottom edge")
  sameMove(tapCell(s, 1, ui, 4, 4), { "P", 4, 4, "V" }, "a down tap")
  eq(tapButton(s, 1, ui, "rotate"), nil, "Rotate again")
  eq(ui.vertical, false, "Rotate flips it back")
  sameMove(tapButton(s, 1, ui, "random"), { "R" }, "Random")
  sameMove(tapButton(s, 1, ui, "clear"), { "C" }, "Clear")
  sameMove(tapButton(s, 1, ui, "ready"), { "Y" }, "Ready")
  -- The shift follows the ship: the Destroyer (2 squares) reaches column 9.
  local late = stateWith({ f = { build({ ACROSS[1], ACROSS[2], ACROSS[3], ACROSS[4] }), fleet.EMPTY } })
  sameMove(tapCell(late, 1, {}, 1, 10), { "P", 1, 9, "H" }, "the Destroyer at the right edge")
  -- A tap on the status line or off the board is nothing.
  local ox, oy = taps.off_target()
  eq(game.input(s, 1, ui, tapEvent(ox, oy)), nil, "a tap off every target")
  -- Seat 2 does not place while seat 1 does: its frame has nothing to tap.
  eq(tapCell(s, 2, {}, 3, 3), nil, "a placement tap by the waiting seat")
  eq(tapButton(s, 2, {}, "ready"), nil, "Ready by the waiting seat")
end

local function inputFiring()
  local s, ui = firing(build(ACROSS), build(DOWN)), {}
  sameMove(tapCell(s, 1, ui, 7, 8), { "F", 7, 8 }, "a shot")
  eq(tapButton(s, 1, ui, "ready"), nil, "a button tap while firing")
  local ox, oy = taps.off_target()
  eq(game.input(s, 1, ui, tapEvent(ox, oy)), nil, "a tap outside the target")
  -- Seat 0 and a finished round take no input.
  eq(tapCell(s, 0, {}, 7, 8), nil, "a tap for seat 0")
  local over = firing(build(ACROSS), string.rep("o", 83) .. "AAAAABBBBCCCDDDEE", 1)
  eq(game.status(over).over, true, "the round is over")
  eq(tapCell(over, 1, {}, 7, 8), nil, "a tap after the round is over")
end

local function inputHelpAndMessages()
  local s, ui = stateWith({}), {}
  local qx, qy = taps.help()
  eq(game.input(s, 1, ui, tapEvent(qx, qy)), nil, "the question button is no move")
  eq(ui.help, true, "the question button opens the page")
  -- Any tap closes it, whatever it hits, and is no move.
  eq(tapCell(s, 1, ui, 3, 4), nil, "a tap on the page is no move")
  eq(ui.help, nil, "the next tap closes the page")
  sameMove(tapCell(s, 1, ui, 3, 4), { "P", 3, 4, "H" }, "play goes on after the page")
  -- A message shows until the next tap.
  eq(game.input(s, 1, ui, { kind = "rejected", reason = OVERLAP }), nil, "a rejection is no move")
  eq(ui.message, OVERLAP, "the reason is kept")
  tapCell(s, 1, ui, 3, 4)
  eq(ui.message, nil, "a tap clears the message")
  for _, kind in ipairs({ "timer", "over", "long_press", "swipe" }) do
    eq(game.input(s, 1, ui, { kind = kind, x = 10, y = 10, dir = "left" }), nil, "a " .. kind .. " event")
  end
  -- The question button works in firing and while waiting too.
  local f = firing(build(ACROSS), build(DOWN))
  local fui = {}
  eq(game.input(f, 1, fui, tapEvent(qx, qy)), nil, "the question button while firing")
  eq(fui.help, true, "the page opens while firing")
  local wui = {}
  eq(game.input(stateWith({ p = 2 }), 1, wui, tapEvent(qx, qy)), nil, "the question button while waiting")
  eq(wui.help, true, "the page opens while waiting")
end

-- What each seat's frame says about the last shot: the mover's result, and for the other seat what happened to its
-- fleet (a hit never names the ship; only a sinking does). Rounds cannot pin the non-mover's frame, so the lines are
-- pinned here.
local function shotLines()
  local s = firing(build(ACROSS), build(DOWN), 1)
  eq(game.last_shot_line(s, 1), "Tap a square to fire", "the first prompt of the seat to fire")
  eq(game.last_shot_line(s, 2), "Waiting for Player 1", "the first prompt of the other seat")
  local want = {
    { 0, 0, "Miss", "Player 1 missed" },
    { 1, 1, "Hit!", "Player 1 hit your ship" },
    { 2, 3, "You sank the Cruiser!", "Player 1 sank your Cruiser!" },
    { 2, 1, "You sank the Carrier!", "Player 1 sank your Carrier!" },
  }
  for _, w in ipairs(want) do
    s.l = { 1, 1, 1, w[1], w[2] }
    eq(game.last_shot_line(s, 1), w[3], "the shooter's line for result " .. w[1])
    eq(game.last_shot_line(s, 2), w[4], "the victim's line for result " .. w[1])
    s.l = { 2, 1, 1, w[1], w[2] }
    eq(game.last_shot_line(s, 2), w[3], "seat 2 as the shooter, result " .. w[1])
    eq(game.last_shot_line(s, 1), (w[4]:gsub("Player 1", "Player 2")), "seat 1 as the victim, result " .. w[1])
  end
  -- Over: each player's count is the shots fired at the other fleet.
  local over = firing("oo" .. string.rep(".", 98), "A" .. string.rep("o", 3) .. string.rep(".", 96), 1)
  eq(game.count_line(over, 1), "Player 1 fired 4 shots", "seat 1's shots land on fleet 2")
  eq(game.count_line(over, 2), "Player 2 fired 2 shots", "seat 2's shots land on fleet 1")
  eq(game.count_line(firing(fleet.EMPTY, "o" .. string.rep(".", 99), 1), 1), "Player 1 fired 1 shot", "one shot")
end

-- A seat that has not placed yet is not ready, and seat 0's frame has no question button to tap.
local function waitingAndSeat0()
  local s = stateWith({})
  eq(game.headline(s, 1), "Player 1: place ships", "seat 1 placing")
  eq(game.headline(s, 2), "Player 2: waiting", "seat 2 before its turn to place")
  s.p = 2
  eq(game.headline(s, 1), "Player 1: fleet ready", "seat 1 after Ready")
  eq(game.headline(s, 2), "Player 2: place ships", "seat 2 placing")
  local ui = {}
  local qx, qy = taps.help()
  eq(game.input(s, 0, ui, tapEvent(qx, qy)), nil, "a tap on seat 0's frame is no move")
  eq(ui.help, nil, "seat 0 has no question button")
end

-- ---- text ----

local function wrapCount(text, size, maxw)
  local lines, line = 0, ""
  for word in text:gmatch("%S+") do
    assert(ch.text_width(word, size) <= maxw, "the word '" .. word .. "' is wider than " .. maxw)
    local try = line == "" and word or line .. " " .. word
    if line ~= "" and ch.text_width(try, size) > maxw then
      lines = lines + 1
      line = word
    else
      line = try
    end
  end
  if line ~= "" then lines = lines + 1 end
  return lines
end

local function helpPageFits()
  local lines, bottom = game.help_lines()
  assert(#lines > 0, "the page has text")
  assert(bottom <= ch.screen.h - 40, "the page fits the canvas under the harness metrics")
  for _, line in ipairs(lines) do
    assert(ch.text_width(line.text, "small") <= ch.screen.w - 48, "a help line is wider than the page")
  end
  assert(ch.text_width("HOW TO PLAY", "large") <= ch.screen.w - 48, "the page title is too wide")
  local text = ""
  for _, line in ipairs(lines) do text = text .. " " .. line.text:lower() end
  for _, word in ipairs({ "rotate", "random", "clear", "ready", "overlap", "touch", "10 by 10", "sunk", "twice", "look away" }) do
    assert(text:find(word, 1, true), "the page does not mention '" .. word .. "'")
  end
end

local function textFits()
  local L = layout.compute(ch.screen.w, ch.screen.h)
  local boardWidth = 10 * L.big.cell
  -- Headers fit between the left edge of the board and the question button.
  local states = { stateWith({}), stateWith({ p = 2 }), firing(build(ACROSS), build(DOWN), 1),
                   firing(build(ACROSS), build(DOWN), 2), firing(build(ACROSS), string.rep("o", 100), 1) }
  for _, s in ipairs(states) do
    for seat = 0, 2 do
      local text = game.headline(s, seat)
      assert(L.big.x + ch.text_width(text, "medium") <= L.question.x - 8, "the header '" .. text .. "' is too wide")
    end
  end
  -- Placement prompts, both directions, fit the board's width; the last shot's line wraps to the column.
  for _, vertical in ipairs({ false, true }) do
    local own = fleet.EMPTY
    for i = 1, 6 do
      local text = game.prompt(own, vertical)
      assert(ch.text_width(text, "medium") <= boardWidth, "the prompt '" .. text .. "' is too wide")
      own = fleet.place(own, i, 1, false) or own
    end
  end
  for seat = 1, 2 do
    for shooter = 1, 2 do
      for ship = 1, 5 do
        for result = 0, 2 do
          local state = firing(build(ACROSS), build(DOWN), 1)
          state.l = { shooter, 1, 1, result, result == 0 and 0 or ship }
          local text = game.last_shot_line(state, seat)
          assert(#text > 0, "an empty last-shot line")
          assert(wrapCount(text, "medium", L.column.w) <= 3, "'" .. text .. "' takes over three lines")
        end
      end
    end
  end
  for _, text in ipairs({ "Tap a square to fire", "Waiting for Player 2", "Their ships: 5", "Your ships: 5" }) do
    assert(wrapCount(text, "medium", L.column.w) <= 3, "'" .. text .. "' is too long for the column")
  end
  assert(ch.text_width("Their ships: 5", "small") + 36 <= L.column.w, "the ships-left line is wider than the column")
  -- Every reason fits the placement view's message line, and wraps in the column.
  for _, reason in ipairs({ OVER, PLACE_FIRST, PLACED, TAP_BOARD, NO_SHIPS, NOT_READY, ALL_PLACED, OFF_BOARD, OVERLAP,
                            NO_ROOM, ALREADY }) do
    assert(#reason <= 64, "'" .. reason .. "' is over reject_reason_bytes")
    assert(ch.text_width(reason, "small") <= boardWidth, "'" .. reason .. "' is wider than the board")
    assert(wrapCount(reason, "small", L.column.w) <= 3, "'" .. reason .. "' takes over three lines in the column")
  end
  -- The over view's counts fit their line.
  assert(ch.text_width("Player 1 fired 100 shots", "small") <= ch.screen.w - 2 * L.over[1].x, "a shot count is too wide")
end

return {
  { name = "layout at two canvas sizes", run = geometry },
  { name = "cell_at inverts cell_rect for the 100 cells of all four boards and is nil outside", run = cellAtInvertsCellRect },
  { name = "the taps module hits its targets and no other", run = tapTargets },
  { name = "the ship table", run = shipTable },
  { name = "ships are placed in order, straight, from the tapped square", run = placementOrderAndShape },
  { name = "touching ships are accepted and overlapping ones rejected", run = touchingAndOverlap },
  { name = "a ship off the board is rejected, one against the edge is not", run = offBoard },
  { name = "Random from an empty board gives legal fleets", run = randomFromEmpty },
  { name = "Random keeps one placed ship", run = randomFromOne },
  { name = "Random keeps two placed ships", run = randomFromTwo },
  { name = "Random keeps three placed ships", run = randomFromThree },
  { name = "Random keeps four placed ships", run = randomFromFour },
  { name = "Random gives different fleets for different seeds", run = randomVaries },
  { name = "Random with nothing left or no room is rejected", run = randomNeedsRoom },
  { name = "placement moves", run = placeMoves },
  { name = "Random, Clear, and Ready moves", run = randomClearReady },
  { name = "a malformed move or cell is rejected", run = badMoves },
  { name = "a move in the wrong phase is rejected", run = wrongPhase },
  { name = "a shot is a miss, a hit, or sinks its ship", run = missHitSunk },
  { name = "a repeated shot is rejected", run = repeatedShot },
  { name = "the 17th hit wins for each seat and no move follows", run = winning },
  { name = "status by phase", run = statusByPhase },
  { name = "views: the target never differs with hidden ships and never names one", run = secrecy },
  { name = "views by seat and phase", run = viewsPerSeat },
  { name = "views against an independent mask over whole games", run = maskOverAWholeGame },
  { name = "shots and ships left are counted", run = shotCounts },
  { name = "taps while placing", run = inputPlacing },
  { name = "taps while firing", run = inputFiring },
  { name = "the question button and messages", run = inputHelpAndMessages },
  { name = "the last-shot lines and shot counts each seat reads", run = shotLines },
  { name = "waiting headlines and no question button on seat 0", run = waitingAndSeat0 },
  { name = "the HOW TO PLAY page fits the canvas", run = helpPageFits },
  { name = "header, prompt, and reason texts fit", run = textFits },
}
