---
title: 'crosshatch game API, level 1 (seed)'
status: draft
api: 1
created: '2026-09-26'
spine: 'ARCHITECTURE-SPINE.md'
---

# crosshatch game API, level 1

> **Seed.** This is the first draft of the reference for people and AI assistants writing crosshatch games. It follows the architecture spine; where the two disagree, the spine wins. Names marked *(draft)* can change until the runtime ships. The final version lives at `docs/crosshatch/game-api.md` and later moves unchanged into the game starter repo.

A crosshatch game is a Lua 5.4 script that runs on an e-ink device with a touchscreen. You write the rules and the drawing. The runtime handles everything else: turns, passing the device between players, the radio link for two-device play, saving, and errors. One script works alone, in pass-and-play, and in Play Nearby without any changes.

## 1. The package

A game is one `.cpgame` file: a zip archive with these files at its root.

| File | Required | What it is |
| --- | --- | --- |
| `manifest.json` | yes | Describes the game (below). |
| `main.lua` | yes | Returns the game table (section 2). |
| `*.lua` | no | Extra modules, loaded with `require("name")` from inside the package only. |
| `icon.png` | no | The launcher icon. *(size and colors: draft)* |

`manifest.json`:

```json
{
  "id": "tic-tac-toe",
  "name": "Tic-Tac-Toe",
  "version": "1.0.0",
  "api": 1,
  "seats": { "min": 1, "max": 2 },
  "modes": ["solo", "pass", "nearby"],
  "hidden": false
}
```

| Key | Rule |
| --- | --- |
| `id` | Lowercase letters, digits, and `-`; starts with a letter or digit; at most 32 characters. Installing a package with the same `id` replaces the old one and keeps its saves. |
| `name` | Shown in the launcher. |
| `version` | Any string. A new version discards unfinished saved games. |
| `api` | The API level the game needs. This document is level 1. |
| `seats` | How many players the game takes. v1 devices support at most 2. |
| `modes` | Any of `solo` (one player), `pass` (players share one device), `nearby` (each player on their own device). |
| `hidden` | `true` if players must not see each other's screen (Battleship, Hangman). In `pass` mode, the runtime then blanks the screen and asks for the device to be handed over between turns. |

Install: upload the `.cpgame` on the device's web Games page, or copy it into `/games/` on the SD card and open the Games launcher.

## 2. The game table

`main.lua` returns a table with five functions. Don't define other globals the runtime might use, and never loop waiting for input: the runtime calls you.

```lua
local game = {}

function game.setup(ctx)                   -- returns the starting state
function game.status(state)                -- returns {turn = seat} or {over = true, winners = {...}}
function game.apply(state, seat, move)     -- returns the new state, or nil, "reason"
function game.draw(state, seat, ui)        -- draws the screen for this seat
function game.input(state, seat, ui, ev)   -- returns a move, or nil

return game
```

### The rules

- **`state` is the whole game, and only `apply` changes it.** Keep everything the players share in `state`: the board, scores, whose turn it is. `apply` gets its own copy of `state`; change that copy and return it, or return a new table. Changes made in `draw`, `input`, or `status` are thrown away.
- **`ui` is for this screen only.** A selected cell, a cursor, an open menu: put these in `ui`. Each device has its own `ui`, you may change it anywhere, and it is never sent or saved.
- **A move is a small table** that describes what a player did, such as `{cell = 5}`. `input` turns a tap into a move; `apply` checks it and applies it. Return `nil, "reason"` from `apply` to reject an illegal move.
- **`setup` and `apply` run only on one device** (the host in Play Nearby). The others receive the new `state` automatically. So `math.random` is safe to use in `setup` and `apply`.
- **Seats are numbers from 1.** Seat 1 is the host in Play Nearby. `status` says whose turn it is; the runtime passes moves to `apply` only from that seat.
- **A computer opponent**, if your game has one, plays inside `apply`: apply the human's move, then compute and apply the computer's move before returning.

### Arguments

| Name | Contents |
| --- | --- |
| `ctx` (in `setup`) | `ctx.seats` (number of players), `ctx.mode` (`"solo"`, `"pass"`, or `"nearby"`) |
| `seat` (in `draw`, `input`) | The seat this screen belongs to. In `pass` mode it is the seat whose turn it is. |
| `ev` (in `input`) | `{kind = "tap", x, y}`, `{kind = "long_press", x, y}`, or `{kind = "swipe", dir = "left" \| "right" \| "up" \| "down"}` |

The edge swipes for Back, Home, and Menu belong to the device and never reach your game.

## 3. What values can go in `state`

`state`, moves, and `ch.store` can hold only:

- `nil`, booleans, integers, floats, strings
- tables with string or integer keys, nested at most 16 deep

No functions, metatables, or tables that contain themselves. **Size limits:** `state` at most 1,400 bytes when packed, a move at most 256 bytes, `ch.store` at most 4 KB. Breaking a limit stops the game with an error, in every mode, so you find out in pass-and-play before a Play Nearby match does.

Tips for staying small: store a board as one string (`"x.o......"`) or a flat array of small integers rather than nested tables of strings.

## 4. The `ch` library

Everything the runtime offers is in the global table `ch`.

### `ch.screen`

`ch.screen.w`, `ch.screen.h`: the screen size in pixels, portrait. Always lay out from these; devices differ.

### `ch.gfx` (drawing)

Call these only inside `draw`. The screen shows what you drew when `draw` returns.

| Function | Notes |
| --- | --- |
| `ch.gfx.clear(color)` | Fill the screen. |
| `ch.gfx.rect(x, y, w, h, color, filled)` | `filled` defaults to `false`. |
| `ch.gfx.line(x1, y1, x2, y2, color)` | |
| `ch.gfx.circle(x, y, r, color, filled)` | *(draft)* |
| `ch.gfx.text(x, y, str, size, color, align)` | `size`: `"small"`, `"medium"`, `"large"`; `align`: `"left"`, `"center"`, `"right"` *(draft)* |
| `ch.gfx.text_width(str, size)` | *(draft)* |
| `ch.gfx.refresh(mode)` | Ask for `"fast"` (default), `"half"`, or `"full"` for this frame. The device may do a fuller refresh than you asked for, never a lighter one. |

Colors: `"white"`, `"light"`, `"dark"`, `"black"`.

E-ink tips: every screen update is slow (about 0.7 s for a fast refresh) and leaves faint ghosts. Draw whole frames, ask for `"full"` after big changes such as a new round, and prefer high contrast.

### `ch.store` (saved data)

One table per game that survives restarts, for things like high scores.

| Function | Notes |
| --- | --- |
| `ch.store.get()` | Returns the saved table, or an empty table. |
| `ch.store.set(t)` | Saves `t` (section 3 limits apply). |

You don't need to save unfinished games: in `solo` and `pass` mode the runtime saves `state` itself and offers to resume.

### Other

| Function | Notes |
| --- | --- |
| `ch.api` | The device's API level (an integer). |
| `ch.time.ms()` | Milliseconds since the game started. Useful for timing, not for game rules. |
| `ch.log(...)` | Writes to the device's debug log. |

The standard `table`, `string`, and `math` libraries are available. `io`, `os`, `debug`, `load`, `loadfile`, and `dofile` are not.

## 5. Limits and errors

- **Speed.** The device runs about 2 million Lua instructions per second. Each call into your game may use at most about 2 million, roughly one second; more stops the game. Keep searches small: limit depth, prune, or use tables.
- **Memory.** Each game has 256 KB of Lua memory.
- **Errors.** Any Lua error stops the game and shows the message with its line number. In Play Nearby, the other device is told the match ended.

## 6. A complete example

Two-player tic-tac-toe that works in every mode:

```lua
local game = {}
local LINES = {{1,2,3},{4,5,6},{7,8,9},{1,4,7},{2,5,8},{3,6,9},{1,5,9},{3,5,7}}

local function layout()
  local size = math.min(ch.screen.w, ch.screen.h) - 40
  return (ch.screen.w - size) // 2, (ch.screen.h - size) // 2, size // 3
end

function game.setup(ctx)
  return { board = ".........", turn = 1 }
end

local function winner(board)
  for _, l in ipairs(LINES) do
    local a = board:sub(l[1], l[1])
    if a ~= "." and a == board:sub(l[2], l[2]) and a == board:sub(l[3], l[3]) then
      return a == "x" and 1 or 2
    end
  end
end

function game.status(state)
  local w = winner(state.board)
  if w then return { over = true, winners = { w } } end
  if not state.board:find(".", 1, true) then return { over = true, winners = {} } end
  return { turn = state.turn }
end

function game.apply(state, seat, move)
  local i = move.cell
  if type(i) ~= "number" or i < 1 or i > 9 or state.board:sub(i, i) ~= "." then
    return nil, "cell taken"
  end
  local mark = seat == 1 and "x" or "o"
  state.board = state.board:sub(1, i - 1) .. mark .. state.board:sub(i + 1)
  state.turn = 3 - seat
  return state
end

function game.draw(state, seat, ui)
  local x0, y0, c = layout()
  ch.gfx.clear("white")
  for i = 1, 2 do
    ch.gfx.line(x0 + i * c, y0, x0 + i * c, y0 + 3 * c, "black")
    ch.gfx.line(x0, y0 + i * c, x0 + 3 * c, y0 + i * c, "black")
  end
  for i = 1, 9 do
    local m = state.board:sub(i, i)
    if m ~= "." then
      local cx = x0 + ((i - 1) % 3) * c + c // 2
      local cy = y0 + ((i - 1) // 3) * c + c // 2
      ch.gfx.text(cx, cy, m:upper(), "large", "black", "center")
    end
  end
  local s = game.status(state)
  local msg = s.over and (#s.winners == 0 and "Draw" or ("Player " .. s.winners[1] .. " wins"))
    or ("Player " .. s.turn .. "'s turn")
  ch.gfx.text(ch.screen.w // 2, y0 - 30, msg, "medium", "black", "center")
end

function game.input(state, seat, ui, ev)
  if ev.kind ~= "tap" then return nil end
  local x0, y0, c = layout()
  local col, row = (ev.x - x0) // c, (ev.y - y0) // c
  if col < 0 or col > 2 or row < 0 or row > 2 then return nil end
  return { cell = row * 3 + col + 1 }
end

return game
```
