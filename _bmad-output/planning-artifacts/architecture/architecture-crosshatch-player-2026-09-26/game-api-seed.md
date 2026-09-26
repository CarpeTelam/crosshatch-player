---
title: 'crosshatch game API, level 1 (seed)'
status: draft
api: 1
created: '2026-09-26'
updated: '2026-09-26'
spine: 'ARCHITECTURE-SPINE.md'
---

# crosshatch game API, level 1

> **Seed.** This is the first draft of the reference for people and AI assistants writing crosshatch games. It follows the architecture spine; where the two disagree, the spine wins. Names marked *(draft)* can change until the runtime ships. The final version lives at `docs/crosshatch/game-api.md`, next to a LuaLS `---@meta` stub (`ch.d.lua`), and later moves unchanged into the game starter repo.

A crosshatch game is a Lua 5.5 script that runs on an e-ink device with a touchscreen. You write the rules and the drawing. The runtime handles everything else: turns, passing the device between players, the radio link for two-device play, saving, and errors. One script works alone, in pass-and-play, and in Play Nearby without any changes.

**What it's for.** crosshatch is built for simple, turn-based games: the kind you could play with pen and paper, a board, cards, dice, or words, plus puzzles and parlor games. Think tic-tac-toe, Dots and Boxes, Battleship, Hangman, Sudoku, or a party guessing game. It is not an engine for platformers, action games, or anything that needs a frame loop: the screen is e-ink, and every update takes most of a second. The API grows only in ways that serve this kind of game.

## 1. The package

A game is one `.cpgame` file: a zip archive with these files at its root and nothing else (no folders).

| File | Required | What it is |
| --- | --- | --- |
| `manifest.json` | yes | Describes the game (below). |
| `main.lua` | yes | Returns the game table (section 2). |
| `name.lua` | no | Extra modules, named with lowercase letters, digits, and `_`. Load one with `require("name")`. |
| `icon.png` | no | The launcher icon, converted to 64×64 black and white. Must be non-interlaced. Or name a library icon in the manifest instead. |
| `name.png` | no | Your own images for `ch.gfx.image`, named with lowercase letters, digits, and `_`. Non-interlaced; converted to black and white at install. |

Limits: the whole package at most 256 KB, at most 32 files, each file at most 128 KB unpacked, and at most 128 KB of converted images. Lua files must be source text; precompiled bytecode is refused.

`manifest.json`:

```json
{
  "id": "tic-tac-toe",
  "name": "Tic-Tac-Toe",
  "version": "1.0.0",
  "api": 1,
  "seats": { "min": 2, "max": 2 },
  "modes": ["pass", "nearby"],
  "hidden": false,
  "icon": "mark_x"
}
```

| Key | Rule |
| --- | --- |
| `id` | Lowercase letters, digits, and `-`; starts with a letter or digit; at most 32 characters. Installing a package with the same `id` replaces the old one and keeps its saved data. |
| `name` | Shown in the launcher. |
| `version` | Any string, for people. |
| `api` | The API level the game needs. This document is level 1. |
| `seats` | How many players the game takes. v1 devices support at most 2. |
| `modes` | One or more of `solo` (one player; needs `seats.min` of 1), `pass` (players share one device), `nearby` (each player on their own device; needs `seats.max` of 2 or more). |
| `icon` | Optional. The name of a library icon (section 4) to use as the game's icon when the package has no `icon.png`. |
| `hidden` | Optional, default `false`. Set `true` if players must not see each other's screen (Battleship, Hangman). In `pass` mode the runtime then blanks the screen and asks for the device to be handed over between turns. |

Unknown keys are ignored.

**Install:** copy the `.cpgame` into the `/games/` folder on the SD card (with the device's web file manager, or over USB), then open Games on the device. The device installs it and removes the file. If the package is broken, the file is renamed `*.cpgame.bad` and the launcher tells you why.

**Changing a game:** any change to the package's files counts as a new package. Unfinished saved games of the old package are discarded, and two devices can only play Play Nearby when both have exactly the same package.

## 2. The game table

`main.lua` returns a table with five functions. Never loop waiting for input: the runtime calls you.

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

- **`state` is the whole game, and only `apply` changes it.** Keep everything the players share in `state`: the board, scores, whose turn it is. `apply` gets its own copy; change it and return it, or return a new table. Changes made in `draw`, `input`, or `status` are thrown away.
- **`status` must depend only on `state`.** No clocks, no random numbers. It decides whose turn it is and when the round is over.
- **`ui` is for this player's screen only.** A selected cell, a cursor, an open menu: put these in `ui`. You may change it anywhere, and it is never sent or saved. In pass-and-play each player gets their own `ui`, so one player's selection never shows on the other's turn.
- **A move is a small table** describing what a player did, such as `{cell = 5}`. `input` turns a tap into a move; `apply` checks it and applies it. Return `nil, "reason"` from `apply` to reject an illegal move.
- **Rejections come back as an event.** When a move is rejected, the player's `input` receives `{kind = "rejected", reason = "..."}`. Show it however you like, for example by setting a message in `ui`.
- **The end of a round is an event too.** When a round ends, each local player's `input` receives `{kind = "over"}` exactly once. Record per-device results such as wins there, not in `draw`, which runs many times.
- **One move at a time.** After `input` returns a move, further moves are ignored until the result comes back.
- **`setup` and `apply` run on one device only** (the host in Play Nearby). The others receive the new `state` automatically, so `math.random` is safe in `setup` and `apply`. The runtime seeds it for you.
- **Seats are numbers from 1.** Seat 1 is the host in Play Nearby. The runtime passes moves to `apply` only from the seat that `status` names.
- **A computer opponent**, if your game has one, plays inside `apply`: apply the human's move, then compute and apply the computer's move before returning.

### Arguments

| Name | Contents |
| --- | --- |
| `ctx` (in `setup`) | `ctx.seats` (number of players in this match), `ctx.mode` (`"solo"`, `"pass"`, or `"nearby"`) |
| `seat` (in `draw`, `input`) | The seat this screen belongs to. In `pass` mode it is the seat whose turn it is; in a hidden game, the player who just moved until they pass the device; and `0` ("everyone") once the round is over, with its own `ui` table. |
| `ev` (in `input`) | `{kind = "tap", x, y}`, `{kind = "long_press", x, y}`, `{kind = "swipe", x, y, dir}` (`x, y` is where the swipe started; `dir` is `"left"`, `"right"`, `"up"`, or `"down"`), `{kind = "rejected", reason}`, `{kind = "over"}`, or `{kind = "timer"}` |

There is no dragging. Some swipes belong to the device and never reach your game: a right-swipe starting in the left quarter of the screen (Back) and an up-swipe from the bottom edge (Home) open the device's pause menu, and a down-swipe from the top edge opens the device's light panel.

### When things happen

- `draw` is called after every change to `state`, after every `input` call, and after the hand-off screen. There is no frame loop; use `ch.timer` (section 4) for countdowns.
- When `status` says the round is over, the device shows its own end-of-round menu over your last frame: **Play again** (which calls `setup` again with the same players) or **Leave**.
- In `pass` mode with `hidden = true`, after a move that changes whose turn it is, the mover first sees the result (your `draw` is called with the mover's seat), then taps to pass the device; the screen goes blank until the next player taps. The same blank screen appears when a hidden game starts or resumes.

## 3. What values can go in `state`

`state`, moves, and `ch.store` can hold only:

- `nil`, booleans, integers (64-bit), floats, strings
- tables with string or integer keys, nested at most 16 deep

No functions, metatables, or tables that contain themselves. **Size limits:** `state` at most 1,400 bytes when packed, a move at most 256 bytes, `ch.store` at most 4 KB. Breaking a limit stops the game with an error in every mode, so you find out in pass-and-play before a Play Nearby match does.

To stay small, store a board as one string (`"x.o......"`) or a flat array of small integers rather than nested tables of strings.

## 4. The `ch` library

Everything the runtime offers is in the global table `ch`.

### `ch.screen`

`ch.screen.w`, `ch.screen.h`: your canvas size in pixels, portrait. Always lay out from these; devices differ. You own the whole canvas.

### `ch.gfx` (drawing)

Call these only inside `draw`; anywhere else they raise an error. The screen shows what you drew when `draw` returns.

| Function | Notes |
| --- | --- |
| `ch.gfx.clear(color)` | Fill the canvas. |
| `ch.gfx.rect(x, y, w, h, color, filled)` | `filled` defaults to `false`. |
| `ch.gfx.line(x1, y1, x2, y2, color)` | `"white"` or `"black"` only. |
| `ch.gfx.circle(x, y, r, color, filled)` | *(draft)* |
| `ch.gfx.text(x, y, str, size, color, align)` | `size`: `"small"`, `"medium"`, `"large"`; `color`: `"white"` or `"black"`; `align`: `"left"`, `"center"`, `"right"` *(draft)* |
| `ch.gfx.icon(name, x, y, size, color)` | Draw a library icon (below) with its top-left corner at `x, y`. `size`: `"small"` (32 px), `"medium"` (64 px), `"large"` (128 px); `color`: `"white"` or `"black"`. |
| `ch.gfx.image(name, x, y, color)` | Draw one of your package's images at its own size; `name` is the file name without `.png`. |
| `ch.gfx.refresh(mode)` | Ask for `"fast"` (default), `"half"`, or `"full"` for this frame. The device may refresh more fully than you asked, never less. |

Colors: `"white"`, `"light"`, `"dark"`, `"black"`. `"light"` and `"dark"` work for fills (they are drawn as fine dot patterns); lines, text, icons, and images are black or white.

`ch.text_width(str, size)` returns the width of `str` in pixels. Unlike `ch.gfx`, you can call it anywhere, for example in `input` to hit-test a line of text.

### Icons

The device has a built-in icon library, and using it gives your game the same look as the rest of crosshatch. Prefer a library icon over drawing your own: `ch.gfx.icon("suit_heart", x, y, "medium", "black")`.

The set covers marks, card suits, dice faces, board pieces, player markers, and common controls. Names are lowercase with `_`, such as `mark_x`, `suit_spade`, `die_6`, `piece_king`, `arrow_left`. *(The full list is draft; the reference will include a catalog with pictures.)* A name that doesn't exist stops the game with an error. Icons never change meaning within an API level, and new levels only add names.

When you need something the library doesn't have, ship it as a package image and draw it with `ch.gfx.image`.

A frame holds at most 2,048 drawing calls; more stops the game with an error.

E-ink tips: every screen update is slow (about 0.7 s for a fast refresh) and leaves faint ghosts. Draw whole frames, ask for `"full"` after big changes such as a new round, and prefer high contrast.

### `ch.store` (saved data)

One table per game on each device that survives restarts, for things like high scores.

| Function | Notes |
| --- | --- |
| `ch.store.get()` | Returns the saved table, or an empty table. |
| `ch.store.set(t)` | Saves `t` (section 3 limits apply; checked immediately). The device writes it to the card shortly after. |

`ch.store` belongs to the device it runs on. Calls made in `apply` happen only on the host, so record per-device results when `input` receives `{kind = "over"}`.

You don't need to save unfinished games: in `solo` and `pass` mode the runtime saves after every move and offers "Continue" in the launcher.

### Other

| Function | Notes |
| --- | --- |
| `ch.api` | The device's API level (an integer). |
| `ch.timer.after(ms)` | Deliver a `{kind = "timer"}` event to `input` after `ms` milliseconds (at least 1,000). One timer at a time: a new call replaces the pending one. For countdowns in parlor games. If time running out changes the game, return a move from `input` so it goes through `apply`. |
| `ch.timer.cancel()` | Clear the pending timer. |
| `ch.time.ms()` | Milliseconds since the game started. For display only; never use it in `status` or game rules. |
| `ch.log(...)` | Writes to the device's debug log. `print` does the same. |

Available standard libraries: `table`, `string`, `math`, `utf8`, and the basic functions except `load`, `loadfile`, and `dofile`. Not available: `io`, `os`, `debug`, `coroutine`, `package` (use the package-local `require`).

Lua 5.5 notes: `global` is a reserved word, and a `for` loop's control variable is read-only (declare a local with the same name to change it).

## 5. Limits and errors

- **Speed.** The device runs about 2 million Lua instructions per second. Each call into your game may use at most about 2 million, roughly one second; more stops the game. Keep searches small: limit depth, prune, or use tables. Long string pattern matches count too.
- **Memory.** Each game has 256 KB of Lua memory.
- **Errors.** Any Lua error stops the game and shows a short message, the game name, and the Lua error with its line number. In Play Nearby, the other device is told the match ended.

## 6. A complete example

Two-player tic-tac-toe that works in pass-and-play and Play Nearby:

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
  if math.type(i) ~= "integer" or i < 1 or i > 9 or state.board:sub(i, i) ~= "." then
    return nil, "That square is taken"
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
      local x = x0 + ((i - 1) % 3) * c + (c - 128) // 2
      local y = y0 + ((i - 1) // 3) * c + (c - 128) // 2
      ch.gfx.icon(m == "x" and "mark_x" or "mark_o", x, y, "large", "black")
    end
  end
  local s = game.status(state)
  local msg
  if s.over then
    msg = #s.winners == 0 and "Draw" or ("Player " .. s.winners[1] .. " wins")
  else
    msg = ui.message or ("Player " .. s.turn .. "'s turn")
  end
  ch.gfx.text(ch.screen.w // 2, y0 - 30, msg, "medium", "black", "center")
end

function game.input(state, seat, ui, ev)
  if ev.kind == "rejected" then
    ui.message = ev.reason
    return nil
  end
  if ev.kind ~= "tap" then return nil end
  ui.message = nil
  local x0, y0, c = layout()
  local col, row = (ev.x - x0) // c, (ev.y - y0) // c
  if col < 0 or col > 2 or row < 0 or row > 2 then return nil end
  return { cell = row * 3 + col + 1 }
end

return game
```
