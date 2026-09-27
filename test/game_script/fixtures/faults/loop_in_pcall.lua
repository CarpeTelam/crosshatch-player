-- An endless loop in draw that keeps catching the budget error: the fault is
-- sticky, so pcall returns false and the next instruction raises it again.
return {
  setup = function() return {} end,
  draw = function()
    while true do
      pcall(function() while true do end end)
    end
  end,
}
