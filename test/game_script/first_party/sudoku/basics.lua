-- basics.lua: what every module of this folder that checks or observes shares and nothing else: `eq` and the draw
-- recorder `record`. It requires nothing and has no side effect when it loads, so canvases.lua and checks.lua, which
-- keep clear of the pin modules (first_party/README.md, "The split of Sudoku's checks and pins"), can load it and pins.lua
-- can re-export `eq` from it. A module requires `basics` before it requires `pins`.

local basics = {}

function basics.eq(got, want, what)
  if got ~= want then error((what or "") .. " got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- The commands of a frame, counted. TEST DOUBLE for the engine's ch.gfx (which faults outside a draw call, keeps a
-- frame to 2,048 commands, and checks an image's name against the package): it counts as the engine does (ChBindings.cpp):
-- every call is one command (`clear` included) except `refresh`, which asks for a refresh of the frame and appends no
-- command, so it is not counted (board.draw_grid says 28 commands, and the first check in drawn.lua pins that this double
-- counts it so; marks.lua pins a frame with a clear and a refresh in it), and hands each image call to `on_image` and
-- every call, image and refresh included, to `on_call(name, ...)` instead of keeping the calls (a round's VM has no
-- room for a frame's worth of tables). It takes its function names from the real ch.gfx's
-- own keys, so a name the engine lacks (a misspelled call in a draw) raises here too. It is more permissive than the
-- device: it clips nothing, takes any arguments, and does not look an image up, so a name that is not in the package
-- passes it (the games check's rounds, drawing the real ch.gfx, fault on one). A failed draw leaves no recorder
-- installed: ch.gfx is restored and the error raised again.
function basics.record(f, on_image, on_call)
  local count = 0
  local gfx = {}
  for name, value in pairs(ch.gfx) do
    if type(value) == "function" then
      gfx[name] = function(...)
        if name ~= "refresh" then count = count + 1 end
        if name == "image" and on_image then on_image(...) end
        if on_call then on_call(name, ...) end
      end
    end
  end
  local real = ch.gfx
  ch.gfx = gfx
  local ok, err = pcall(f)
  ch.gfx = real
  if not ok then error(err, 0) end
  return count
end

return basics
