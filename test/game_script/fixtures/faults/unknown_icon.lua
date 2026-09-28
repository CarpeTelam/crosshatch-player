-- An icon name the library does not have, drawn inside pcall: the unknown name
-- stops the game anyway (the guard's fault is sticky), and the frame is not
-- published. status and apply make it a complete game, so the runtime reaches
-- draw.
return {
  setup = function() return {} end,
  status = function() return { turn = 1 } end,
  apply = function(state) return state end,
  draw = function()
    pcall(function() ch.gfx.icon('no_such_icon', 0, 0, 'small', 'black') end)
    ch.gfx.text(0, 0, 'still drawing', 'small', 'black')
  end,
}
