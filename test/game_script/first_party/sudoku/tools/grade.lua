-- grade.lua: grades candidate puzzles with the game's own solver and counts the instructions each call takes, for
-- make_bank.py. It runs under a host Lua built from lib/lua/src (the VM the device runs), not under the game's sandbox:
-- it uses io and debug, which a game cannot.
--
--   lua grade.lua <path to games/sudoku/solver.lua> <path to counter.lua> < candidates
--
-- stdin: one candidate per line, "<hash12> <81 digits>". stdout, first a line
--   build <n>
-- the instructions of the solver's one-time table building (the first call minus a second, equal call), then per
-- candidate a line
--   <hash12> <band> <rung> <grade> <answer> <hint> <count> <solutions>
-- band is 1..4, or 0 when the ladder cannot finish the puzzle; rung is the hardest rung applied (0 when the clues
-- contradict each other); grade, answer, and hint are the instructions (counted one by one by the count hook) of
-- solver.grade(grid), solver.answer(grid), and solver.hint(grid), each from a fresh grid with the tables already built:
-- the calls the checks make (grade) and the first HINT or CHECK of a game makes (answer, then hint). count is the
-- instructions of counter.count(grid), the independent solution counter the checks run on every puzzle, and solutions
-- what it found (0, 1, or 2 for two or more).
local path, counter_path = arg[1], arg[2]
if not path or not counter_path then
  io.stderr:write("usage: lua grade.lua <path to solver.lua> <path to counter.lua> < candidates\n")
  os.exit(2)
end
local solver = assert(loadfile(path))()
local counter = assert(loadfile(counter_path))()

local count = 0
local function hook() count = count + 1 end

local function measure(f, ...)
  count = 0
  debug.sethook(hook, "", 1)
  local a, b = f(...)
  debug.sethook()
  return count, a, b
end

local blank = string.rep("0", 81)
local first = measure(solver.grade, blank)
local second = measure(solver.grade, blank)
print("build " .. (first - second))

for line in io.lines() do
  local hash, grid = line:match("^(%x+) ([0-9.]+)$")
  if not hash or #grid ~= 81 then
    io.stderr:write("bad candidate: " .. line .. "\n")
    os.exit(2)
  end
  local grade_cost, band, rung = measure(solver.grade, grid)
  local answer_cost = measure(solver.answer, grid)
  local hint_cost = measure(solver.hint, grid)
  local count_cost, solutions = measure(counter.count, grid)
  print(string.format("%s %d %d %d %d %d %d %d", hash, band or 0, rung or 0, grade_cost, answer_cost, hint_cost,
    count_cost, solutions))
end
