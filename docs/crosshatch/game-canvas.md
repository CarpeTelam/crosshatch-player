# The game canvas and the match's views

How `GameMatchActivity` (`src/activities/games/`) takes input and what each of its views does. It records the one
declared exception to [touch-and-ui.md](../contributing/touch-and-ui.md) (spine AD-20), the solo match states
(AD-21), and the hidden pass match's hand-off states.

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
| Starting | the VM started | Playing | The first frame replaces the Games list; gestures made before it is published and drawn are read and dropped, as after Play again (a tap that opened the game, lifted late, is not the game's first input). With `Start::Resume` and a usable `resume.bin` the VM continues from the saved snapshot at its saved ver and `setup` does not run; otherwise (no `.pkg`, no save, a save that is not usable: logged) a new match starts. A save that is there but cannot be read, or that the VM refuses, does not start a new match over it: see the next row. |
| Starting | a load or start failure | Error | The error view names the reason in `tr()` text. So does a `Start::Resume` whose `resume.bin` could not be read or was refused by the VM ("The saved match could not be resumed"): the file is left as it was ([resume.bin](formats.md#resumebin)). |
| Playing | Back or Home | Paused | The pause menu (Resume, Leave) opens over the frame; the game gets no input or timers. |
| Playing | the status the game shipped is over | Over | `Session` has delivered `over` once; `ch.store` is flushed; `resume.bin` and its tmp are deleted (an over snapshot is never written, and one still pending deletes the file instead); the end-of-round menu (Play again, Leave) opens over the last frame. A round that ends while the pause menu is open deletes the file at once, from the pause menu's loop pass. |
| Paused | Resume, or Back | Playing | The frame is redrawn on a cleared screen with a full refresh; a timer that fell due meanwhile fires now. In the Play-again gap (Play again, then Back and Resume before the new round's first frame is published) nothing is redrawn: the pause menu stays on screen but is inert (its routing is closed, and `loopPlaying` drops every gesture), and an overlay closed in the gap likewise leaves its pixels on screen, until the new round's first frame is drawn on a cleared screen with a full refresh. A pause menu opened in the gap says so: its line "Starting the next round" sits under the headline, and the menu is drawn again without it once the round's first frame is published (`GameMatchActivity::gapWhenPaused`). The last round's board, which would take no taps, is never shown. |
| Paused, Over | Leave | Leaving | See Leaving. |
| Over | Play again | Playing | The new round's snapshots are written to `resume.bin` again. The queued events are dropped; the VM cancels the pending timer and runs `Session::start()` and `draw()`; ver keeps counting (`GameScript::SoloRounds`). The end-of-round menu stays on screen until the new round's first frame is published (`GameVM::roundsStarted()` moves), so a frame from a step still running when Play again came is never shown. Gestures made until that frame is published, and then until the render task has drawn it and handed it to the panel (`displayBuffer` has returned, `GameMatchActivity::roundsDisplayed`), are read and dropped (`GameMatchActivity::loopPlaying`). |
| Playing, Paused, Over | a ScriptError or a stuck call | Error | The VM is stopped (a stuck one cancelled, then abandoned after 500 ms); the error view shows. |
| Error | Back | Leaving | See Leaving. |
| any but Leaving | forced exit (sleep, any Replace) | Leaving | See the forced exit. |

### Resume

A match with an installed package (`.pkg`) keeps `resume.bin` (docs/crosshatch/formats.md) so a solo match survives
sleep. The VM hands each snapshot it commits to the loop task through a latest-wins mailbox (`GameVM::committed()`), and
in Playing and Paused every loop pass writes the newest one (a failed write is retried 5 s later). Leave and the forced
exit write the last pending one, once more, after the VM has stopped and before it is freed (a write that failed less than
5 s before is not retried then either). Only Over
deletes the file; Leave and the forced exit keep it. Entering with `Start::Resume` restores it (`Session::restore`), so
the match continues from the snapshot and does not write it again until a move commits. The VM task never touches the
card (AD-5).

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

A user exit runs from `loop()`: take `RenderLock`, cancel the VM and join it for up to 500 ms or abandon it (writing the
last pending snapshot as `resume.bin` between the two, unless the round is over), release the lock, flush a dirty
`ch.store`, then `goToGames()`. When an abandon leaves the task alive (the simulator always does,
since it cannot stop a thread), the slot is still flushed, then leaked with the task when the match is destroyed: the
slot's mutex is held only for a copy inside a locked binding, which an abandon never deletes or leaves suspended, so
the flush waits at most one copy and saves every set made before the leak.

### The forced exit

`onExit()` without a user exit (sleep, any Replace) runs under the `RenderLock` that `ActivityManager` already holds and
never takes it again (pitfall 12cc816). It notes `millis()` at its very start, and the order is:

1. cancel the VM and join it (up to 500 ms);
2. in a hidden pass match only, the blank hand-off screen (`FrameReplay::drawBlank`, the screen HandOff shows) is drawn
   and pushed with a half refresh (`HalDisplay::HALF_REFRESH`) and logged ("forced exit: blank hand-off screen
   pushed"), whether the VM joined or not, so no seat's frame stays on the panel while the device sleeps (AD-12, AD-20).
   The match is Leaving, so render draws nothing, and the render task is not inside `render()` while the lock is held,
   so the loop task may use `replay` and the framebuffer here. Any state with a VM gets it, the error view of a script
   error included (only a stuck call frees the VM on the way to Error). Solo and open pass matches, a user Leave, and an
   exit with no VM (Error after a stuck call, or after Leave) push nothing. The half refresh is AD-12's choice, where
   the hand-off between seats uses a full one; whether it leaves a ghost of the seat's frame is checked on an X4 Pro
   (epic-pass-and-play entry 11);
3. the pending snapshot is written as `resume.bin` (in Playing or Paused only), before an abandon can free the memory it
   lives in;
4. abandon the VM if it did not join (up to 500 ms more); a VM that ends within that wait has its last published
   snapshot written before it is deleted;
5. a `resume.bin` delete that Over could not finish is retried;
6. the `ch.store` flush.

Those are the only SD writes in `onExit()` (AD-17). Each of steps 3, 4's write, 5, and 6 starts only if less than
`FORCED_EXIT_DEADLINE_MS` (1,500 ms) has passed since the start of `onExit()`; a step that would start later is skipped
and logged with one `LOG_ERR` line naming it. Step 2 is no SD step and the deadline does not gate it, but its time counts
against the deadline of the steps after it. A resume write that failed less than 5 s (`FLUSH_INTERVAL_MS`) before is not
retried at the exit either. Leave has no deadline and follows the same order (its delete retry and store flush come after
it releases `RenderLock`).

**The bound.** The stop's two waits count `millis()`, not polls: the join ends 500 ms (`STOP_TIMEOUT_MS`) after it began
and the abandon 500 ms (`GameVM::ABANDON_WAIT_MS`) after it began. Each is late by its last iteration: a poll
(`GameVM::STOP_POLL_MS`, 5 ms) and, in the abandon, the settle of up to 10 ticks that follows a suspend (10 ms at the
firmware's 1 kHz tick) and the wait for `taskMutex` in `deleteIfStuckInLua`. Teardown (freeing the VM's memory) is not
counted. So a VM that never stops, such as one held inside a locked binding, costs about 1,030 ms of `RenderLock`
before the SD steps, on a device whose polls run on time; a starved poll makes it later by that much. The SD steps add
only what starts before the deadline: each is one tmp write and rename, the card's time, and nothing starts after
1,500 ms from the start of `onExit()`. `ResumeMatchTest` pins the arithmetic on the host's fake clock (a timeout no
poll divides into, so a late poll shows), counts elapsed time under a thread that advances the clock faster than the
polls do, and checks the order and the skipped steps.

A hidden pass match's blank (step 2) is pushed after the join's wait and before the abandon's: at once when the VM
joins, about 500 ms after `onExit()` began when it is stuck. Its half refresh counts against the 1,500 ms window, for
every forced exit of a hidden pass match, and is unmeasured (entry 11; `lib/hal/HalDisplay.h` names 1,720 ms for a
half refresh, source unstated). If `displayBuffer` returns only after that long, every SD step after it is skipped,
even when the VM joined at once. The host double's push costs no clock, so `GameMatchTest` moves the clock by hand
where it needs the push to take time. Measured on an X4 Pro before the blank existed, so without it: a forced exit
with a stuck VM held `RenderLock` for 502 ms (epic-install-and-launcher entry 14).

**Resume before store.** The resume write (step 3) comes before the `ch.store` flush (step 6), so a slow exit can skip the
store flush after the resume write ran: Continue may then resume a board newer than `ch.store` holds. Deferred
(retrospective rev-5); its trigger is a device log line `forced exit past 1500 ms; skipped the ch.store flush`.

## Hidden pass states

A pass match whose manifest says `hidden` (each seat's frame is its own: cards, a fleet) never shows one seat's frame to
another. `MatchLifecycle(true)` runs the hidden pass machine (`lib/GameCore/MatchLifecycle.h`), and the match builds its
`GameVM` with the hand-off flag, so the VM draws only the seat the match asks for (`GameVM::drawShown`, never
`GameCore::NO_SEAT`): nothing after a round begins, the turn seat after `GameVM::showTurnSeat()`, and the mover after a
move that passes the turn. Solo and open pass matches run the solo machine above and never reach these states.

| State | Event | Next | What the match does |
| --- | --- | --- | --- |
| Starting | the VM started | HandOff | The VM runs `setup` and draws nothing; the blank hand-off screen replaces the Games list. |
| HandOff | a tap anywhere, or Confirm | Playing | The match asks the VM for the turn seat (`showTurnSeat`, which the VM serves ahead of any queued event) and keeps the blank on the panel until that seat's frame is published: the loop asks for no render and drops gestures, and `renderCanvas` draws nothing, until `GameVM::seatShownRequest()` reaches the request (`seatAwaited`), then gestures until render has pushed that frame (`seatDisplayed`). The frame is drawn on a cleared screen with a full refresh. |
| Playing | a move that passes the turn | Result | The VM draws the mover's own frame again and counts the move (`GameVM::turnsPassed`, `passedTo`); the match shows that frame with the banner "Tap to pass to player N" (a fast refresh, no button hints). |
| Playing | the status the game shipped is over | Over | As in solo: a move that ends the round is RoundOver, never a turn change, and the end-of-round menu sits over seat 0's frame, the one for everyone. |
| Result | a tap anywhere, or Confirm | HandOff | The blank hand-off screen. |
| Result, HandOff | Back or Home | Paused | The pause menu. From Result it sits over the mover's frame; from HandOff it sits on no frame (a cleared screen), since the device is between players. |
| Paused | Resume, or Back | Result, HandOff, or Playing | The state the menu was opened from: Result redraws the mover's frame and banner, HandOff the blank. |
| Result, HandOff | a ScriptError or a stuck call | Error | As in solo: the loop watches the VM there as in a menu. |
| Over | Play again | HandOff | The VM begins the new round (`setup`) and draws nothing; the blank shows at once, and its tap shows the new round's first seat. The Play-again gap's rules hold on top of the seat gate: no frame of the last round is drawn after the blank. |

**The blank.** `FrameReplay::drawBlank` clears the screen and draws the `eye-closed` library icon at 128 px, black, centred
on the canvas, and no game command; the hand-off screen has no other text. The match pushes it with a full refresh, so
nothing of the last seat's frame stays on the panel, and only then publishes its tap zone (`fui::tapZones` over the safe
area, routed by `routeTouch`).

A forced exit (sleep, any Replace) pushes the blank too, with a half refresh, from any state with a VM: see The forced
exit, step 2.

**Taps.** The banner's screen and the blank accept a tap anywhere and Confirm, and only once render has pushed that
state's own screen (`GameMatchActivity::passScreenShown`, which `handle()` resets on every transition): a tap or a Confirm
press made before the banner or the blank is on the panel, or while it is being pushed, is read and dropped, so a double
tap cannot skip either. Result's banner is a framed panel at the bottom of the screen; the tap zone under it covers the
whole safe area.

**Moves in Result.** A tap the mover made right after its turn-passing move, queued behind it, still reaches the mover's
own `input` (its `ui` may change, and the match redraws the banner over the new frame), and the move it returns is
discarded by the Session, since the mover is no longer the turn seat. While the match is in Result or HandOff, and until
the next seat's frame is on the panel, it posts no game input, so before the VM draws the next seat it plays whatever is
still queued under the view it was meant for (the mover, or on the hand-off screen nobody): no queued tap reaches the
next seat.

**Timers.** A timer that falls due in Result or HandOff (polled by the loop there, or already queued) is held by the VM and
delivered to the next seat right after its first frame; one the game re-armed or cancelled meanwhile is dropped as stale,
as anywhere else. Play again drops a held timer with the last round.

A hidden pass match keeps no `resume.bin` (pass does not save until epic-pass-and-play entry 9).

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
| Result banner (hidden pass) | -- | -- | "Tap to pass to player N" in a framed panel at the bottom, over the mover's frame | a tap anywhere | Confirm passes; Back pauses; no hints |
| Hand-off screen (hidden pass) | `eye-closed`, 128 px, centred on the blank canvas | -- | -- | a tap anywhere | Confirm passes; Back pauses |

The pause menu opened in the Play-again gap adds the line "Starting the next round" under its headline (see Paused ->
Playing above).

The error view's reasons for a failed start are `tr()` keys: not enough memory (also when the Session does not fit in
the arena), the game's folder is missing, no Lua files, a Lua file name that is not valid (the only `.lua` files have
names no module can have), Lua files too large, an image is damaged or too large (a package image whose header is
not a 1-bit BMP the canvas can draw, or too large), and cannot read the files (an SD error, or a file gone between the
loader's two passes). The same headline covers every failure of the host's own inside the VM task, since each comes
before any game code runs (AD-14, as amended by the owner 2026-09-28; `GameScript::failedToStart`): running out of
memory inside `LuaGame::load` (its scratch or the Lua state) shows the `tr()` "Not enough memory" text, and a call into
the game before its load (defensive; the VM never makes one) shows "The game did not load". "The game stopped with an
error" is left for the script's own error, with Lua's message, and for a stuck call, which shows "It stopped responding:
one step ran over 3 seconds", unless the VM ended on its own error meanwhile, whose message says more.

Heap exhaustion after the game has started is the game's own error, in Lua's words. A `LUA_ERRMEM` or a heap-cap fault in a
callback (`setup`, `status`, `apply`, `draw`, `input`) ends the round like any script error: "The game stopped with an
error" and Lua's untranslated "not enough memory" (AD-14 allows Lua's message as it is), where the same condition in
`load()`, before any game code has run, shows the `tr()` text under "The game could not start". The two are different
things to say (a game that ran and used too much, against a device that had no room to begin), so the runtime does not
map the first to the second; the game's heap is capped by the arena, and a game raises the error itself by allocating
without bound (`error("not enough memory", 0)` stops it the same way, api-level-1.txt). A save that cannot be trusted
because a callback ran out of memory is not deleted for the same reason: the error cannot tell a bug in the game from a
transient fault (see the `resume.bin` section of formats.md).
