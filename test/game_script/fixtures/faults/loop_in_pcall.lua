-- An endless loop in draw that keeps catching the budget error: the fault is
-- sticky, so pcall returns false and the next instruction raises it again.
-- status and apply make it a complete game, so the runtime reaches draw.
return {
  setup = function() return {} end,
  status = function() return { turn = 1 } end,
  apply = function(state) return state end,
  draw = function()
    while true do
      pcall(function() while true do end end)
    end
  end,
}
