-- C recursion through an __index function, about 254 B of host stack a level.
return {
  setup = function()
    local t = setmetatable({}, { __index = function(t, k) return t[k] end })
    return t.x
  end,
}
