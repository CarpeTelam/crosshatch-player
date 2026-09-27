-- An endless loop in input, reached by the first tap on the canvas. status and
-- apply make it a complete game, so the runtime draws it and waits for the tap.
return {
  setup = function() return {} end,
  status = function() return { turn = 1 } end,
  apply = function(state) return state end,
  draw = function() ch.gfx.clear('white') ch.gfx.text(20, 20, 'Tap to loop in input', 'medium', 'black') end,
  input = function() while true do end end,
}
