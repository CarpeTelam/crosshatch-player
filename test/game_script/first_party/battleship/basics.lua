-- basics.lua: what checks.lua and draws.lua both need: `eq` and `withCanvas`. It requires nothing.
local basics = {}

function basics.eq(got, want, what)
  if got ~= want then error(what .. ": got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- Runs f() with ch.screen as w x h and puts it back, also when f raises.
function basics.withCanvas(w, h, f)
  local rw, rh = ch.screen.w, ch.screen.h
  ch.screen.w, ch.screen.h = w, h
  local ok, err = pcall(f)
  ch.screen.w, ch.screen.h = rw, rh
  if not ok then error(err, 0) end
end

return basics
