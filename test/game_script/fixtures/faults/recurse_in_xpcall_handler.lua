-- An xpcall whose message handler recurses through pcall after a budget fault.
return {
  setup = function()
    local function f() pcall(f) end
    xpcall(function() while true do end end, function(e) f() return e end)
  end,
}
