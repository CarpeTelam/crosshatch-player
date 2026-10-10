-- help.lua: the words of the HOW TO PLAY page and how they wrap to the page's width. Pure: it needs ch.text_width only.
local layout = require("layout")

local help = {}

local CREDIT = "Puzzles from the Sudoku Exchange puzzle bank (sudokuexchange.com), public domain."
local HELP = {
  "Fill the grid so every row, column, and 3 x 3 box holds each digit from 1 to 9 once.",
  "Tap a cell, then a digit on the pad. Tap the digit a cell holds to clear it. With no cell selected, a digit key "
    .. "lights every copy of that digit.",
  "NOTES pencils small marks instead; a mark a neighbour rules out is hidden. ERASE clears the selected cell. UNDO "
    .. "takes back one cell, or one whole FILL NOTES.",
  "In MENU, HINT picks a cell and names the rule, never the digit; a wrong digit comes first. CHECK strikes wrong "
    .. "digits. After a hint the solve sets no best time.",
  CREDIT,
  "Tap anywhere to close this page.",
}
help.CREDIT = CREDIT

-- The lines of the page as { y, text }, wrapped with ch.text_width to the page's width, and the y below the last one.
function help.lines()
  local x, y, width = layout.help_area()
  local out = {}
  for _, paragraph in ipairs(HELP) do
    local line = ""
    for word in paragraph:gmatch("%S+") do
      local try = line == "" and word or line .. " " .. word
      if line ~= "" and ch.text_width(try, "small") > width then
        out[#out + 1] = { y = y, text = line }
        y, line = y + 28, word
      else
        line = try
      end
    end
    out[#out + 1] = { y = y, text = line }
    y = y + 28 + 12
  end
  return out, y
end

return help
