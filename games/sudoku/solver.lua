-- solver.lua: the technique ladder of Sudoku, ported rung for rung from crossplay's SudokuCore, as a set of cheap
-- functions over a digit string. A grid is a string of 81 characters, row by row, "0" (or ".") for an empty cell and
-- "1".."9" for a digit; the cells of the interface count from 1.
--
--   solver.grade(grid)   -> band (1 Easy, 2 Medium, 3 Hard, 4 Expert), rung; nil, rung when the ladder cannot finish it
--   solver.answer(grid)  -> the solved grid as a string, or nil when the ladder cannot finish it
--   solver.hint(grid)    -> cell, digit, rung, unit: the first placement the ladder proves, and the hardest rung it
--                           needed to reach it (a cell and never a rule's elimination); unit (0..26) is the unit of an
--                           only-cell placement, else nil; nil when there is none, the grid is complete, or its digits
--                           contradict each other (so a wrong digit is the caller's to find, against `answer`)
--   solver.band_of(rung) -> band; solver.name(rung) -> the rung's name; solver.RUNGS, solver.BANDS
--
-- The ladder: 1 only digit (naked single), 2 only cell (hidden single), 3 locked candidates (pointing and claiming),
-- 4 naked pair, 5 hidden pair, 6 naked triple, 7 hidden triple, 8 X-wing, 9 XY-wing, 10 swordfish. The cheapest rung
-- that fires is applied and the ladder restarts; the hardest rung applied is the grade (singles Easy, locked candidates
-- Medium, pairs Hard, everything above Expert). The two singles are run to a fixpoint from a work queue, which reaches
-- the same position whatever order they are taken in; every rung from 3 up takes the first instance in crossplay's
-- scanning order, so the band is the C++ ladder's. What makes it cheap: candidates and each digit's homes in each unit
-- are bit masks kept up to date as candidates go, so no rung scans cells, and a rung skips a unit (or a digit) whose
-- candidates have not changed since it last found nothing there.
--
-- The tables are built on the first call and reused (a call allocates nothing), so a game that never calls the solver
-- holds none of them. Cells are 1..81 here; units 0..26 are the rows, columns, and boxes in that order.
local solver = {}

local NAMES = {
  "Only digit", "Only cell", "Locked candidates", "Naked pair", "Hidden pair",
  "Naked triple", "Hidden triple", "X-wing", "XY-wing", "Swordfish",
}
local BANDS = { 1, 1, 2, 3, 3, 4, 4, 4, 4, 4 }
solver.RUNGS = #NAMES
solver.BANDS = 4

function solver.band_of(rung) return BANDS[rung] end

function solver.name(rung) return NAMES[rung] end

local byte, find, char = string.byte, string.find, string.char
local unpack = table.unpack

local ALL = (1 << 27) - 1 -- every unit
local BOXES = ALL & ~((1 << 18) - 1) -- units 18..26
local LINES = (1 << 18) - 1 -- units 0..17

-- Lookup tables, built by build().
local BIT, IDX, POP, LB -- digit bit, bit to digit (or slot + 1), popcount of a 9-bit mask, bit to unit of a 27-bit mask
local K1, K2, K3 -- ten times the row, column, and box unit of a cell: the base of that unit's masks in P
local N1, N2, N3 -- the cell's own position in its row, column, and box, inverted
local UB -- the three units of a cell as a 27-bit set
local UC -- UC[u][slot + 1]: the cells of unit u
local built = false

-- The working position.
local cand, val, P, stk, hst = {}, {}, {}, {}, {}
-- cand[c]: the digits a cell can still take (bit d - 1), 0 once it is filled. val[c]: the digit's character code, or 0.
-- P[u * 10 + d]: the slots (bit s) of unit u where digit d can still go, 0 when d is placed there.
-- stk: the cells (1..81) that became a naked single; hst: the u * 10 + d that became a hidden single. A queued entry
-- is looked at again when taken, since the position moves on; the naked ones go first, as the ladder takes them.
local sp, hp, dirty, dirtyD, filled, broken, usedHidden = 0, 0, 0, 0, 0, false, false
-- Per rung, the units (rungs 3 to 7) or digit-and-orientation pairs (8, 10) known to give nothing, and XY-wing's flag.
local CL = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
local SC, SM, FL, FP, PL = {}, {}, {}, {}, {} -- scratch lists

local function build()
  BIT, IDX, POP, LB = {}, {}, { [0] = 0 }, {}
  for d = 1, 9 do
    BIT[d] = 1 << (d - 1)
    IDX[BIT[d]] = d
  end
  for m = 1, 511 do POP[m] = POP[m >> 1] + (m & 1) end
  for u = 0, 26 do LB[1 << u] = u end
  K1, K2, K3, N1, N2, N3, UB = {}, {}, {}, {}, {}, {}, {}
  for c = 1, 81 do
    local r, k = (c - 1) // 9, (c - 1) % 9
    local b = (r // 3) * 3 + k // 3
    K1[c], K2[c], K3[c] = r * 10, (9 + k) * 10, (18 + b) * 10
    N1[c], N2[c], N3[c] = ~(1 << k), ~(1 << r), ~(1 << ((r % 3) * 3 + k % 3))
    UB[c] = (1 << r) | (1 << (9 + k)) | (1 << (18 + b))
  end
  UC = {}
  for u = 0, 26 do
    local cells = {}
    for s = 0, 8 do
      if u < 9 then
        cells[s + 1] = u * 9 + s + 1
      elseif u < 18 then
        cells[s + 1] = s * 9 + (u - 9) + 1
      else
        local b = u - 18
        cells[s + 1] = ((b // 3) * 3 + s // 3) * 9 + (b % 3) * 3 + s % 3 + 1
      end
    end
    UC[u] = cells
  end
  built = true
end

-- Take digit d out of the homes of cell c in its three units; queue a unit left with one home.
local function unplace(c, d)
  local k = K1[c] + d
  local x = P[k] & N1[c]
  P[k] = x
  if x & (x - 1) == 0 and x ~= 0 then
    hp = hp + 1
    hst[hp] = k
  end
  k = K2[c] + d
  x = P[k] & N2[c]
  P[k] = x
  if x & (x - 1) == 0 and x ~= 0 then
    hp = hp + 1
    hst[hp] = k
  end
  k = K3[c] + d
  x = P[k] & N3[c]
  P[k] = x
  if x & (x - 1) == 0 and x ~= 0 then
    hp = hp + 1
    hst[hp] = k
  end
  dirty = dirty | UB[c]
  dirtyD = dirtyD | BIT[d]
end

-- Strike digit d from the candidates of cell c, which holds it; queue the cell if it is left with one.
local function strike(c, d)
  local m = cand[c] & ~BIT[d]
  cand[c] = m
  if m & (m - 1) == 0 then
    if m == 0 then
      broken = true
    else
      sp = sp + 1
      stk[sp] = c
    end
  end
  unplace(c, d)
end

-- Strike every digit of mask from cell c; whether any went.
local function elim(c, mask)
  local rem = cand[c] & mask
  if rem == 0 then return false end
  repeat
    local b = rem & -rem
    rem = rem ~ b
    strike(c, IDX[b])
  until rem == 0
  return true
end

-- Put digit d in cell c (an empty cell that can take it) and strike it from every peer.
local function assign(c, d)
  local rem = cand[c]
  local bit = BIT[d]
  cand[c] = 0
  val[c] = d + 48
  filled = filled + 1
  rem = rem ~ bit
  while rem ~= 0 do
    local b = rem & -rem
    rem = rem ~ b
    unplace(c, IDX[b])
  end
  local k1, k2, k3 = K1[c] + d, K2[c] + d, K3[c] + d
  local x1, x2, x3 = P[k1] & N1[c], P[k2] & N2[c], P[k3] & N3[c]
  P[k1], P[k2], P[k3] = 0, 0, 0
  dirty = dirty | UB[c]
  dirtyD = dirtyD | bit
  local uc = UC[K1[c] // 10]
  while x1 ~= 0 do
    local b = x1 & -x1
    x1 = x1 ~ b
    local p = uc[IDX[b]]
    if cand[p] & bit ~= 0 then strike(p, d) end
  end
  uc = UC[K2[c] // 10]
  while x2 ~= 0 do
    local b = x2 & -x2
    x2 = x2 ~ b
    local p = uc[IDX[b]]
    if cand[p] & bit ~= 0 then strike(p, d) end
  end
  uc = UC[K3[c] // 10]
  while x3 ~= 0 do
    local b = x3 & -x3
    x3 = x3 ~ b
    local p = uc[IDX[b]]
    if cand[p] & bit ~= 0 then strike(p, d) end
  end
end

-- Start from the clues of a grid string; false when it is not 81 characters or the clues contradict each other.
local function load(s)
  if #s ~= 81 then return false end
  for c = 1, 81 do cand[c], val[c] = 511, 0 end
  for base = 0, 260, 10 do
    for d = 1, 9 do P[base + d] = 511 end
    P[base + 10] = 0
  end
  sp, hp, dirty, dirtyD, filled, broken, usedHidden = 0, 0, 0, 0, 0, false, false
  for r = 3, 10 do CL[r] = 0 end
  local c = 0
  while true do
    c = find(s, "[1-9]", c + 1)
    if not c then break end
    local d = byte(s, c) - 48
    if cand[c] & BIT[d] == 0 then
      broken = true
    else
      assign(c, d)
    end
  end
  return not broken
end

-- Both singles, to a fixpoint, the naked ones first.
local function closure()
  while not broken do
    if sp > 0 then
      local e = stk[sp]
      sp = sp - 1
      local m = cand[e]
      if m ~= 0 and m & (m - 1) == 0 then assign(e, IDX[m]) end
    elseif hp > 0 then
      local k = hst[hp]
      hp = hp - 1
      local x = P[k]
      if x ~= 0 and x & (x - 1) == 0 then
        usedHidden = true
        assign(UC[k // 10][IDX[x]], k % 10)
      end
    else
      return
    end
  end
end

-- Apply what changed since the last settle to what each rung knows to be empty.
local function settle()
  if dirty ~= 0 then
    local nd = ~dirty
    CL[3], CL[4], CL[5], CL[6], CL[7] = CL[3] & nd, CL[4] & nd, CL[5] & nd, CL[6] & nd, CL[7] & nd
    CL[9] = 0
    dirty = 0
  end
  if dirtyD ~= 0 then
    local nf = ~(dirtyD | (dirtyD << 9))
    CL[8], CL[10] = CL[8] & nf, CL[10] & nf
    dirtyD = 0
  end
end

-- Strike digit d from the cells of unit u in the slot mask rest.
local function strike_slots(u, rest, d)
  local uc = UC[u]
  repeat
    local b = rest & -rest
    rest = rest ~ b
    strike(uc[IDX[b]], d)
  until rest == 0
end

-- Rung 3: a digit's homes in a box lie on one line (pointing), or in a line lie in one box (claiming).
local function locked()
  local todo = ~CL[3] & BOXES
  while todo ~= 0 do
    local ub = todo & -todo
    todo = todo ~ ub
    local u = LB[ub]
    local brow, bcol = (u - 18) // 3, (u - 18) % 3
    local base = u * 10
    for d = 1, 9 do
      local pos = P[base + d]
      if pos & (pos - 1) ~= 0 then
        local rest = 0
        if pos & 504 == 0 then
          local row = brow * 3
          rest = P[row * 10 + d] & ~(7 << (bcol * 3))
          if rest ~= 0 then
            strike_slots(row, rest, d)
            return true
          end
        elseif pos & 455 == 0 then
          local row = brow * 3 + 1
          rest = P[row * 10 + d] & ~(7 << (bcol * 3))
          if rest ~= 0 then
            strike_slots(row, rest, d)
            return true
          end
        elseif pos & 63 == 0 then
          local row = brow * 3 + 2
          rest = P[row * 10 + d] & ~(7 << (bcol * 3))
          if rest ~= 0 then
            strike_slots(row, rest, d)
            return true
          end
        end
        local col
        if pos & 438 == 0 then
          col = bcol * 3
        elseif pos & 365 == 0 then
          col = bcol * 3 + 1
        elseif pos & 219 == 0 then
          col = bcol * 3 + 2
        end
        if col then
          rest = P[(9 + col) * 10 + d] & ~(7 << (brow * 3))
          if rest ~= 0 then
            strike_slots(9 + col, rest, d)
            return true
          end
        end
      end
    end
    CL[3] = CL[3] | ub
  end
  todo = ~CL[3] & LINES
  while todo ~= 0 do
    local ub = todo & -todo
    todo = todo ~ ub
    local u = LB[ub]
    local base = u * 10
    for d = 1, 9 do
      local pos = P[base + d]
      if pos & (pos - 1) ~= 0 then
        local t = -1
        if pos & 504 == 0 then
          t = 0
        elseif pos & 455 == 0 then
          t = 1
        elseif pos & 63 == 0 then
          t = 2
        end
        if t >= 0 then
          local box, linemask
          if u < 9 then
            box = (u // 3) * 3 + t
            linemask = 7 << (3 * (u % 3))
          else
            box = t * 3 + (u - 9) // 3
            linemask = 73 << ((u - 9) % 3)
          end
          local rest = P[(18 + box) * 10 + d] & ~linemask
          if rest ~= 0 then
            strike_slots(18 + box, rest, d)
            return true
          end
        end
      end
    end
    CL[3] = CL[3] | ub
  end
  return false
end

-- Rungs 4 and 6: size cells of a unit whose candidates span exactly size digits own them.
local function naked(rung, size)
  local todo = ~CL[rung] & ALL
  while todo ~= 0 do
    local ub = todo & -todo
    todo = todo ~ ub
    local uc = UC[LB[ub]]
    local open, n = 0, 0
    for s = 1, 9 do
      local c = uc[s]
      local m = cand[c]
      if m ~= 0 then
        open = open + 1
        local cnt = POP[m]
        if cnt >= 2 and cnt <= size then
          n = n + 1
          SC[n], SM[n] = c, m
        end
      end
    end
    if open > size and n >= size then
      for i = 1, n - 1 do
        for j = i + 1, n do
          local spanned = SM[i] | SM[j]
          if size == 2 then
            if POP[spanned] == 2 then
              local prog = false
              local ci, cj = SC[i], SC[j]
              for s = 1, 9 do
                local c = uc[s]
                if c ~= ci and c ~= cj and elim(c, spanned) then prog = true end
              end
              if prog then return true end
            end
          else
            for k = j + 1, n do
              local sp3 = spanned | SM[k]
              if POP[sp3] == 3 then
                local prog = false
                local ci, cj, ck = SC[i], SC[j], SC[k]
                for s = 1, 9 do
                  local c = uc[s]
                  if c ~= ci and c ~= cj and c ~= ck and elim(c, sp3) then prog = true end
                end
                if prog then return true end
              end
            end
          end
        end
      end
    end
    CL[rung] = CL[rung] | ub
  end
  return false
end

-- Rungs 5 and 7: size digits of a unit whose homes are exactly size cells; those cells keep only those digits.
local function hidden(rung, size)
  local todo = ~CL[rung] & ALL
  while todo ~= 0 do
    local ub = todo & -todo
    todo = todo ~ ub
    local u = LB[ub]
    local uc = UC[u]
    local base = u * 10
    local n = 0
    for d = 1, 9 do
      local pos = P[base + d]
      local cnt = POP[pos]
      if cnt >= 2 and cnt <= size then
        n = n + 1
        SC[n], SM[n] = d, pos
      end
    end
    if n >= size then
      for i = 1, n - 1 do
        for j = i + 1, n do
          local covered = SM[i] | SM[j]
          local wanted = BIT[SC[i]] | BIT[SC[j]]
          local ks, ke = 0, 0
          if size == 3 then ks, ke = j + 1, n end
          for k = ks, ke do
            local cov, want = covered, wanted
            if size == 3 then
              cov = cov | SM[k]
              want = want | BIT[SC[k]]
            end
            if POP[cov] == size then
              local prog = false
              local keep = 511 & ~want
              repeat
                local b = cov & -cov
                cov = cov ~ b
                if elim(uc[IDX[b]], keep) then prog = true end
              until cov == 0
              if prog then return true end
            end
          end
        end
      end
    end
    CL[rung] = CL[rung] | ub
  end
  return false
end

-- Rungs 8 and 10: size lines on which a digit's homes span exactly size crossing lines take it from those crossings.
local function fish(rung, size)
  for o = 0, 1 do
    for d = 1, 9 do
      local fb = 1 << (o * 9 + d - 1)
      if CL[rung] & fb == 0 then
        local found = 0
        for i = 0, 8 do
          local pos = P[(o * 9 + i) * 10 + d]
          local cnt = POP[pos]
          if cnt >= 2 and cnt <= size then
            found = found + 1
            FL[found], FP[found] = i, pos
          end
        end
        if found >= size then
          for i = 1, found - 1 do
            for j = i + 1, found do
              local ks, ke = 0, 0
              if size == 3 then ks, ke = j + 1, found end
              for k = ks, ke do
                local covered = FP[i] | FP[j]
                local lines = (1 << FL[i]) | (1 << FL[j])
                if size == 3 then
                  covered = covered | FP[k]
                  lines = lines | (1 << FL[k])
                end
                if POP[covered] == size then
                  local prog = false
                  repeat
                    local b = covered & -covered
                    covered = covered ~ b
                    local cu = IDX[b] - 1
                    if o == 0 then cu = cu + 9 end
                    local rest = P[cu * 10 + d] & ~lines
                    if rest ~= 0 then
                      prog = true
                      strike_slots(cu, rest, d)
                    end
                  until covered == 0
                  if prog then return true end
                end
              end
            end
          end
        end
        CL[rung] = CL[rung] | fb
      end
    end
  end
  return false
end

-- The peers of a cell in ascending order, in PL[1..20].
local function peers_of(c)
  local pr, pc = K1[c] // 10, K2[c] // 10 - 9
  local bc = pc - pc % 3
  local band = pr // 3
  local n = 0
  for r = 0, 8 do
    local base = r * 9 + 1
    if r == pr then
      for k = 0, 8 do
        if k ~= pc then
          n = n + 1
          PL[n] = base + k
        end
      end
    elseif r // 3 == band then
      for k = bc, bc + 2 do
        n = n + 1
        PL[n] = base + k
      end
    else
      n = n + 1
      PL[n] = base + pc
    end
  end
end

-- Rung 9: a pivot {a, b} with pincers {a, c} and {b, c} in its peers: c goes from every cell that sees both pincers.
local function xywing()
  if CL[9] == 1 then return false end
  for pivot = 1, 81 do
    local pm = cand[pivot]
    if POP[pm] == 2 then
      local bit_a = pm & -pm
      local bit_b = pm ~ bit_a
      peers_of(pivot)
      for i = 1, 20 do
        local first = PL[i]
        local fm = cand[first]
        if POP[fm] == 2 then
          local has_a, has_b = fm & bit_a ~= 0, fm & bit_b ~= 0
          if has_a ~= has_b then
            local shared = has_a and bit_a or bit_b
            local third = fm ~ shared
            local wanted = (pm ~ shared) | third
            for j = 1, 20 do
              local second = PL[j]
              if second ~= first and cand[second] == wanted then
                local prog = false
                local d3 = IDX[third]
                for t = 1, 3 do
                  local ku = (t == 1 and K1 or t == 2 and K2 or K3)[first]
                  local x = P[ku + d3]
                  local uc = UC[ku // 10]
                  while x ~= 0 do
                    local b = x & -x
                    x = x ~ b
                    local c = uc[IDX[b]]
                    if
                      c ~= first
                      and c ~= second
                      and cand[c] & third ~= 0
                      and (K1[c] == K1[second] or K2[c] == K2[second] or K3[c] == K3[second])
                    then
                      strike(c, d3)
                      prog = true
                    end
                  end
                end
                if prog then return true end
              end
            end
          end
        end
      end
    end
  end
  CL[9] = 1
  return false
end

local function fire(rung)
  if rung == 3 then return locked() end
  if rung == 4 then return naked(4, 2) end
  if rung == 5 then return hidden(5, 2) end
  if rung == 6 then return naked(6, 3) end
  if rung == 7 then return hidden(7, 3) end
  if rung == 8 then return fish(8, 2) end
  if rung == 9 then return xywing() end
  return fish(10, 3)
end

-- Run the ladder from the loaded position: whether it ended solved, and the hardest rung it applied.
local function run()
  local hardest = 1
  while true do
    closure()
    if usedHidden and hardest < 2 then hardest = 2 end
    if broken then return false, hardest end
    if filled == 81 then return true, hardest end
    settle()
    local fired = false
    for rung = 3, 10 do
      if fire(rung) then
        if rung > hardest then hardest = rung end
        fired = true
        break
      end
    end
    if not fired then return false, hardest end
  end
end

function solver.grade(s)
  if not built then build() end
  if not load(s) then return nil, 0 end
  local ok, hardest = run()
  if not ok then return nil, hardest end
  return BANDS[hardest], hardest
end

function solver.answer(s)
  if not built then build() end
  if not load(s) then return nil end
  if not run() then return nil end
  return char(unpack(val, 1, 81))
end

-- The first single by crossplay's order: the lowest cell with one candidate left, else the first digit and unit with
-- one home left. Returns cell, digit, 1 or 2, and the unit for a hidden single.
local function first_single()
  for c = 1, 81 do
    local m = cand[c]
    if m ~= 0 and m & (m - 1) == 0 then return c, IDX[m], 1, nil end
  end
  for u = 0, 26 do
    for d = 1, 9 do
      local x = P[u * 10 + d]
      if x ~= 0 and x & (x - 1) == 0 then
        local m = cand[UC[u][IDX[x]]]
        if m & (m - 1) ~= 0 then return UC[u][IDX[x]], d, 2, u end
      end
    end
  end
  return nil
end

function solver.hint(s)
  if not built then build() end
  if not load(s) then return nil end
  local hardest = 0
  while true do
    if broken or filled == 81 then return nil end
    if sp > 0 or hp > 0 then
      local c, d, via, unit = first_single()
      if c then return c, d, hardest > via and hardest or via, unit end
      sp, hp = 0, 0
    end
    settle()
    local fired = false
    for rung = 3, 10 do
      if fire(rung) then
        if rung > hardest then hardest = rung end
        fired = true
        break
      end
    end
    if not fired then return nil end
  end
end

return solver
