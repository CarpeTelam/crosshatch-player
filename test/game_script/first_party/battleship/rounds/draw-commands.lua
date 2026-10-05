-- What the draw puts on the canvas beyond text, read through the recording `ch.gfx` (draws.lua): the recorder itself, secrecy
-- at the level of the commands, the icon and shape kinds, and seat 0's Over frame. The pins run in this round's `steps`
-- function, in the round's own VM (the checks VM has no room to draw frames: first_party/README.md, "The checks VM
-- heap"), and an error that names the first thing wrong fails the round. The one step it plays is a Rotate tap that moves
-- nothing, so the round proves nothing about play.
local taps = require("taps")
local draws = require("draws")

return {
  mode = "pass",
  steps = function(state)
    draws.recorder()
    draws.secrecy()
    draws.kinds()
    draws.over()
    return { taps.press(1, "rotate", { move = false }) }
  end,
  unfinished = true,
}
