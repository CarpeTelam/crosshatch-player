-- Seat 1 hits all 17 cells of seat 2's fleet while seat 2 fires 16 misses: seat 1 wins on its 17th shot. The fleets
-- are written out ship by ship (the tapped square is a ship's first; both directions, touching ships). Seat 2's
-- fleet is hit in this order: Carrier, Battleship, Cruiser, Submarine, Destroyer.
local taps = require("taps")

local SEAT1_SHIPS = { { 1, 1, "H" }, { 2, 1, "H" }, { 1, 10, "V" }, { 5, 3, "V" }, { 7, 4, "H" } }
local SEAT2_SHIPS = { { 10, 1, "H" }, { 9, 1, "H" }, { 3, 3, "V" }, { 3, 8, "H" }, { 4, 10, "V" } }

-- The 17 cells of SEAT2_SHIPS, ship by ship.
local HITS = {
  { 10, 1 }, { 10, 2 }, { 10, 3 }, { 10, 4 }, { 10, 5 },
  { 9, 1 }, { 9, 2 }, { 9, 3 }, { 9, 4 },
  { 3, 3 }, { 4, 3 }, { 5, 3 },
  { 3, 8 }, { 3, 9 }, { 3, 10 },
  { 4, 10 }, { 5, 10 },
}

-- 16 water cells of seat 1's fleet.
local MISSES = {
  { 8, 1 }, { 8, 2 }, { 8, 3 }, { 8, 4 }, { 8, 5 }, { 8, 6 }, { 8, 7 }, { 8, 8 }, { 8, 9 }, { 8, 10 },
  { 9, 1 }, { 9, 2 }, { 9, 3 }, { 9, 4 }, { 9, 5 }, { 9, 6 },
}

local steps = taps.place(1, SEAT1_SHIPS, false)
steps[#steps + 1] = taps.press(1, "ready")
taps.append(steps, (taps.place(2, SEAT2_SHIPS, false)))
steps[#steps + 1] = taps.press(2, "ready")

for i = 1, #HITS do
  steps[#steps + 1] = taps.shoot(1, HITS[i][1], HITS[i][2])
  if MISSES[i] then steps[#steps + 1] = taps.shoot(2, MISSES[i][1], MISSES[i][2]) end
end

-- What the frames say: the Carrier's first cell is a hit and its last sinks it (seat 1's shots 1 and 5), seat 2's first
-- shot misses, shot 12 sinks the Cruiser, and the last frame names the winner.
local firstShot = #steps - (#HITS + #MISSES) + 1
local function shot(seat, i) return steps[firstShot + 2 * (i - 1) + (seat == 2 and 1 or 0)] end
shot(1, 1).shows = "Hit!"
shot(2, 1).shows = "Miss"
shot(1, 5).shows = "Carrier!"
shot(1, 12).shows = "Cruiser!"
steps[#steps].shows = "Player 1 wins!"

return { mode = "pass", steps = steps, winners = { 1 } }
