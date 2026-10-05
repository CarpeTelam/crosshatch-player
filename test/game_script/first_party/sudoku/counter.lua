-- counter.lua: an independent count of the solutions of a Sudoku grid, for the checks. It shares nothing with the game
-- (the game's solver is the thing under test): bitmask rows, columns, and boxes, and a depth-first search that, at each
-- node, takes a forced move when there is one (a digit with a single home in some unit, else the empty cell with the
-- fewest candidates; a cell or a digit with no place ends that branch) and stops at the second solution.
--
--   counter.count(grid) -> n, solution
--
-- grid is 81 characters, row by row, "0" or "." for an empty cell. n is 0 (no solution, or the givens repeat a digit in
-- a row, column, or box, or the grid is not 81 cells), 1, or 2 (two or more); solution is the first solution found, as
-- a string of 81 digits, or nil when n is 0.
local counter = {}

local BIT, LOW = {}, {} -- the bit of a digit, and the digit of a lone bit
for d = 1, 9 do
  BIT[d] = 1 << (d - 1)
  LOW[BIT[d]] = d
end
local POP = { [0] = 0 }
for m = 1, 511 do POP[m] = POP[m >> 1] + (m & 1) end

-- Units 1..9 are the rows, 10..18 the columns, 19..27 the boxes; a cell's three.
local UR, UK, UB = {}, {}, {}
for c = 1, 81 do
  local r, k = (c - 1) // 9, (c - 1) % 9
  UR[c], UK[c], UB[c] = r + 1, 10 + k, 19 + (r // 3) * 3 + k // 3
end

function counter.count(grid)
  if type(grid) ~= "string" or #grid ~= 81 then return 0, nil end
  local used, value, empty, n = {}, {}, {}, 0
  for u = 1, 27 do used[u] = 0 end
  for c = 1, 81 do
    local ch = grid:sub(c, c)
    local d = tonumber(ch)
    if ch == "." or d == 0 then
      n = n + 1
      empty[n] = c
      value[c] = 0
    elseif d and d >= 1 and d <= 9 and #ch == 1 then
      local bit = BIT[d]
      if (used[UR[c]] | used[UK[c]] | used[UB[c]]) & bit ~= 0 then return 0, nil end
      used[UR[c]], used[UK[c]], used[UB[c]] = used[UR[c]] | bit, used[UK[c]] | bit, used[UB[c]] | bit
      value[c] = d
    else
      return 0, nil
    end
  end

  local found, first = 0, nil
  local once, twice = {}, {}
  local function search(left)
    if left == 0 then
      found = found + 1
      if found == 1 then first = table.concat(value) end
      return
    end
    for u = 1, 27 do once[u], twice[u] = 0, 0 end
    local best, bestAt, bestFree = 10, 0, 0
    for i = 1, left do
      local c = empty[i]
      local ur, uk, ub = UR[c], UK[c], UB[c]
      local free = ~(used[ur] | used[uk] | used[ub]) & 511
      local count = POP[free]
      if count == 0 then return end
      if count < best then best, bestAt, bestFree = count, i, free end
      local o = once[ur]
      twice[ur], once[ur] = twice[ur] | (o & free), o | free
      o = once[uk]
      twice[uk], once[uk] = twice[uk] | (o & free), o | free
      o = once[ub]
      twice[ub], once[ub] = twice[ub] | (o & free), o | free
    end
    -- a unit with a digit that has no home ends the branch; one with a single home is a forced move
    local forcedUnit, forcedBit = 0, 0
    for u = 1, 27 do
      local o = once[u]
      if (~used[u] & 511) ~= o then return end
      local lone = o & ~twice[u]
      if lone ~= 0 and forcedUnit == 0 then forcedUnit, forcedBit = u, lone & -lone end
    end
    if forcedUnit ~= 0 then
      for i = 1, left do
        local c = empty[i]
        if
          (UR[c] == forcedUnit or UK[c] == forcedUnit or UB[c] == forcedUnit)
          and ~(used[UR[c]] | used[UK[c]] | used[UB[c]]) & forcedBit ~= 0
        then
          bestAt, bestFree = i, forcedBit
          break
        end
      end
    end
    local c = empty[bestAt]
    empty[bestAt], empty[left] = empty[left], c -- out of the list while it is filled
    local ur, uk, ub = UR[c], UK[c], UB[c]
    while bestFree ~= 0 and found < 2 do
      local bit = bestFree & -bestFree
      bestFree = bestFree ~ bit
      used[ur], used[uk], used[ub] = used[ur] | bit, used[uk] | bit, used[ub] | bit
      value[c] = LOW[bit]
      search(left - 1)
      used[ur], used[uk], used[ub] = used[ur] ~ bit, used[uk] ~ bit, used[ub] ~ bit
    end
    value[c] = 0
    empty[left], empty[bestAt] = empty[bestAt], empty[left]
  end
  search(n)
  return found, first
end

return counter
