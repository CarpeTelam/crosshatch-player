-- An endless loop in draw.
return {
  setup = function() return {} end,
  draw = function() while true do end end,
}
