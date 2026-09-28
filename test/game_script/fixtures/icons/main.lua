-- Icons fixture: every library icon through ch.gfx.icon at each size (small
-- 32 px, medium 64 px, large 128 px). The black page draws black icons on white;
-- a tap turns to the white page, white icons on black, and another tap back. The
-- medium row sits on a light band, which shows through around each icon's ink,
-- since an icon draws only its ink pixels. Copy this folder to /.games/icons/ on
-- the SD card (fs_/.games/icons/ in the simulator).
local game = {}

local NAMES = { "die_6", "mark_o", "mark_x", "suit_heart" }

function game.setup(ctx)
  return {}
end

function game.status(state)
  return { turn = 1 }
end

function game.apply(state, seat, move)
  return state
end

function game.draw(state, seat, ui)
  local W = ch.screen.w
  local ink = ui.white and "white" or "black"
  ch.gfx.clear(ui.white and "black" or "white")
  ch.gfx.text(12, 10, ui.white and "Icons: white" or "Icons: black", "medium", ink)
  ch.gfx.text(W - 12, 14, "tap to switch", "small", ink, "right")

  -- Small and medium: one row each, one icon per quarter of the width.
  local cell = W // #NAMES
  for i, name in ipairs(NAMES) do
    ch.gfx.icon(name, (i - 1) * cell + (cell - 32) // 2, 56, "small", ink)
  end
  ch.gfx.rect(0, 104, W, 80, "light", true)
  for i, name in ipairs(NAMES) do
    ch.gfx.icon(name, (i - 1) * cell + (cell - 64) // 2, 112, "medium", ink)
  end

  -- Large: a 2 x 2 grid, each icon named under it.
  local half = W // 2
  for i, name in ipairs(NAMES) do
    local column, row = (i - 1) % 2, (i - 1) // 2
    local x = column * half + (half - 128) // 2
    local y = 204 + row * 176
    ch.gfx.icon(name, x, y, "large", ink)
    ch.gfx.text(column * half + half // 2, y + 134, name, "small", ink, "center")
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then ui.white = not ui.white end
  return nil
end

return game
