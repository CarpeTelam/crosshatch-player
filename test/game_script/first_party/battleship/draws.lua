-- draws.lua: what Battleship's draw puts on the canvas beyond text, read through trace.lua's recording `ch.gfx`: the recorder
-- itself, secrecy at the level of the commands, the icon and shape kinds, and seat 0's Over frame. Every pin is called from
-- one round, rounds/draw-commands.lua, in its `steps(state)`; an error that names the first thing wrong fails that round.
-- They live here, in the round's own VM, and not in checks.lua: that VM holds the game's modules and the 55 KB of checks
-- (about 190 KB of the sandbox's 256 KB, once compiled), and a frame's draw leaves about 47 KB of garbage, so a check that
-- draws frames faulted "not enough memory" there. Sudoku's pin modules are the same arrangement.
local fleet = require("fleet")
local layout = require("layout")
local game = require("main")
local trace = require("trace")

local draws = {}

-- The fewest commands a frame compared may hold, a little under what each holds (measured: 76 in a firing frame, 32 to 53
-- while placing or waiting), so a comparison of empty or nearly empty frames cannot pass.
local MIN_FIRING, MIN_PLACING = 70, 30
local SMALL_LINE = 24 -- the height of a line of small text: the game's own step for a wrapped message
local ACROSS = { { 1, 1, "H" }, { 2, 1, "H" }, { 3, 1, "H" }, { 4, 1, "H" }, { 5, 1, "H" } }
local DOWN = { { 1, 10, "V" }, { 1, 9, "V" }, { 1, 8, "V" }, { 1, 7, "V" }, { 1, 6, "V" } }

local function eq(got, want, what)
  if got ~= want then error(what .. ": got " .. tostring(got) .. ", wanted " .. tostring(want), 2) end
end

-- A fleet from { row, col, "H" | "V" } ships placed in the game's order, through fleet.place.
local function build(ships, from)
  local f = from or fleet.EMPTY
  for i, ship in ipairs(ships) do
    local got, reason = fleet.place(f, ship[1], ship[2], ship[3] == "V")
    if not got then error("ship " .. i .. " was not placed: " .. tostring(reason), 2) end
    f = got
  end
  return f
end

-- A new round's state with the given fields replaced.
local function stateWith(fields)
  local s = game.setup({ seats = 2, mode = "pass", api = 1, settings = {} })
  for key, value in pairs(fields) do s[key] = value end
  return s
end

-- A state in the firing phase with the given fleets and the shooter `t`.
local function firing(f1, f2, t) return stateWith({ f = { f1, f2 }, p = 3, t = t or 1 }) end

-- A fleet after the shots { row, col } ..., through fleet.shot.
local function shotAt(f, ...)
  for _, at in ipairs({ ... }) do
    local got, result = fleet.shot(f, at[1], at[2])
    assert(got, "shot " .. at[1] .. "," .. at[2] .. " was refused: " .. tostring(result))
    f = got
  end
  return f
end

-- The same fleet with its intact ships (lower-case letters) taken out: what a shot-only view of it holds.
local function withoutIntact(f) return (f:gsub("[a-e]", ".")) end

-- The commands of game.draw(state, seat, {}) as text, one per line, and, when `keep`, as a list of
-- { name = "rect", <arguments in order> }.
local function frame(state, seat, keep)
  collectgarbage("collect")
  local events = keep and {} or nil
  local text = trace.record(function() game.draw(state, seat, {}) end, keep and function(name, ...)
    events[#events + 1] = { name = name, ... }
  end or nil)
  collectgarbage("collect")
  return text, events
end

-- Two frames' commands must be the same: the first command that differs is named. Returns the number of commands.
local function sameFrames(a, b, what)
  local count, next_a, next_b = 0, a:gmatch("[^\n]+"), b:gmatch("[^\n]+")
  while true do
    local x, y = next_a(), next_b()
    if x ~= y then error(what .. ": command " .. count + 1 .. " differs: " .. tostring(x) .. " against " .. tostring(y), 2) end
    if x == nil then return count end
    count = count + 1
  end
end

-- The frames of seat `seat` for each state of `states` are the same as the first's: what the states differ in is not drawn
-- for that seat. Returns the least number of commands in any frame compared, and fails when it is under `min`, so a
-- comparison of empty frames cannot pass.
local function sameAcross(seat, states, what, min)
  local base = frame(states[1], seat)
  local least = select(2, base:gsub("\n", "\n")) + 1
  for i = 2, #states do
    local other = frame(states[i], seat)
    local count = sameFrames(base, other, what .. " (state " .. i .. ")")
    least = math.min(least, count)
  end
  assert(least >= min, what .. ": the frames hold " .. least .. " commands, under " .. min)
  return least
end

-- The comparison is red, with the first differing command named, when the states differ in what a frame may show: the
-- positive control of each sameAcross below.
local function differs(seat, states, what)
  local ok, err = pcall(sameAcross, seat, states, what, 0)
  assert(not ok and tostring(err):find("differs", 1, true), what .. ": expected frames that differ, got " .. tostring(err))
end

-- Whether point x, y is on board B, its edge lines included.
local function onBoard(B, x, y)
  local side = 10 * B.cell
  return x >= B.x and x <= B.x + side and y >= B.y and y <= B.y + side
end

-- The recorder: a function making a known number of calls gives that count and one text line per call, as the engine
-- counts them (ChBindings.cpp: a `clear` is one command, a `refresh` none, which on_call still sees), a name the engine
-- lacks (a misspelled ch.gfx function) raises, and ch.gfx is the real table again after an error inside the function.
function draws.recorder()
  local text, count = trace.record(function()
    ch.gfx.clear("white")
    ch.gfx.rect(1, 2, 3, 4, "black", true)
    ch.gfx.text(5, 6, "a, b", "small", "black")
  end)
  eq(count, 3, "commands recorded")
  eq(select(2, text:gsub("\n", "\n")), 2, "text lines break")
  eq(text, 'clear("white")\nrect(1, 2, 3, 4, "black", true)\ntext(5, 6, "a, b", "small", "black")', "the recorded text")
  eq(select(2, trace.record(function() end)), 0, "an empty function")
  -- A frame with a clear and a refresh in it: the refresh asks for a refresh and appends no command to the frame.
  local calls = {}
  local framed, framed_count = trace.record(function()
    ch.gfx.clear("white")
    ch.gfx.refresh("full")
    ch.gfx.rect(1, 2, 3, 4, "black", true)
  end, function(name) calls[#calls + 1] = name end)
  eq(framed_count, 2, "commands of a frame with a clear and a refresh")
  eq(framed, 'clear("white")\nrect(1, 2, 3, 4, "black", true)', "the text of a frame with a refresh")
  eq(table.concat(calls, ","), "clear,refresh,rect", "on_call sees a refresh")
  -- A newline inside a text argument stays inside its command's line (%q would break the line there).
  local broken, broken_count = trace.record(function() ch.gfx.text(1, 2, "a\nb", "small", "black") end)
  eq(broken_count, 1, "a text with a newline")
  eq(select(2, broken:gsub("\n", "\n")), 0, "a newline in a text argument breaks the line")
  local real = ch.gfx
  local seen = {}
  trace.record(function() ch.gfx.line(1, 2, 3, 4, "dark") end, function(name, ...) seen = { name, ... } end)
  eq(table.concat(seen, ","), "line,1,2,3,4,dark", "on_call")
  local ok, err = pcall(trace.record, function() ch.gfx.sparkle(1, 2) end)
  eq(ok, false, "a misspelled function")
  assert(tostring(err):find("sparkle", 1, true), "the error names the function: " .. tostring(err))
  eq(ch.gfx, real, "ch.gfx after a raised error")
  ok, err = pcall(trace.record, function() ch.gfx.clear("white") error("boom", 0) end)
  eq(ok, false, "an error in the function")
  eq(err, "boom", "the error is raised again as it was")
  eq(ch.gfx, real, "ch.gfx after an error in the function")
end

-- Draw-level secrecy. Each seat's frame is a function of its own fleet and what has been fired at the other, never of
-- where the other seat's intact ships lie: two states that differ only there draw the same commands for that seat, in
-- the firing frames, while the other seat is placing, and for the seat that waits; and each comparison has a positive
-- control (a difference a frame may show is found).
function draws.secrecy()
  local own = shotAt(build(ACROSS), { 6, 6 }, { 1, 1 })
  -- Two fleets with the same shots (a miss, and a hit on the Carrier's first cell), whose intact ships lie differently.
  local x = shotAt(build(DOWN), { 10, 10 }, { 1, 10 })
  local y = shotAt(build({ { 1, 10, "V" }, { 1, 1, "H" }, { 2, 1, "H" }, { 3, 1, "H" }, { 4, 1, "H" } }), { 10, 10 }, { 1, 10 })
  assert(x ~= y and withoutIntact(x) == withoutIntact(y), "the two fleets must differ in their intact ships only")
  for seat = 1, 2 do
    for shooter = 1, 2 do
      local function state(other)
        local f = seat == 1 and { own, other } or { other, own }
        return firing(f[1], f[2], shooter)
      end
      sameAcross(seat, { state(x), state(y) },
        "seat " .. seat .. " firing, shooter " .. shooter .. ", the other fleet's intact ships", MIN_FIRING)
    end
  end
  -- A positive control: the same comparison is red when the states differ in what a frame may show (one more miss).
  local more = shotAt(x, { 10, 9 })
  differs(1, { firing(own, x, 1), firing(own, more, 1) }, "firing, one more shot")
  -- Placement: seat 1 waiting while seat 2 places, seat 2 placing, and seat 2 waiting while seat 1 places, each with a
  -- positive control (the seat's own fleet, which its frame does show, differs).
  local waiting1 = function(own1, other)
    return stateWith({ f = { own1, other }, p = 2 })
  end
  sameAcross(1, { waiting1(build(ACROSS), fleet.EMPTY), waiting1(build(ACROSS), build({ ACROSS[1] })),
                  waiting1(build(ACROSS), build({ DOWN[1], DOWN[2] })) }, "seat 1 waiting, seat 2's ships", MIN_PLACING)
  differs(1, { waiting1(build(ACROSS), fleet.EMPTY), waiting1(build(DOWN), fleet.EMPTY) }, "seat 1 waiting, its own ships")
  local placing2 = function(other, own2)
    return stateWith({ f = { other, own2 }, p = 2 })
  end
  sameAcross(2, { placing2(build(ACROSS), build({ ACROSS[1] })), placing2(build(DOWN), build({ ACROSS[1] })),
                  placing2(fleet.EMPTY, build({ ACROSS[1] })) }, "seat 2 placing, seat 1's ships", MIN_PLACING)
  differs(2, { placing2(build(ACROSS), build({ ACROSS[1] })), placing2(build(ACROSS), build({ DOWN[1] })) },
    "seat 2 placing, its own ships")
  local waiting2 = function(other, own2)
    return stateWith({ f = { other, own2 }, p = 1 })
  end
  sameAcross(2, { waiting2(fleet.EMPTY, fleet.EMPTY), waiting2(build({ ACROSS[1], ACROSS[2] }), fleet.EMPTY),
                  waiting2(build({ DOWN[1] }), fleet.EMPTY) }, "seat 2 waiting, seat 1's ships", MIN_PLACING)
  differs(2, { waiting2(fleet.EMPTY, fleet.EMPTY), waiting2(fleet.EMPTY, build({ ACROSS[1] })) },
    "seat 2 waiting, its own ships")
  local placing1 = function(own1, other)
    return stateWith({ f = { own1, other }, p = 1 })
  end
  sameAcross(1, { placing1(build({ ACROSS[1] }), fleet.EMPTY), placing1(build({ ACROSS[1] }), build(DOWN)) },
    "seat 1 placing, seat 2's fleet", MIN_PLACING)
  differs(1, { placing1(build({ ACROSS[1] }), fleet.EMPTY), placing1(build({ DOWN[1] }), fleet.EMPTY) },
    "seat 1 placing, its own ships")
  -- The phase is drawn too: the same seat waiting and placing differ.
  differs(1, { stateWith({ f = { fleet.EMPTY, fleet.EMPTY }, p = 1 }), stateWith({ f = { fleet.EMPTY, fleet.EMPTY }, p = 2 }) },
    "seat 1 placing against waiting")
end

-- Icon and shape kinds. The target board (the other fleet as the mask gives it) holds grid lines, outlines, and the icons
-- of shots only: a wave for a miss, a flame for a hit, a solid flame for a sunk ship, each on a cell that was shot; no
-- filled shape, circle, or text a ship could be drawn with. The own fleet draws no icon. The only other icons of a
-- firing frame are the question mark and the boat beside the counts.
function draws.kinds()
  local own = shotAt(build(ACROSS), { 6, 6 }, { 1, 1 })
  local other = shotAt(build(DOWN), { 10, 10 }, { 1, 10 }, { 1, 6 }, { 2, 6 })
  local s = firing(own, other, 1)
  s.l = { 1, 2, 6, 2, 5 }
  local L = layout.compute(ch.screen.w, ch.screen.h)
  local _, events = frame(s, 1, true)
  local want = {}
  for _, shot in ipairs({ { 10, 10, "waves" }, { 1, 10, "fire" }, { 1, 6, "fire", "fill" }, { 2, 6, "fire", "fill" } }) do
    local x, y = layout.cell_rect(L.big, shot[1], shot[2])
    want[(x + 6) .. "," .. (y + 6)] = shot[3] .. (shot[4] or "")
  end
  local questions, boats = 0, 0
  for _, e in ipairs(events) do
    if e.name == "icon" then
      local at = e[2] .. "," .. e[3]
      assert(not onBoard(L.small, e[2], e[3]), "an icon on the own fleet: " .. e[1])
      if e[1] == "question" then
        questions = questions + 1
      elseif e[1] == "boat" then
        boats = boats + 1
      else
        eq(want[at], e[1] .. (e[6] or ""), "the icon at " .. at)
        want[at] = nil
      end
    elseif e.name == "text" then
      assert(not onBoard(L.big, e[1], e[2]), "text on the target board: " .. tostring(e[3]))
    elseif e.name ~= "clear" then
      -- What falls on the target board is a line or an unfilled rect (the grid and a last-shot outline), nothing else.
      local px, py = e[1], e[2]
      if e.name == "line" then px, py = math.min(e[1], e[3]), math.min(e[2], e[4]) end
      if onBoard(L.big, px, py) then
        assert(e.name == "line" or (e.name == "rect" and not e[6]),
          "a " .. e.name .. " on the target board at " .. px .. "," .. py .. " (a filled shape or a circle could be a ship)")
      end
    end
  end
  eq(next(want), nil, "an icon for every shot at the target board")
  eq(questions, 1, "question marks")
  eq(boats, 1, "boats")
  -- A shot-free target board has no icon at all, however the other fleet lies.
  local _, quiet = frame(firing(own, build(DOWN), 1), 1, true)
  for _, e in ipairs(quiet) do
    if e.name == "icon" then assert(e[1] == "question" or e[1] == "boat", "an icon on a target with no shot: " .. e[1]) end
  end
  -- The own fleet, placing or waiting, draws no icon on its board either.
  for p = 1, 2 do
    local _, mine = frame(stateWith({ f = { build(ACROSS), fleet.EMPTY }, p = p }), 1, true)
    for _, e in ipairs(mine) do
      if e.name == "icon" then assert(not onBoard(L.big, e[2], e[3]), "an icon on the own fleet: " .. e[1]) end
    end
  end
end

-- Seat 0 at Over: every ship cell of both fleets is drawn (a filled black bar over the cell's centre), no water or miss
-- cell is covered by one, every miss is a ring at its cell's centre, and each label's text ends 4 px or more above the
-- first line of its board, whichever seat won.
function draws.over()
  local L = layout.compute(ch.screen.w, ch.screen.h)
  local sunk = (build(DOWN):gsub("%l", string.upper))
  local alive = shotAt(build(ACROSS), { 8, 3 }, { 9, 9 }, { 3, 1 })
  for winner = 1, 2 do
    local state = winner == 1 and firing(alive, sunk, 1) or firing(sunk, alive, 2)
    eq(game.status(state).over, true, "the round is over")
    local _, events = frame(state, 0, true)
    for i = 1, 2 do
      local B, f, what = L.over[i], state.f[i], "winner " .. winner .. ", fleet " .. i
      local bars, rings, labels, top = {}, {}, nil, nil
      for _, e in ipairs(events) do
        if e.name == "rect" and e[6] == true and e[5] == "black" then
          for row = 1, 10 do
            for col = 1, 10 do
              local x, y, w, h = layout.cell_rect(B, row, col)
              local cx, cy = x + w // 2, y + h // 2
              if cx >= e[1] and cx < e[1] + e[3] and cy >= e[2] and cy < e[2] + e[4] then bars[(row - 1) * 10 + col] = true end
            end
          end
        elseif e.name == "circle" then
          rings[e[1] .. "," .. e[2]] = true
        elseif e.name == "text" and e[3] == "Player " .. i then
          labels = e
        elseif e.name == "line" and e[1] >= B.x and e[1] <= B.x + 10 * B.cell and e[2] >= B.y - 1 then
          top = math.min(top or e[2], e[2], e[4])
        end
      end
      local ships = 0
      for row = 1, 10 do
        for col = 1, 10 do
          local at = (row - 1) * 10 + col
          local char = f:sub(at, at)
          local x, y, w, h = layout.cell_rect(B, row, col)
          local ship = char:find("[a-eA-E]") ~= nil
          if (bars[at] == true) ~= ship then error(what .. ": a ship bar at " .. row .. "," .. col .. ": " .. tostring(bars[at]) .. ", wanted " .. tostring(ship)) end
          if (rings[(x + w // 2) .. "," .. (y + h // 2)] == true) ~= (char == "o") then
            error(what .. ": a ring at " .. row .. "," .. col .. " is wrong for the cell '" .. char .. "'")
          end
          if bars[at] then ships = ships + 1 end
        end
      end
      eq(ships, 17, what .. ": ship cells drawn")
      eq(top, B.y, what .. ": the board's first line")
      assert(labels and labels[1] == B.x and labels[4] == "small", what .. ": the label of board " .. i)
      assert(labels[2] + SMALL_LINE + 4 <= top, what .. ": the label must end at least 4 px above its board")
    end
  end
end

-- Runs f() with ch.screen as w x h and puts it back, also when f raises.
local function withCanvas(w, h, f)
  local rw, rh = ch.screen.w, ch.screen.h
  ch.screen.w, ch.screen.h = w, h
  local ok, err = pcall(f)
  ch.screen.w, ch.screen.h = rw, rh
  if not ok then error(err, 0) end
end

-- The lines f() sends to ch.log.
local function logged(f)
  local real, lines = ch.log, {}
  ch.log = function(...) lines[#lines + 1] = table.concat({ ... }, " ") end
  local ok, err = pcall(f)
  ch.log = real
  if not ok then error(err, 0) end
  return lines
end

-- The commands that carry an x, and which of their arguments are one.
local X_ARGS = { line = { 1, 3 }, rect = { 1 }, circle = { 1 }, text = { 1 }, icon = { 2 }, image = { 2 } }

-- The commands of `a` against `b`: the same, `b` with every x `dx` further right.
local function sameShifted(a, b, dx, what)
  eq(#b, #a, what .. ": commands")
  for i, e in ipairs(a) do
    local o = b[i]
    eq(o.name, e.name, what .. ": command " .. i)
    assert(X_ARGS[e.name] or e.name == "clear" or e.name == "refresh", what .. ": a command with unknown coordinates: " .. e.name)
    local x = {}
    for _, k in ipairs(X_ARGS[e.name] or {}) do x[k] = dx end
    local n = 0
    for k in pairs(e) do
      if math.type(k) == "integer" and k > n then n = k end
    end
    for k = 1, n do
      local want = e[k]
      if x[k] then want = want + x[k] end
      eq(o[k], want, what .. ": command " .. i .. " (" .. e.name .. ") argument " .. k)
    end
  end
end

-- The frame of game.draw(state, seat, ui) as the list of its commands, with ch.screen as w x h.
local function eventsAt(w, h, state, seat, ui)
  local events
  withCanvas(w, h, function()
    collectgarbage("collect")
    events = {}
    trace.record(function() game.draw(state, seat, ui) end, function(name, ...) events[#events + 1] = { name = name, ... } end)
  end)
  return events
end

-- One layout at every canvas: the Sticky's 474 x 788 frame of each screen is the X4 Pro's 466 x 788 frame with every x 4
-- px further right, nothing more and nothing different.
function draws.sameAtBothCanvases()
  local own, other = build(ACROSS), build(DOWN)
  local sunk = (other:gsub("%l", string.upper))
  local screens = {
    { "placing", stateWith({ f = { build({ ACROSS[1], ACROSS[2] }), fleet.EMPTY }, p = 1 }), 1, {} },
    { "placing with a message", stateWith({}), 1, { message = "Ships cannot overlap" } },
    { "waiting", stateWith({ f = { own, fleet.EMPTY }, p = 2 }), 1, {} },
    { "firing", (function() local s = firing(shotAt(own, { 6, 6 }), shotAt(other, { 10, 10 }, { 1, 10 }), 1)
      s.l = { 1, 10, 10, 0, 0 } return s end)(), 1, {} },
    { "help", stateWith({}), 1, { help = true } },
    { "over", firing(shotAt(own, { 8, 3 }), sunk, 1), 0, {} },
  }
  for _, screen in ipairs(screens) do
    local a = eventsAt(466, 788, screen[2], screen[3], screen[4])
    local b = eventsAt(474, 788, screen[2], screen[3], screen[4])
    assert(#a >= 5, screen[1] .. ": a frame of " .. #a .. " commands")
    sameShifted(a, b, 4, screen[1])
  end
end

-- The ink of a frame, for every screen the game has: its first command is clear("white") and no other is a clear; every
-- line is black, except the white cross a hit draws inside its black bar (both ends inside one filled black rect); every
-- text, icon, circle and rect is black, except the inverted Ready button (a filled black rect, its "check" icon and its
-- "Ready" label white, once the fleet is placed). A game that cleared its page black or drew its grid white drew the right
-- commands in the wrong ink, which the commands alone do not show.
function draws.ink()
  local own, other = build(ACROSS), build(DOWN)
  local sunk = (other:gsub("%l", string.upper))
  local hit = shotAt(own, { 1, 1 })
  local screens = {
    { "placing", stateWith({ f = { build({ ACROSS[1], ACROSS[2] }), fleet.EMPTY }, p = 1 }), 1, {} },
    { "placing, fleet ready", stateWith({ f = { own, fleet.EMPTY }, p = 1 }), 1, {} },
    { "placing with a message", stateWith({}), 1, { message = "Ships cannot overlap" } },
    { "waiting", stateWith({ f = { own, fleet.EMPTY }, p = 2 }), 1, {} },
    { "firing", (function() local s = firing(hit, shotAt(other, { 10, 10 }, { 1, 10 }), 1)
      s.l = { 1, 10, 10, 0, 0 } return s end)(), 1, {} },
    { "help", stateWith({}), 1, { help = true } },
    { "over", firing(shotAt(own, { 8, 3 }, { 1, 1 }), sunk, 1), 0, {} },
  }
  local inverted_seen, hits_seen = false, false
  for _, screen in ipairs(screens) do
    local what = screen[1]
    local events = eventsAt(ch.screen.w, ch.screen.h, screen[2], screen[3], screen[4])
    assert(#events >= 5, what .. ": a frame of " .. #events .. " commands")
    eq(events[1].name, "clear", what .. ": the first command")
    eq(events[1][1], "white", what .. ": the page is cleared to")
    -- The filled black rects so far: what a white line or a white label may lie in.
    local black = {}
    local function inside(px, py)
      for _, r in ipairs(black) do
        if px >= r[1] and px <= r[1] + r[3] and py >= r[2] and py <= r[2] + r[4] then return true end
      end
      return false
    end
    for k, e in ipairs(events) do
      local at = what .. ": command " .. k .. " (" .. e.name .. ")"
      if k > 1 then assert(e.name ~= "clear", what .. ": a clear after the first command") end
      if e.name == "rect" then
        eq(e[5], "black", at .. " is drawn")
        if e[6] then black[#black + 1] = e end
      elseif e.name == "line" then
        if e[5] == "white" then
          assert(inside(e[1], e[2]) and inside(e[3], e[4]), at .. " is white and lies outside any filled black bar (a hit's cross is the only white line)")
          hits_seen = true
        else
          eq(e[5], "black", at .. " is drawn")
        end
      elseif e.name == "circle" then
        eq(e[4], "black", at .. " is drawn")
      elseif e.name == "icon" then
        if e[5] == "white" then
          eq(e[1], "check", at .. ": the only white icon is the inverted Ready button's")
          assert(inside(e[2] + 16, e[3] + 16), at .. " is white outside a filled black button")
          inverted_seen = true
        else
          eq(e[5], "black", at .. " " .. tostring(e[1]) .. " is drawn")
        end
      elseif e.name == "text" then
        if e[5] == "white" then
          eq(e[3], "Ready", at .. ": the only white text is the inverted Ready button's label")
          assert(inside(e[1], e[2] + 8), at .. " is white outside a filled black button")
        else
          eq(e[5], "black", at .. " '" .. tostring(e[3]) .. "' is drawn")
        end
      end
    end
  end
  assert(inverted_seen, "no screen drew the inverted Ready button")
  assert(hits_seen, "no screen drew a hit's white cross")
end

-- A canvas under 466 x 788 is unsupported: the first draw says so in one line naming both canvases, no later draw does, and
-- nothing faults. A supported canvas logs nothing.
function draws.smallCanvas()
  eq(#logged(function() eventsAt(466, 788, stateWith({}), 1, {}) end), 0, "log lines from a draw at 466 x 788")
  eq(#logged(function() eventsAt(474, 788, stateWith({}), 1, {}) end), 0, "log lines from a draw at 474 x 788")
  local first = logged(function() eventsAt(320, 480, stateWith({}), 1, {}) end)
  eq(#first, 1, "log lines from the first draw at 320 x 480")
  assert(first[1]:find("320 x 480", 1, true) and first[1]:find("466 x 788", 1, true), "the line names both canvases: " .. first[1])
  eq(#logged(function() eventsAt(320, 480, stateWith({}), 1, {}) end), 0, "log lines from the second draw")
  eq(#logged(function() eventsAt(320, 480, stateWith({}), 1, { help = true }) end), 0, "log lines from a help draw")
end

return draws
