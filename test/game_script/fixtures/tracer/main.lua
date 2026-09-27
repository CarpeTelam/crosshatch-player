-- Tracer fixture: draws a frame with clear, rect, and text, and counts taps in `ui`.
-- Copy this folder to /.games/tracer/ on the SD card (fs_/.games/tracer/ in the simulator).
local game = {}

function game.setup(ctx)
  return { seats = ctx.seats, mode = ctx.mode }
end

function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.rect(20, 20, 400, 100, "black", false)
  ch.gfx.text(40, 50, "Tracer", "large", "black")
  ch.gfx.text(40, 150, "Taps: " .. (ui.taps or 0), "medium", "black")
  ch.gfx.text(40, 190, "Tap anywhere", "small", "black")
  if ui.x then
    ch.gfx.rect(ui.x - 20, ui.y - 20, 40, 40, "black", true)
  end
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ui.taps = (ui.taps or 0) + 1
    ui.x, ui.y = ev.x, ev.y
  end
  return nil
end

return game
