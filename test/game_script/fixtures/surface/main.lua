-- Reports the surface a game sees, one fact per print line, for ApiSurfaceTest
-- (test/game_script), which compares the facts with docs/crosshatch/api-level-1.txt.
-- It defines no global, so _G holds only what the host put there.
--
--   G <global> <type>          every global
--   M <table>.<member> <type>  every member of a global table other than _G and ch
--   C <path> <type>            every non-table value under ch, walked recursively
--   MT <path>                  a global table or a table under ch with a metatable
--   S <key> <value>            each key of the string metatable
--   X <field> <type>           each field of setup's ctx; then "mode <ctx.mode>"
--   E <kind>[ <field>...]      each event input() gets, its other fields sorted;
--                              then "D <dir>" when it has a dir
-- A <type> is math.type's for numbers ("integer", "float"), type's otherwise.

local ipairs, pairs, print, tostring, type = ipairs, pairs, print, tostring, type
local getmetatable, mathType, sort, concat = getmetatable, math.type, table.sort, table.concat

local function typeOf(value)
  return mathType(value) or type(value)
end

local function sortedKeys(t)
  local keys = {}
  for key in pairs(t) do
    keys[#keys + 1] = tostring(key)
  end
  sort(keys)
  return keys
end

for _, name in ipairs(sortedKeys(_G)) do
  local value = _G[name]
  print("G " .. name .. " " .. typeOf(value))
  if type(value) == "table" then
    if getmetatable(value) ~= nil then print("MT " .. name) end
    if value ~= _G and name ~= "ch" then
      for _, member in ipairs(sortedKeys(value)) do
        print("M " .. name .. "." .. member .. " " .. typeOf(value[member]))
      end
    end
  end
end

local function walk(path, t)
  if getmetatable(t) ~= nil then print("MT " .. path) end
  for _, key in ipairs(sortedKeys(t)) do
    local value, full = t[key], path .. "." .. key
    if type(value) == "table" then
      walk(full, value)
    else
      print("C " .. full .. " " .. typeOf(value))
    end
  end
end
walk("ch", ch)

local stringMeta = getmetatable("")
for _, key in ipairs(sortedKeys(stringMeta)) do
  local value = stringMeta[key]
  print("S " .. key .. " " .. (value == string and "string" or typeOf(value)))
end

return {
  setup = function(ctx)
    for _, key in ipairs(sortedKeys(ctx)) do
      print("X " .. key .. " " .. typeOf(ctx[key]))
    end
    print("mode " .. tostring(ctx.mode))
    return {}
  end,
  draw = function() end,
  input = function(_, _, _, event)
    local fields = {}
    for _, key in ipairs(sortedKeys(event)) do
      if key ~= "kind" then fields[#fields + 1] = key end
    end
    print("E " .. tostring(event.kind) .. (#fields > 0 and " " .. concat(fields, " ") or ""))
    if event.dir ~= nil then print("D " .. tostring(event.dir)) end
  end,
}
