-- 199 recursive pattern items: the matcher recurses in C with no hook; MAXCCALLS
-- (16) stops it with "pattern too complex".
return {
  setup = function()
    return string.find(string.rep("a", 199), string.rep("a?", 199))
  end,
}
