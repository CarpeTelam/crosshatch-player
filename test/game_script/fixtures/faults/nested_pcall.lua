-- C recursion through pcall: each level is a C call, about 784 B of host stack.
-- Lua's own C-stack error is caught by the pcall above it, so without the
-- runtime's headroom check this script would unwind and carry on.
local function f() pcall(f) end f()
return { setup = function() return {} end }
