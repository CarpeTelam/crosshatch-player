-- Timing fixture for the device run (entry 14 of epic-install-and-launcher): three frames at the top of what a
-- frame may ask of the replay, one band each. Tap a band to draw its frame; tap anywhere on that frame to go back to
-- the menu, whose cheap frame is the baseline. The README beside this folder says what to record.
--   1. gray.png: the game's own mid-gray 480 x 800 image, dithered to 1 bit by the installer's converter (a worst case
--      for runs: about one fill per pixel pair), drawn once at the canvas's top-left.
--   2. Exactly 1,048,576 icon and image pixels (api-level-1.txt's frame_icon_image_pixels, the whole budget): two gray
--      images, 128 px fill icons, and one-row strips of the image. The count is worked out from ch.screen, so the frame
--      is exact on any canvas, and logged as "band 2 charges N pixels".
--   3. 2,048 filled rects, each the whole canvas, in the four colors in turn (the frame's whole command limit; the
--      dithered ones cost more than black or white). The last one is "dark".
local game = {}

local BUDGET = 1048576 -- frame_icon_image_pixels
local ICON = 128       -- the large icon side
local IMAGE_W, IMAGE_H = 480, 800 -- gray.png
local RECTS = 2048     -- the frame's command limit
local COLORS = { "light", "black", "white", "dark" }
local TOP = 100
local BAND_HEIGHT = 110
local BANDS = {
  { label = "1. One gray image", hint = "gray.png, 480 x 800, dithered" },
  { label = "2. 1,048,576 icon and image px", hint = "the frame's whole pixel budget" },
  { label = "3. 2,048 full-canvas rects", hint = "the frame's whole command limit" },
}

function game.setup(ctx)
  return { view = 0 }
end

function game.status(state)
  return { turn = 1 }
end

function game.apply(state, seat, move)
  state.view = move.view
  return state
end

-- The pixels an icon or image of w x h with its top-left at (x, y) charges: the part on the canvas.
local function visible(x, y, w, h)
  local vw = math.min(x + w, ch.screen.w) - math.max(x, 0)
  local vh = math.min(y + h, ch.screen.h) - math.max(y, 0)
  if vw <= 0 or vh <= 0 then return 0 end
  return vw * vh
end

local function drawMenu()
  ch.gfx.clear("white")
  ch.gfx.text(20, 30, "Frame timing", "large", "black")
  for i, band in ipairs(BANDS) do
    local top = TOP + (i - 1) * BAND_HEIGHT
    ch.gfx.rect(20, top, ch.screen.w - 40, BAND_HEIGHT - 16, "black", false)
    ch.gfx.text(40, top + 16, band.label, "medium", "black")
    ch.gfx.text(40, top + 56, band.hint, "small", "black")
  end
  ch.gfx.text(20, TOP + 3 * BAND_HEIGHT + 10, "Tap a band; tap its frame to return.", "small", "black")
end

local function drawOneImage()
  ch.gfx.image("gray", 0, 0, "black")
end

-- Two whole images, then as many 128 px icons as fit in what is left of the budget, then the last few thousand pixels
-- as one-row strips of the image: an image with its top-left on the canvas's last row shows only its first row.
local function drawBudget()
  ch.gfx.clear("white")
  local total = 0
  for _ = 1, 2 do
    ch.gfx.image("gray", 0, 0, "black")
    total = total + visible(0, 0, IMAGE_W, IMAGE_H)
  end
  local rest = BUDGET - total
  local icons = rest // (ICON * ICON)
  local across = math.max(ch.screen.w // ICON, 1)
  local down = math.max(ch.screen.h // ICON, 1)
  for i = 0, icons - 1 do
    local slot = i % (across * down) -- past the canvas's room, icons overlap: each still counts
    local x, y = (slot % across) * ICON, (slot // across) * ICON
    ch.gfx.icon("circle", x, y, "large", "white", "fill")
    total = total + visible(x, y, ICON, ICON)
  end
  rest = BUDGET - total
  local last = ch.screen.h - 1
  local stripW = visible(0, last, IMAGE_W, IMAGE_H)
  while stripW > 0 and rest >= stripW do
    ch.gfx.image("gray", 0, last, "black")
    total = total + stripW
    rest = rest - stripW
  end
  if rest > 0 then
    local x = ch.screen.w - rest -- a strip rest px wide, at the right edge
    ch.gfx.image("gray", x, last, "black")
    total = total + visible(x, last, IMAGE_W, IMAGE_H)
  end
  ch.log("band 2 charges " .. total .. " pixels")
end

local function drawRects()
  for i = 1, RECTS do
    ch.gfx.rect(0, 0, ch.screen.w, ch.screen.h, COLORS[(i - 1) % #COLORS + 1], true)
  end
end

function game.draw(state, seat, ui)
  if state.view == 1 then
    drawOneImage()
  elseif state.view == 2 then
    drawBudget()
  elseif state.view == 3 then
    drawRects()
  else
    drawMenu()
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind ~= "tap" then return nil end
  if state.view ~= 0 then return { view = 0 } end
  local i = (ev.y - TOP) // BAND_HEIGHT + 1
  if i >= 1 and i <= #BANDS then return { view = i } end
  return nil
end

return game
