-- load() is removed from the base library.
return { setup = function() return load('return 1')() end }
