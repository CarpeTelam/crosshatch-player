-- table.move over a huge range: its copy loop runs in C, where no hook runs, so
-- the sandbox refuses more than 65,536 elements.
return { setup = function() table.move({}, 1, math.maxinteger, 1) end }
