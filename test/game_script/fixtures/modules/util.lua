-- A helper module; its chunk gets the module name as its argument.
LOADS = (LOADS or 0) + 1
local name = ...
return { loads = LOADS, name = name, add = function(a, b) return a + b end }
