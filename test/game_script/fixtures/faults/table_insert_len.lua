-- table.insert on a table whose __len claims maxinteger - 1 elements: the shift
-- loop would run in C for ever, so the sandbox refuses it.
return {
  setup = function()
    local t = setmetatable({}, { __len = function() return math.maxinteger - 1 end })
    table.insert(t, 1, "x")
  end,
}
