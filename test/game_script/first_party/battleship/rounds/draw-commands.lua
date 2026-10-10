-- What the draw puts on the canvas beyond text, read through the recording `ch.gfx` (draws.lua): the recorder itself, secrecy
-- at the level of the commands, the icon and shape kinds, seat 0's Over frame, the ink of every screen, the same frames at
-- both canvases, and a canvas under the box. The pins run in this round's `steps` function, in the round's own VM (they were kept out of
-- checks.lua when that VM had the device's 256 KB and no room to draw frames; the check VMs' limits are larger now,
-- first_party/README.md, "The check VMs' limits"), and an error that names the first thing wrong fails the round.
-- The one step it plays is a Rotate tap that moves nothing, so the round proves nothing about play.
local taps = require("taps")
local draws = require("draws")

return {
  mode = "pass",
  steps = function(state)
    draws.recorder()
    draws.secrecy()
    draws.kinds()
    draws.over()
    draws.ink()
    draws.sameAtBothCanvases()
    draws.smallCanvas()
    return { taps.press(1, "rotate", { move = false }) }
  end,
  unfinished = true,
}
