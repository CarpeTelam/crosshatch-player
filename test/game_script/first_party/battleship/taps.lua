-- taps: rounds read as ship lists and shot lists. This module turns a board cell or a button into the canvas pixel a
-- player taps, through the game's layout module and the canvas size, so a round has no pixel in it.
local layout = require("layout")

local taps = {}

local function canvas() return layout.compute(ch.screen.w, ch.screen.h) end

local function center(rect) return rect.x + rect.w // 2, rect.y + rect.h // 2 end

-- The canvas centre x, y of cell (row, col) of the big board.
function taps.cell(row, col)
  local x, y, w, h = layout.cell_rect(canvas().big, row, col)
  return x + w // 2, y + h // 2
end

-- The centre x, y of a placement button: "rotate", "random", "clear", or "ready".
function taps.button(name) return center(canvas().buttons[name]) end

-- The centre x, y of the question button.
function taps.help() return center(canvas().question_rect) end

-- A canvas point that is no tap target in placement or in firing: clear of the big board, the buttons, and the
-- question button.
function taps.off_target()
  local L = canvas()
  return L.big.x + 200, L.big.y + layout.BOARD * L.big.cell + 32
end

-- One step: `seat` taps the pixel (x, y); `extra` adds its keys (move = false, shows = "...").
function taps.at(seat, x, y, extra)
  local step = { seat = seat, x = x, y = y }
  for key, value in pairs(extra or {}) do step[key] = value end
  return step
end

function taps.shoot(seat, row, col, extra)
  local x, y = taps.cell(row, col)
  return taps.at(seat, x, y, extra)
end

function taps.press(seat, name, extra)
  local x, y = taps.button(name)
  return taps.at(seat, x, y, extra)
end

-- The steps that place ships for `seat`, in the order the game places them: each ship is { row, col, "H" | "V" }, the
-- tapped square being its first. A Rotate tap (it moves nothing) comes before a ship whose direction differs from the
-- seat's current one; `vertical` is that direction at the start (false: across, as a new round has it). Returns the
-- steps and the direction at the end.
function taps.place(seat, ships, vertical)
  local steps = {}
  for _, ship in ipairs(ships) do
    local down = ship[3] == "V"
    if down ~= (vertical == true) then
      steps[#steps + 1] = taps.press(seat, "rotate", { move = false })
      vertical = down
    end
    steps[#steps + 1] = taps.shoot(seat, ship[1], ship[2])
  end
  return steps, vertical == true
end

-- Appends the steps of `more` to `steps`.
function taps.append(steps, more)
  for _, step in ipairs(more) do steps[#steps + 1] = step end
  return steps
end

return taps
