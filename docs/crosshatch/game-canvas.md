# The game canvas and the match's views

How `GameMatchActivity` (`src/activities/games/`) takes input and what each of its views does. It records the one
declared exception to [touch-and-ui.md](../contributing/touch-and-ui.md) (spine AD-20) and the solo match states
(AD-21).

## The canvas exception

touch-and-ui.md routes every tap through FreeInkUI's interaction table. The game canvas does not, because a game draws
its own screen from a display list (AD-7) and has no FreeInkUI elements to hit-test:

- While a round is in play, taps and long presses come from `touchSnapshotFrom(mappedInput, true)` and swipes from
  `gpio.wasSwipe` (which keeps the swipe's start point). `GameTouch::toEvent` maps them through `GameViewport` into
  canvas coordinates and drops any that start off the canvas, so the game's `input` receives `tap`, `long_press`, and
  `swipe` events in canvas pixels.
- The system's edge gestures never reach the game. The Back gesture (a right swipe from the left 25 %) arrives as
  `Button::Back`; the Home gesture (an up swipe from the bottom 14 %, or the Home key) reaches `handleHomeGesture()`,
  which the match overrides; `ActivityManager` takes the light panel's down swipe first; and `GameTouch` drops every
  edge swipe that is left, classified by `fui::edgeSwipe` exactly as `MappedInputManager` does.
- The runtime's own views (below) are ordinary FreeInkUI: one option dialog built on `UiAppHost`, touch routed with
  `routeTouch`, physical buttons read in `loop()`. Nothing in the match uses `rowTouch`, `colTouch`, or
  `wasTapInRect`.

No other screen may use this exception; a new screen that is not a game canvas follows touch-and-ui.md.

Drawing is in canvas pixels too, and its coordinates and sizes (`x`, `y`, `w`, `h`, `r`, and a line's ends) saturate to
−32,768..32,767 when a `ch.gfx` command is recorded (`DisplayList`), before `FrameReplay` clips it to the canvas. A
shape given wider values is therefore not the shape they describe: `rect(-40000, 0, 80480, 800, c, true)` is recorded
as a rect covering x = −32,768..−2 and draws nothing, and a circle or text anchored beyond the range moves to its edge.
`api-level-1.txt` says the same in a comment beside `ch.gfx`; it is documentation, not an entry of the contract.

## Solo states

`GameCore::MatchLifecycle` (`lib/GameCore/MatchLifecycle.h`) is the state machine; `GameMatchActivity::handle()` is
the only place that changes state and runs what entering a state requires. Any event not listed is ignored.

| State | Event | Next | What the match does |
| --- | --- | --- | --- |
| Starting | the VM started | Playing | The first frame replaces the Games list. |
| Starting | a load or start failure | Error | The error view names the reason in `tr()` text. |
| Playing | Back or Home | Paused | The pause menu (Resume, Leave) opens over the frame; the game gets no input or timers. |
| Playing | the status the game shipped is over | Over | `Session` has delivered `over` once; `ch.store` is flushed; the end-of-round menu (Play again, Leave) opens over the last frame. |
| Paused | Resume, or Back | Playing | The frame is redrawn on a cleared screen with a full refresh; a timer that fell due meanwhile fires now. |
| Paused, Over | Leave | Leaving | See Leaving. |
| Over | Play again | Playing | The queued events are dropped; the VM cancels the pending timer and runs `Session::start()` and `draw()`; ver keeps counting (`GameScript::SoloRounds`). The end-of-round menu stays on screen until the new round's first frame is published (`GameVM::roundsStarted()` moves), so a frame from a step still running when Play again came is never shown. |
| Playing, Paused, Over | a ScriptError or a stuck call | Error | The VM is stopped (a stuck one cancelled, then abandoned after 500 ms); the error view shows. |
| Error | Back | Leaving | See Leaving. |
| any but Leaving | forced exit (sleep, any Replace) | Leaving | See the forced exit. |

Back never leaves a match directly: in play it pauses, in the pause menu it resumes, in the end-of-round menu it does
nothing, and only in the error view does it leave. Home pauses a round in play and is ignored otherwise, until the
match is Leaving (it then goes Home as on any screen, should `goToGames()` fail).

The VM counts a round as ended after the round's last frame is published (`GameVM::roundsEnded()`); the match enters
Over when the count moves, so a round that ends while the pause menu is open shows its end-of-round menu after Resume.

### Overlays

An overlay that `ActivityManager` opens over the match, such as the light panel (its down swipe), stops the match's
`loop()` until it closes, and with it the loop's supervision of the round: the 3 s watchdog, the timer poll, and the
periodic `ch.store` flush. A call already running in the VM runs on unwatched, a timer that falls due fires when the
overlay closes, and a store set meanwhile is written then (or by the forced exit). The round itself is not paused.
Fixing it needs files outside the ledger (`FrontlightPanelActivity`, or `ActivityManager` beyond rows 4 and 5), so it
is deferred (`deferred-work.md`, 3.7).

### Leaving

A user exit runs from `loop()`: take `RenderLock`, cancel the VM and join it for up to 500 ms or abandon it, release the
lock, flush a dirty `ch.store`, then `goToGames()`. When an abandon leaves the task alive (the simulator always does,
since it cannot stop a thread), the slot is still flushed, then leaked with the task when the match is destroyed: the
slot's mutex is held only for a copy inside a locked binding, which an abandon never deletes or leaves suspended, so
the flush waits at most one copy and saves every set made before the leak.

### The forced exit

`onExit()` without a user exit (sleep, any Replace) runs under the `RenderLock` that `ActivityManager` already holds and
never takes it again (pitfall 12cc816): the same stop (cancel, join, or abandon) and the same flush. It is the only SD
write in `onExit()` (AD-17).

## The views

Each view is one framed option dialog: the game's name as a small caption, a `tr()` headline, and large touch targets.
The views draw from the shared icon library (`lib/GameIcons`) by name, as the games do, in the regular weight: the view's 64 px icon is
centred in the dialog's content band, between the text and the rows, and each row's 32 px icon sits at the row's left,
in the ink of the row's label, drawn when the label leaves room for it. `src/games/GameViewIcons.h` names the icons
and holds the room rule; `GameViewIconsTest` checks every name against the library.

| View | Icon | Headline | Body | Options | Buttons |
| --- | --- | --- | --- | --- | --- |
| Pause menu | `pause`; rows `play`, `sign-out` | Paused | -- | Resume, Leave | Back resumes; Up and Down move; Confirm chooses |
| End-of-round menu | `flag-checkered`; rows `arrows-clockwise`, `sign-out` | Game over | -- | Play again, Leave | Up and Down move; Confirm chooses |
| Error view | `warning`; row `sign-out` | The game stopped with an error, or The game could not start | Lua's message or the reason, small type, wrapped to 8 lines | Back | Back |

The error view's reasons for a failed start are `tr()` keys: not enough memory (also when the Session does not fit in
the arena), the game's folder is missing, no Lua files, a Lua file name that is not valid (the only `.lua` files have
names no module can have), Lua files too large, an image is damaged or too large (a package image whose header is
not a 1-bit BMP the canvas can draw, or too large), and cannot read the files (an SD error, or a file gone between the
loader's two passes). Running out of memory inside `LuaGame::load` (its scratch or the Lua state) shows "The game
stopped with an error" with the `tr()` "Not enough memory" text, and a call into the game before its load (defensive;
the VM never makes one) shows the same headline with "The game did not load". A stuck call shows "It stopped responding: one step ran over 3
seconds", unless the VM ended on its own error meanwhile, whose message says more.
