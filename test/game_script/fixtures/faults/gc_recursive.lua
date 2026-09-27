-- A finalizer that recurses through pcall would run with hooks off, outside the
-- stack check, so setmetatable refuses any metatable with __gc.
return {
  setup = function()
    local function f() pcall(f) end
    setmetatable({}, { __gc = function() f() end })
    collectgarbage()
  end,
}
