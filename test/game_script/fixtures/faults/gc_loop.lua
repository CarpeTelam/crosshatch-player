-- A finalizer running 10 M iterations would run with hooks off, outside the
-- budget, so setmetatable refuses any metatable with __gc.
return {
  setup = function()
    setmetatable({}, { __gc = function() for i = 1, 1e7 do end end })
    collectgarbage()
  end,
}
