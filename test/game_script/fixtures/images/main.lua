-- Images fixture: the game's own images through ch.gfx.image, at their own size
-- and opaque. badge.bmp (100 x 60: a border and a crosshatch) and dot.bmp (37 x 37:
-- a disc) are drawn once in black, as converted, and once in white, which swaps
-- their black and white; each row sits on a light band, which an image covers
-- whole (its white pixels too), unlike an icon. A last badge hangs over the right
-- edge and is clipped there. icon.bmp is the launcher's and not an image. Copy this
-- folder to /.games/images/ on the SD card (fs_/.games/images/ in the simulator).
local game = {}

local NAMES = { "badge", "dot" }

function game.setup(ctx)
  return {}
end

function game.status(state)
  return { turn = 1 }
end

function game.apply(state, seat, move)
  return state
end

-- One row: a light band with a label, then each image in `color`.
local function row(y, color)
  ch.gfx.rect(0, y, ch.screen.w, 100, "light", true)
  ch.gfx.text(12, y + 4, color, "small", "black")
  local x = 12
  for _, name in ipairs(NAMES) do
    ch.gfx.image(name, x, y + 28, color)
    x = x + 120
  end
end

function game.draw(state, seat, ui)
  local W = ch.screen.w
  ch.gfx.clear("white")
  ch.gfx.text(12, 10, "Images", "medium", "black")
  row(60, "black")
  row(180, "white")
  -- Clipped: half of the badge is past the right edge.
  ch.gfx.rect(0, 300, W, 100, "light", true)
  ch.gfx.text(12, 304, "clipped", "small", "black")
  ch.gfx.image("badge", W - 50, 328, "black")
end

function game.input(state, seat, ui, ev)
  return nil
end

return game
