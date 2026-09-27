-- Counter fixture: ch.store survives leaving, reopening, and a restart. Each tap is
-- a move that apply counts into ch.store; draw shows the saved count, read back
-- with ch.store.get. The runtime writes a changed store to
-- /.games-data/counter/store.bin at most every 5 s, so wait 5 s before leaving
-- (entry 13 adds the flush on leaving).
-- Copy this folder to /.games/counter/ on the SD card (fs_/.games/counter/ in the simulator).
local game = {}

function game.setup(ctx)
  local saved = ch.store.get()
  print("opened with", saved.taps or 0, "saved taps")
  return { taps = saved.taps or 0 }
end

function game.status(state)
  return { turn = 1 }
end

function game.apply(state, seat, move)
  state.taps = state.taps + 1
  ch.store.set({ taps = state.taps })
  return state
end

function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end

function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.rect(20, 20, 400, 100, "black", false)
  ch.gfx.text(40, 50, "Counter", "large", "black")
  ch.gfx.text(40, 150, "Taps: " .. state.taps, "medium", "black")
  ch.gfx.text(40, 190, "Saved taps: " .. (ch.store.get().taps or 0), "small", "black")
  ch.gfx.text(40, 220, "Tap to count; the store is written within 5 s", "small", "black")
end

return game
