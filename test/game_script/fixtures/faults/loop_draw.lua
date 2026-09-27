-- An endless loop in draw. status and apply make it a complete game, so the
-- runtime reaches draw (after setup and status) when it runs as one.
return {
  setup = function() return {} end,
  status = function() return { turn = 1 } end,
  apply = function(state) return state end,
  draw = function() while true do end end,
}
