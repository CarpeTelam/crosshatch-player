-- require: util loads once and is cached; a module returning nothing gives true.
local util = require("util")
local again = require("util")
local quiet = require("quiet")

return {
  setup = function()
    return { same = util == again, loads = util.loads, name = util.name, quiet = quiet, sum = util.add(2, 3) }
  end,
  draw = function(s)
    local line = table.concat({ tostring(s.same), s.loads, s.name, tostring(s.quiet), s.sum }, " ")
    ch.gfx.text(0, 0, line, "small", "black")
  end,
}
