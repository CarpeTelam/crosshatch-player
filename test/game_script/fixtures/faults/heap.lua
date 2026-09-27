-- Grows the Lua heap until the 256 KiB cap refuses it.
return {
  setup = function()
    local t = {}
    for i = 1, 1e7 do t[i] = string.rep('x', 64) .. i end
  end,
}
