-- One layout on every canvas (canvases.lua): the Sticky's 474 x 788 is the X4 Pro's 466 x 788 layout and frames with
-- every x 4 px right, and a canvas under 466 x 788 is unsupported and said so once. It needs a round of its own because
-- it loads neither rules.lua nor taps.lua, the rounds' heaviest modules: the digest of a frame, drawn six times on two
-- canvases, would not fit beside them. The one step it plays opens the MENU and moves nothing, so the round proves
-- nothing about play.
local layout = require("layout")
local canvases = require("canvases")

return {
  mode = "solo",
  settings = { level = "Easy" },
  seed = 41,
  steps = function(state)
    canvases.frames(state, canvases.layout())
    local x, y, w, h = layout.rail_rect(4)
    return { { seat = 1, x = x + w // 2, y = y + h // 2, move = false } }
  end,
  unfinished = true,
}
