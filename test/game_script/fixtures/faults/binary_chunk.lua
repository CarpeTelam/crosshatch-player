-- A binary chunk: string.dump works, but there is no load() to run it.
return { setup = function() return load(string.dump(function() return 1 end))() end }
