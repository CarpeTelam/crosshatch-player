-- The HOW TO PLAY page, with the puzzle credit; a tap closes it. The credit is checked here, word for word, and the
-- page against the canvas: this file's VM has the room that the game's own checks (the bank's) do not.
local taps = require("taps")
local rules = require("rules")
local help = require("help")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 31,
  steps = function(state)
    rules.layout()
    local lines, bottom = help.lines()
    local credit, page = nil, {}
    for i, line in ipairs(lines) do
      page[i] = line.text
      if line.text:find("^Puzzles from") then credit = line.text end
    end
    assert(help.CREDIT == "Puzzles from the Sudoku Exchange puzzle bank (sudokuexchange.com), public domain.")
    assert(table.concat(page, " "):find(help.CREDIT, 1, true), "the credit is on the page, word for word")
    assert(bottom <= ch.screen.h - 40, "the page fits the canvas under the harness's metrics")
    local steps = taps.menu_row({}, 7, { move = false, shows = credit })
    taps.menu(steps, 1, { move = false, shows = "Sudoku - Easy" })
    return steps
  end,
  unfinished = true,
}
