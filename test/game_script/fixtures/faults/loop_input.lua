-- An endless loop in input.
return {
  setup = function() return {} end,
  draw = function() ch.gfx.clear('white') end,
  input = function() while true do end end,
}
