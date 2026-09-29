-- Packed-images fixture: the packable twin of images/. Its icon.png (96 x 96: rings) is the launcher's row icon,
-- which the installer converts to a 64 x 64 icon.bmp; badge.png (100 x 60) and dot.png (37 x 37) are the game's own
-- images, converted to badge.bmp and dot.bmp at their own size and drawn through ch.gfx.image. The host build packs
-- this folder with scripts/pack_game.py and installs it with the real installer (PackedFixturesTest), which is the
-- one place the packer's live output meets the C++ installer with an icon and an image in it.
local game = {}

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
  ch.gfx.clear("white")
  ch.gfx.text(12, 10, "Packed images", "medium", "black")
  ch.gfx.image("badge", 12, 60, "black")
  ch.gfx.image("dot", 140, 60, "black")
end

function game.input(state, seat, ui, ev)
  return nil
end

return game
