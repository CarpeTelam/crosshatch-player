-- Gallery fixture: draws every ch.gfx command in every color it takes, text in
-- each size and alignment with a bar exactly ch.text_width wide under it, and the
-- last touch event, marked where it landed. Copy this folder to /.games/gallery/
-- on the SD card (fs_/.games/gallery/ in the simulator).
local game = {}

local FILLS = { "white", "light", "dark", "black" }

function game.setup(ctx)
  return {}
end

function game.status(state)
  return { turn = 1 }
end

function game.apply(state, seat, move)
  return state
end

-- Text at x aligned by align, with a 3 px bar under it as wide as ch.text_width
-- says; drawn text must end exactly where its bar does.
local function measured(x, y, str, size, align, bar_y)
  local w = ch.text_width(str, size)
  local left = x
  if align == "center" then left = x - w // 2 elseif align == "right" then left = x - w end
  ch.gfx.text(x, y, str, size, "black", align)
  ch.gfx.rect(left, bar_y, w, 3, "black", true)
end

function game.draw(state, seat, ui)
  local W, H = ch.screen.w, ch.screen.h
  ch.gfx.clear("white")

  -- Fills: rects and circles in the four colors (white ones outlined to be seen).
  for i, color in ipairs(FILLS) do
    local x = 12 + (i - 1) * 115
    ch.gfx.rect(x, 10, 100, 56, color, true)
    ch.gfx.circle(x + 50, 116, 36, color, true)
    ch.gfx.text(x + 50, 156, color, "small", "black", "center")
  end
  ch.gfx.rect(12, 10, 100, 56, "black", false)
  ch.gfx.circle(62, 116, 36, "black", false)

  -- Ink: outlines and lines in black, and in white on a black band.
  ch.gfx.rect(12, 186, 100, 56, "black", false)
  ch.gfx.circle(177, 214, 26, "black", false)
  ch.gfx.line(222, 186, 322, 242, "black")
  ch.gfx.line(222, 242, 322, 186, "black")
  ch.gfx.rect(337, 186, 125, 56, "black", true)
  ch.gfx.rect(345, 192, 40, 44, "white", false)
  ch.gfx.circle(420, 214, 18, "white", false)
  ch.gfx.line(345, 238, 455, 190, "white")
  ch.gfx.rect(0, 252, W, 30, "black", true)
  ch.gfx.text(W // 2, 256, "white text on black", "medium", "white", "center")

  -- Text: each size left and right on one row, centred on the guide below;
  -- the bars are ch.text_width wide.
  ch.gfx.line(W // 2, 290, W // 2, 540, "black")
  ch.gfx.line(W - 12, 290, W - 12, 540, "black")
  local y = 296
  for _, size in ipairs({ "small", "medium", "large" }) do
    local h = size == "large" and 52 or 32
    measured(12, y, "Left " .. size, size, "left", y + h - 6)
    measured(W - 12, y, "Right", size, "right", y + h - 6)
    measured(W // 2, y + h, "Centre " .. size, size, "center", y + 2 * h - 6)
    y = y + 2 * h + 4
  end

  -- The last touch event, and a cross where it landed.
  local ev = ui.ev
  local line = "Touch the canvas: tap, hold, or swipe"
  if ev then
    line = string.format("%d: %s at %d,%d%s", ui.count, ev.kind, ev.x, ev.y, ev.dir and (" " .. ev.dir) or "")
    ch.gfx.line(ev.x - 12, ev.y, ev.x + 12, ev.y, "black")
    ch.gfx.line(ev.x, ev.y - 12, ev.x, ev.y + 12, "black")
    ch.gfx.circle(ev.x, ev.y, 16, "black", false)
  end
  ch.gfx.text(12, 556, line, "medium", "black")
  ch.gfx.text(12, 596, string.format("canvas %dx%d", W, H), "small", "black")
  -- Far-off coordinates are clipped to the canvas: a line across its width, and
  -- a huge circle wholly to its left that draws nothing.
  ch.gfx.line(-30000, 640, 30000, 640, "black")
  ch.gfx.circle(-20000, 400, 19000, "dark", true)
  ch.gfx.refresh(ev and ev.kind == "long_press" and "half" or "fast")
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" or ev.kind == "long_press" or ev.kind == "swipe" then
    ui.ev = { kind = ev.kind, x = ev.x, y = ev.y, dir = ev.dir }
    ui.count = (ui.count or 0) + 1
    print("event", ev.kind, ev.x, ev.y, ev.dir)
  end
  return nil
end

return game
