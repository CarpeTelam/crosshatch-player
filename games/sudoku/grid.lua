-- grid.lua: pure helpers over the game's grid string (no ch, no state): the digit of a character, which cells clash,
-- which digits an empty cell can take, whether a peer holds a digit, and the symmetry a new puzzle is dealt through.
--
-- A grid is 81 characters, row by row: "0" an empty cell, "1".."9" a clue, "a".."i" a digit of the player's (1 to 9).
-- Cells count from 1. Notes are 16 bits a cell, bit d - 1 for digit d, packed two bytes a cell (FMT).
local grid = {}

local byte = string.byte

-- The digit (1..9) of a character's byte, or nil for an empty cell.
local DIG = {}
for d = 1, 9 do
  DIG[48 + d] = d
  DIG[96 + d] = d
end
grid.DIG = DIG

-- A player's letter to its digit character, for the solver.
grid.LETTER = {}
for d = 1, 9 do grid.LETTER[string.char(96 + d)] = string.char(48 + d) end

-- 81 notes, 16 bits each, little endian.
grid.FMT = "<" .. string.rep("I2", 81)

-- The row, column, and box (0..26: rows, columns, boxes) of cell c.
local function units(c)
  local r, k = (c - 1) // 9, (c - 1) % 9
  return r, 9 + k, 18 + r // 3 * 3 + k // 3
end
grid.units = units

-- The cells (a set) whose digit repeats in their row, column, or box; nil when none does.
function grid.clashes(v)
  local once, twice = {}, {}
  for u = 0, 26 do once[u], twice[u] = 0, 0 end
  local ds = { byte(v, 1, 81) }
  for c = 1, 81 do
    local d = DIG[ds[c]]
    if d then
      local bit = 1 << (d - 1)
      local a, b, e = units(c)
      twice[a], twice[b], twice[e] = twice[a] | once[a] & bit, twice[b] | once[b] & bit, twice[e] | once[e] & bit
      once[a], once[b], once[e] = once[a] | bit, once[b] | bit, once[e] | bit
    end
  end
  local out
  for c = 1, 81 do
    local d = DIG[ds[c]]
    if d then
      local a, b, e = units(c)
      if (twice[a] | twice[b] | twice[e]) & (1 << (d - 1)) ~= 0 then
        out = out or {}
        out[c] = true
      end
    end
  end
  return out
end

-- Per cell, the digits (a 9-bit mask) it can take given every digit on the grid, 0 for a filled cell.
function grid.candidates(v)
  local used = {}
  for u = 0, 26 do used[u] = 0 end
  local ds = { byte(v, 1, 81) }
  for c = 1, 81 do
    local d = DIG[ds[c]]
    if d then
      local a, b, e = units(c)
      local bit = 1 << (d - 1)
      used[a], used[b], used[e] = used[a] | bit, used[b] | bit, used[e] | bit
    end
  end
  local out = {}
  for c = 1, 81 do
    if DIG[ds[c]] then
      out[c] = 0
    else
      local a, b, e = units(c)
      out[c] = 511 & ~(used[a] | used[b] | used[e])
    end
  end
  return out
end

-- Whether a peer of cell c (its row, column, or box, not c itself) holds digit d.
function grid.peer_has(v, c, d)
  local r, k = (c - 1) // 9, (c - 1) % 9
  local br, bc = r // 3 * 3, k // 3 * 3
  for i = 0, 8 do
    if i ~= k and DIG[byte(v, r * 9 + i + 1)] == d then return true end
    if i ~= r and DIG[byte(v, i * 9 + k + 1)] == d then return true end
    local rr, cc = br + i // 3, bc + i % 3
    if (rr ~= r or cc ~= k) and DIG[byte(v, rr * 9 + cc + 1)] == d then return true end
  end
  return false
end

local function shuffle(t, rand)
  for i = #t, 2, -1 do
    local j = rand(i)
    t[i], t[j] = t[j], t[i]
  end
end

-- The nine row (or column) indices 0..8 in a random order that keeps the three bands (stacks) whole: the bands in a
-- random order, each band's rows in a random order.
local function order(rand)
  local bands, out = { 0, 1, 2 }, {}
  shuffle(bands, rand)
  for _, b in ipairs(bands) do
    local w = { 0, 1, 2 }
    shuffle(w, rand)
    for _, i in ipairs(w) do out[#out + 1] = b * 3 + i end
  end
  return out
end

-- The grid s (81 characters, "0" or a digit) dealt through a random symmetry: the digits relabelled, bands and the
-- rows in a band permuted, stacks and the columns in a stack permuted, and a transposition half the time. Each keeps
-- a valid puzzle valid and a unique one unique. rand(n) gives an integer 1..n (math.random).
function grid.symmetry(s, rand)
  local map = { 1, 2, 3, 4, 5, 6, 7, 8, 9 }
  shuffle(map, rand)
  local rows, cols = order(rand), order(rand)
  local flip = rand(2) == 2
  local out = {}
  for r = 1, 9 do
    for c = 1, 9 do
      local rr, cc = rows[r], cols[c]
      if flip then rr, cc = cc, rr end
      local d = byte(s, rr * 9 + cc + 1) - 48
      out[#out + 1] = d == 0 and "0" or string.char(48 + map[d])
    end
  end
  return table.concat(out)
end

return grid
