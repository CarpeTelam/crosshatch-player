-- An xpcall whose message handler loops: when the budget error is raised from
-- the hook, Lua runs the handler with hooks off, so the runtime must not call it.
return {
  setup = function()
    xpcall(function() while true do end end, function(e) while true do end end)
  end,
}
