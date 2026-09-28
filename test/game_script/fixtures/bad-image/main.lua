-- Bad-image fixture: broken.bmp has the converter's header but claims 8 bits per
-- pixel, so the game never starts: the match shows the load-failure view with
-- "An image is damaged or too large". This script never runs.
return {
  setup = function() return {} end,
  status = function() return { turn = 1 } end,
  apply = function(state) return state end,
  draw = function() ch.gfx.text(0, 0, "should not start", "small", "black") end,
}
