-- trace.lua: the draw commands of one frame, for the checks. The games check's rounds see only the text commands of a
-- frame (RoundPlayer keeps those and no others), so what a check pins about the rest (a fill, an icon, a line) it reads
-- through this recorder, which stands in for `ch.gfx` while a function runs. A check cannot call `game.draw` on the real
-- `ch.gfx` (outside the engine's draw call it is an error), so the recorder is the only way to run it.
--
-- TEST DOUBLE for the engine's `ch.gfx`. It takes its function names from the real table's own keys (a name the engine
-- lacks raises on the recorder too, as an unknown function does on the device) and counts as the engine does
-- (ChBindings.cpp): every call is one command (`clear` included) except `refresh`, which asks for a refresh of the frame
-- and appends no command, so it is neither counted nor in the text; `on_call` still sees it. The check "the recorder
-- counts as the engine does" pins the count against board.draw_grid's own, which says 28, and against a frame with a
-- clear and a refresh in it. It is more permissive than the device in what it does not look at: no argument is checked (a
-- bad color or size passes), nothing is clipped, no frame limit (2,048 commands) or icon and image budget is applied, and
-- an icon or image name is not looked up. The games check's rounds, which draw the real `ch.gfx`, are what find those.
local trace = {}

-- The arguments of one call as text: a string quoted, anything else as `tostring` gives it.
local function text_of(...)
  local parts = {}
  for i = 1, select("#", ...) do
    local v = select(i, ...)
    parts[i] = type(v) == "string" and (string.format("%q", v):gsub("\\\n", "\\n")) or tostring(v)
  end
  return table.concat(parts, ", ")
end

-- Runs f() with the recorder as `ch.gfx` and returns the commands as text, one `name(args)` per line in the order they
-- were drawn, and their number (a `refresh` is no command). `on_call(name, ...)`, when given, sees each call as it is
-- made, `refresh` included. ch.gfx is the real table again afterwards, also when f raises, and the error is raised again
-- as it was.
function trace.record(f, on_call)
  local real, lines, gfx = ch.gfx, {}, {}
  for name, value in pairs(real) do
    if type(value) == "function" then
      gfx[name] = function(...)
        -- The engine's gfxRefresh asks for a refresh and appends nothing to the display list.
        if name ~= "refresh" then lines[#lines + 1] = name .. "(" .. text_of(...) .. ")" end
        if on_call then on_call(name, ...) end
      end
    end
  end
  ch.gfx = gfx
  local ok, err = pcall(f)
  ch.gfx = real
  if not ok then error(err, 0) end
  return table.concat(lines, "\n"), #lines
end

return trace
