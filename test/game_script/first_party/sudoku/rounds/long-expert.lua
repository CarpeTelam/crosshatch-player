-- One long Expert round that mixes everything that costs the played game heap, in the order that costs it most: HINT first
-- (it loads the solver, which took the played VM to about 176 to 192 KB of its 256 KB at the first HINT), then FILL NOTES,
-- digit notes (NOTES AS: DIGITS), CHECK with a wrong digit and without, and the rest of the grid written cell by cell, ending
-- on the end screen. It is played at the device's Lua heap cap, and then again with the cap 16 KiB lower (RoundPlayer.h,
-- HEAP_MARGIN_BYTES): if HINT, CHECK, FILL NOTES, the notes or the writes grow the played game's heap past that margin,
-- this round is the one that fails, naming the margin and the cap tried.
local taps = require("taps")
local grid = require("grid")
local solver = require("solver")

local WRITES_BEFORE_CHECK = 14 -- the digits written before the first CHECK
local NOTED = 3 -- the cells whose digit notes are toggled

-- The grid after the digits written so far, in the game's notation (a player's digit is the letter a to i).
local function written(state, answer, cells)
  local v = state.v
  for _, c in ipairs(cells) do
    v = v:sub(1, c - 1) .. string.char(96 + tonumber(answer:sub(c, c))) .. v:sub(c + 1)
  end
  return v
end

return {
  mode = "solo",
  settings = { level = "Expert" },
  seed = 61,
  steps = function(state)
    local answer, empties = taps.answer(state), taps.empties(state)
    local cell = solver.hint(state.v)
    local function write(steps, c) return taps.write(steps, c, tonumber(answer:sub(c, c))) end

    -- HINT: the first thing, with the solver not yet loaded; its tap on the cell it selected lets go of it.
    local steps = taps.menu_row({}, 1, {})
    taps.cell(steps, cell, { move = false })
    -- FILL NOTES: every candidate of every empty cell, as marks.
    taps.menu_row(steps, 2, {})
    -- NOTES AS: DIGITS, then close the MENU.
    taps.menu_row(steps, 6, { move = false, shows = "NOTES AS: DIGITS" })
    taps.menu(steps, 8, { move = false })

    -- The first digits, then CHECK: nothing wrong yet.
    local first = {}
    for i = 1, WRITES_BEFORE_CHECK do
      write(steps, empties[i])
      first[#first + 1] = empties[i]
    end
    taps.menu_row(steps, 3, { move = false, shows = "All correct" })

    -- Digit notes with NOTES on: the first two candidates of three cells not written yet (a note already there comes off).
    local cand = grid.candidates(written(state, answer, first))
    taps.rail(steps, 1, { move = false })
    for i = WRITES_BEFORE_CHECK + 1, WRITES_BEFORE_CHECK + NOTED do
      local c, n = empties[i], 0
      for d = 1, 9 do
        if cand[c] & (1 << d - 1) ~= 0 and n < 2 then
          n = n + 1
          taps.note(steps, c, d)
        end
      end
    end
    taps.rail(steps, 1, { move = false })

    -- A wrong digit, CHECK strikes it, the right digit replaces it, CHECK says all correct.
    local c = empties[WRITES_BEFORE_CHECK + NOTED + 1]
    taps.write(steps, c, taps.wrong_digit(state, c))
    taps.menu_row(steps, 3, { move = false, shows = "1 wrong digit" })
    write(steps, c)
    taps.menu_row(steps, 3, { move = false, shows = "All correct" })

    -- The rest of the grid, the clock moving 5 seconds before the last digit; HINT was used, so no best time is kept.
    for i = WRITES_BEFORE_CHECK + 1, #empties do
      if empties[i] ~= c then write(steps, empties[i]) end
    end
    steps[#steps].wait = 5000
    steps[#steps].shows = "No best time after a hint"
    return steps
  end,
  winners = { 1 },
}
