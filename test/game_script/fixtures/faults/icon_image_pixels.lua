-- 65 large icons at the canvas's top-left, drawn inside pcall: their 65 x 16,384
-- pixels pass frame_icon_image_pixels (1,048,576) at the 65th, which stops the
-- game anyway (the guard's fault is sticky), and the frame is not published.
-- status and apply make it a complete game, so the runtime reaches draw.
return {
  setup = function() return {} end,
  status = function() return { turn = 1 } end,
  apply = function(state) return state end,
  draw = function()
    pcall(function()
      for i = 1, 65 do ch.gfx.icon('circle', 0, 0, 'large', 'black', 'fill') end
    end)
    ch.gfx.text(0, 0, 'still drawing', 'small', 'black')
  end,
}
