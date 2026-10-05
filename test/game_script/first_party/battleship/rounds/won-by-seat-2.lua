-- The mirror: seat 1 fires 17 misses first, and seat 2 hits all 17 cells of seat 1's fleet, winning on its 17th shot
-- (seat 2's shot comes second in each pair). The fleets are written out ship by ship, in the other directions from
-- won-by-seat-1's. Seat 1's fleet is hit in this order: Carrier, Battleship, Cruiser, Submarine, Destroyer.
local taps = require("taps")

local SEAT1_SHIPS = { { 6, 2, "V" }, { 1, 7, "V" }, { 10, 6, "H" }, { 3, 4, "H" }, { 9, 9, "V" } }
local SEAT2_SHIPS = { { 1, 1, "H" }, { 3, 1, "V" }, { 10, 1, "H" }, { 6, 6, "V" }, { 6, 7, "V" } }

-- The 17 cells of SEAT1_SHIPS, ship by ship.
local HITS = {
  { 6, 2 }, { 7, 2 }, { 8, 2 }, { 9, 2 }, { 10, 2 },
  { 1, 7 }, { 2, 7 }, { 3, 7 }, { 4, 7 },
  { 10, 6 }, { 10, 7 }, { 10, 8 },
  { 3, 4 }, { 3, 5 }, { 3, 6 },
  { 9, 9 }, { 10, 9 },
}

-- 17 water cells of seat 2's fleet.
local MISSES = {
  { 2, 1 }, { 2, 2 }, { 2, 3 }, { 2, 4 }, { 2, 5 }, { 2, 6 }, { 2, 7 }, { 2, 8 }, { 2, 9 }, { 2, 10 },
  { 4, 4 }, { 4, 5 }, { 4, 6 }, { 4, 7 }, { 4, 8 }, { 4, 9 }, { 4, 10 },
}

local steps = taps.place(1, SEAT1_SHIPS, false)
steps[#steps + 1] = taps.press(1, "ready")
taps.append(steps, (taps.place(2, SEAT2_SHIPS, false)))
steps[#steps + 1] = taps.press(2, "ready")

for i = 1, #HITS do
  steps[#steps + 1] = taps.shoot(1, MISSES[i][1], MISSES[i][2])
  steps[#steps + 1] = taps.shoot(2, HITS[i][1], HITS[i][2])
end

local firstShot = #steps - (#HITS + #MISSES) + 1
local function shot(seat, i) return steps[firstShot + 2 * (i - 1) + (seat == 2 and 1 or 0)] end
shot(1, 1).shows = "Miss"
shot(2, 1).shows = "Hit!"
shot(2, 5).shows = "Carrier!"
shot(2, 9).shows = "Battleship!"
steps[#steps].shows = "Player 2 wins!"

return { mode = "pass", steps = steps, winners = { 2 } }
