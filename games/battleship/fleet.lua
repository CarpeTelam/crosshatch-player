-- Battleship's rules on one fleet: a 10 x 10 board kept as a 100-char string, row by row (cell (row, col) is char
-- (row - 1) * 10 + col). No `ch` here: the functions are pure, so main.lua draws from their results and the
-- game's own checks call them directly.
--
--   "."    water
--   "o"    a miss the other seat fired here
--   a - e  an intact ship cell: a Carrier (5), b Battleship (4), c Cruiser (3), d Submarine (3), e Destroyer (2)
--   A - E  a hit cell of that ship
-- A ship is sunk when none of its letters is lowercase; the fleet is beaten when no lowercase letter is left.
local fleet = {}

fleet.SIZE = 10
fleet.EMPTY = string.rep(".", 100)
fleet.SHIPS = {
  { key = "a", name = "Carrier", len = 5 },
  { key = "b", name = "Battleship", len = 4 },
  { key = "c", name = "Cruiser", len = 3 },
  { key = "d", name = "Submarine", len = 3 },
  { key = "e", name = "Destroyer", len = 2 },
}

fleet.ALREADY_PLACED = "All five ships are placed"
fleet.OFF_BOARD = "That ship would go off the board"
fleet.OVERLAP = "Ships cannot overlap"
fleet.NO_ROOM = "No room for the other ships; tap Clear"
fleet.ALREADY_FIRED = "You already fired there"

local UPPER = { a = "A", b = "B", c = "C", d = "D", e = "E" }

local function setCell(f, at, char) return f:sub(1, at - 1) .. char .. f:sub(at + 1) end

-- The index (1..5) of the next ship to place: the first whose letter is on no cell, or nil with all five placed.
function fleet.next_ship(f)
  for i, ship in ipairs(fleet.SHIPS) do
    if not f:find(ship.key, 1, true) then return i end
  end
  return nil
end

-- Whether every ship is sunk: no lowercase ship letter is left.
function fleet.beaten(f) return f:find("[a-e]") == nil end

-- The ships of the fleet not yet sunk.
function fleet.ships_left(f)
  local n = 0
  for _, ship in ipairs(fleet.SHIPS) do
    if f:find(ship.key, 1, true) then n = n + 1 end
  end
  return n
end

-- Shots fired at this fleet: its misses and its hit cells.
function fleet.count_shots(f)
  local n = 0
  for _ in f:gmatch("[oA-E]") do n = n + 1 end
  return n
end

-- The cell indexes of a ship of `len` squares anchored at (row, col), going across or down, or nil when it would go
-- off the board.
local function span(row, col, len, vertical)
  local lastRow, lastCol = row, col
  if vertical then lastRow = row + len - 1 else lastCol = col + len - 1 end
  if row < 1 or col < 1 or lastRow > fleet.SIZE or lastCol > fleet.SIZE then return nil end
  local cells = {}
  for i = 0, len - 1 do
    local r, c = row, col
    if vertical then r = r + i else c = c + i end
    cells[i + 1] = (r - 1) * fleet.SIZE + c
  end
  return cells
end

-- Places the next ship with its first square at (row, col): the new fleet string, or nil and a reason.
function fleet.place(f, row, col, vertical)
  local index = fleet.next_ship(f)
  if not index then return nil, fleet.ALREADY_PLACED end
  local ship = fleet.SHIPS[index]
  local cells = span(row, col, ship.len, vertical)
  if not cells then return nil, fleet.OFF_BOARD end
  for _, at in ipairs(cells) do
    if f:sub(at, at) ~= "." then return nil, fleet.OVERLAP end
  end
  for _, at in ipairs(cells) do f = setCell(f, at, ship.key) end
  return f
end

-- The fleet read column by column, so a ship down a column is a run of the string like one across a row.
local function transpose(f)
  local parts = {}
  for col = 1, fleet.SIZE do
    for row = 1, fleet.SIZE do
      local at = (row - 1) * fleet.SIZE + col
      parts[#parts + 1] = f:sub(at, at)
    end
  end
  return table.concat(parts)
end

-- Every legal anchor of the next ship, as { row, col, vertical }.
local function anchors(f, len)
  local water = string.rep(".", len)
  local list = {}
  local down = transpose(f)
  for row = 1, fleet.SIZE do
    for col = 1, fleet.SIZE - len + 1 do
      local at = (row - 1) * fleet.SIZE + col
      if f:sub(at, at + len - 1) == water then list[#list + 1] = { row, col, false } end
    end
  end
  for col = 1, fleet.SIZE do
    for row = 1, fleet.SIZE - len + 1 do
      local at = (col - 1) * fleet.SIZE + row
      if down:sub(at, at + len - 1) == water then list[#list + 1] = { row, col, true } end
    end
  end
  return list
end

-- Places every ship not placed yet at a random legal spot, the earlier ones kept: the new fleet string, or nil and a
-- reason (all five placed, or no room left for one of them: nothing changes then).
function fleet.random_fill(f)
  local first = fleet.next_ship(f)
  if not first then return nil, fleet.ALREADY_PLACED end
  for i = first, #fleet.SHIPS do
    local ship = fleet.SHIPS[i]
    local list = anchors(f, ship.len)
    if #list == 0 then return nil, fleet.NO_ROOM end
    local pick = list[math.random(#list)]
    f = assert(fleet.place(f, pick[1], pick[2], pick[3]))
  end
  return f
end

-- Fires at (row, col): the new fleet string, the result (0 miss, 1 hit, 2 sunk), and the ship's number (0 for a
-- miss); or nil and a reason for a square already shot.
function fleet.shot(f, row, col)
  local at = (row - 1) * fleet.SIZE + col
  local char = f:sub(at, at)
  if char == "o" or char:find("%u") then return nil, fleet.ALREADY_FIRED end
  if char == "." then return setCell(f, at, "o"), 0, 0 end
  local f2 = setCell(f, at, UPPER[char])
  for i, ship in ipairs(fleet.SHIPS) do
    if ship.key == char then
      if f2:find(char, 1, true) then return f2, 1, i end
      return f2, 2, i
    end
  end
end

-- What the other seat may see of this fleet: "." water and intact ships alike, "o" a miss, "x" a hit, "s" a hit
-- cell of a sunk ship. An intact ship never shows, and a hit never names its ship.
function fleet.mask(f)
  local out = f:gsub("[a-e]", ".")
  for _, ship in ipairs(fleet.SHIPS) do
    local upper = UPPER[ship.key]
    if out:find(upper, 1, true) then
      out = out:gsub(upper, f:find(ship.key, 1, true) and "x" or "s")
    end
  end
  return out
end

return fleet
