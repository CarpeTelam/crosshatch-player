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
- Each gesture read in a round in play (state Playing) is logged at `LOG_DBG` with the host's classification of it, one
  line after `<id>: touch ` (`GameTouchLog`, a pure formatter the host tests pin). Logging changes no gesture's outcome:
  what reaches `input`, and in what order, is as before. The formatter runs inside the macro's arguments, so a build with
  `LOG_DBG` compiled out pays nothing, and there is no setting. "Sent" means `classify` accepted the gesture. A later drop
  keeps its own line ("dropped a touch before the frame was on the panel", "dropped a touch that began ... before the
  hand-off passed"), and the VM or the game declining a tap is not logged. The lines show in builds with `LOG_LEVEL` 2
  (the development envs of `platformio.ini` and the simulator); the release envs use 1 and print none. A gesture the game
  does not get says why after a colon (`: not sent, off the canvas`, `: not sent, system edge swipe`). `held` is the
  SDK's `HalGPIO::lastTouchHeldMs()` as latched at release, so it can include the controller's release hold-over. What
  each line means on the device:
  - `tap screen (240,400) canvas (237,391) held 82 ms`: lifted with at most 59 px of excursion and held under 500 ms;
    the point is the touch-down point. A drift of 29 to 59 px that lifts quickly is a tap too, not an ended contact.
  - `long press screen (240,400) canvas (237,391)`: still down, within 28 px of the touch-down point, for 500 ms. The
    line is written at the 500 ms mark, has no `held` (the latched value is stale then), and the lift that follows has no
    line.
  - `swipe left screen (400,400)->(150,420) canvas (397,391) held 120 ms`: a net move of 60 px or more, lifted within
    700 ms. The canvas point is the swipe's start.
  - `tap screen (477,400) held 64 ms: not sent, off the canvas` and `swipe down screen (240,5)->(240,300) held 90 ms: not
    sent, system edge swipe`: the gesture above, refused by `classify` (a start on the bezel, or one of the system's
    edge swipes).
  - `contact ended screen (300,400)->(380,450) held 1200 ms: not sent, not a tap, long press or swipe`: a contact that
    lifted with no gesture. On the device that is (a) a contact held over 700 ms that moved 60 px or more net (the slow
    slide above), (b) one that went past 59 px and came back to under 60 px net, such as `contact ended screen
    (300,400)->(310,405) held 700 ms: ...` after a 90 px excursion, or (c) a multi-finger contact. It sends nothing.
    `screen unknown` (and no `held`, since the latched hold is stale then) when the loop never saw the contact down.
  - The first point of an ended contact is the first position the loop saw (the live point, `isScreenTouchHeld`), which
    for a fast slide can already be past the true touch-down. The last point is the latest position the loop saw. While the
    contact is within 28 px and over 90 ms old, the loop's reading is the touch-down point (`touchSnapshotFrom`), so the
    last point can lag the live finger by up to 28 px.
  - A gesture released on a pass that `loopPlaying` returns from early (Back, a stopped VM, round over, turn passed) is
    never read, so it is never logged.
  - The simulator's HAL classifies taps and releases by its own rules: a slow 40 px slide logs as an ended contact there,
    and a long press's lift logs an extra `contact ended screen unknown` line. What it shows for slides and long-press
    lifts is not the device's.
- The system's edge gestures never reach the game. The Back gesture (a right swipe from the left 25 %) arrives as
  `Button::Back`; the Home gesture (an up swipe from the bottom 14 %, or the Home key) reaches `handleHomeGesture()`,
  which the match overrides; `ActivityManager` takes the light panel's down swipe first; and `GameTouch` drops every
  edge swipe that is left, classified by `fui::edgeSwipe` exactly as `MappedInputManager` does.
- The runtime's own views (below) are ordinary FreeInkUI: one option dialog, or the Result banner's or the hand-off
  screen's one button, built on `UiAppHost`, touch routed with `routeTouch`, physical buttons read in `loop()`. Nothing
  in the match uses `rowTouch`, `colTouch`, or `wasTapInRect`.

No other screen may use this exception; a new screen that is not a game canvas follows touch-and-ui.md.

A game's canvas is the logical screen less the board's bezel insets (`GameViewport::forRenderer`), so its size differs by
device: the X4 Pro gives 466 x 788 (insets {9, 7, 3, 7}) and the Sticky 474 x 788 (the default insets {9, 3, 3, 3}). The
Paper Mono's profile carries the X4 Pro's insets too, but no Paper Mono env sets `FREEINK_CAP_GAMES`, so it runs no games
today. A game designs for a 466 x 788 box and centres it in `ch.screen` (an offset of `(ch.screen.w - 466) // 2` on each
side, so the Sticky shows the same pixel layout with 4 px of white at each side) rather than adapting its layout to the width
it was given. A canvas smaller than 466 x 788 is unsupported: the game does not adapt, it logs one line saying so and lays out
from the canvas's corner, so part of it is clipped. The games check plays every round and check of every game at both
466 x 788 and 474 x 788 (spine AD-7, amended 2026-10-05).

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
| Starting | the VM started | Playing | The first frame replaces the title screen; gestures made before it is published and drawn are read and dropped, as after Play again (a tap that opened the game, lifted late, is not the game's first input). With `Start::Resume` and a usable `resume.bin` the VM continues from the saved snapshot at its saved ver, with the roster the save records (solo, or an open pass match on its saved turn seat; a hidden pass save goes to HandOff, below), and `setup` does not run; otherwise (no `.pkg`, no save, a save that is not usable: logged) a new match starts with the roster it was given. A save that is there but cannot be read, that this host cannot start, or that the VM refuses does not start a new match over it: see the next row. |
| Starting | a load or start failure | Error | The error view names the reason in `tr()` text. So does a `Start::Resume` whose `resume.bin` could not be read or was refused by the VM ("The saved match could not be resumed"), or is a save of this package this host cannot start (`GameSaveStore`'s `Unstartable`: "This saved game cannot be continued on this device"): the file is left as it was ([resume.bin](formats.md#resumebin)). |
| Playing | Back or Home | Paused | The pause menu (Resume, Leave) opens over the frame (on a cleared screen in the Play-again gap, see Paused -> Playing); the game gets no input or timers. |
| Playing | the status the game shipped is over | Over | `Session` has delivered `over` once; `ch.store` is flushed; `resume.bin` and its tmp are deleted (an over snapshot is never written, and one still pending deletes the file instead); the end-of-round menu (Play again, Leave) opens over the last frame. A round that ends while the pause menu is open deletes the file at once, from the pause menu's loop pass. |
| Paused | Resume, or Back | Playing | The frame is redrawn on a cleared screen with a full refresh; a timer that fell due meanwhile fires now. In the Play-again gap (Play again, then Back and Resume before the new round's first frame is published) nothing is redrawn: the pause menu stays on screen but is inert (its routing is closed, and `loopPlaying` drops every gesture), and an overlay closed in the gap likewise leaves its pixels on screen, until the new round's first frame is drawn on a cleared screen with a full refresh. A pause menu opened in the gap says so: its line "Starting the next round" sits under the headline, and the menu is drawn again without it once the round's first frame is published (`GameMatchActivity::gapWhenPaused`). The last round's board, which would take no taps, is never shown, under a pause menu opened in the gap either (in every mode: `GameMatchActivity::canvasUnderView` is false while `roundsStarted()` is below the count the match awaits): the menu sits on a cleared screen, and once the round's first frame is published the menu is drawn again over that frame. |
| Paused, Over | Leave | Leaving | See Leaving. |
| Over | Play again | Playing | The new round's snapshots are written to `resume.bin` again. The queued events are dropped; the VM cancels the pending timer and runs `Session::start()` and `draw()`; ver keeps counting (`GameScript::MatchRounds`). The end-of-round menu stays on screen until the new round's first frame is published (`GameVM::roundsStarted()` moves), so a frame from a step still running when Play again came is never shown. Gestures made until that frame is published, and then until the render task has drawn it and handed it to the panel (`displayBuffer` has returned, `GameMatchActivity::roundsDisplayed`), are read and dropped (`GameMatchActivity::loopPlaying`). |
| Playing, Paused, Over | a ScriptError or a stuck call | Error | The VM is stopped (a stuck one cancelled, then abandoned after 500 ms); the error view shows. |
| Error | Back | Leaving | See Leaving. |
| any but Leaving | forced exit (sleep, any Replace) | Leaving | See the forced exit. |

### Resume

A match with an installed package (`.pkg`) keeps `resume.bin` (docs/crosshatch/formats.md) so a solo or pass match
survives sleep; the save records the match's mode and, for pass, its seat count. The VM hands each snapshot it commits
to the loop task through a latest-wins mailbox (`GameVM::committed()`), and in Playing and Paused (and a hidden pass
match's Result and HandOff) every loop pass writes the newest one (a failed write is retried 5 s later). Leave and the
forced exit write the last pending one, once more, after the VM has stopped and before it is freed (a write that failed
less than 5 s before is not retried then either). Only Over deletes the file; Leave and the forced exit keep it.
Entering with `Start::Resume` restores it (`Session::restore`), so the match continues from the snapshot and does not
write it again until a move commits. The save is read before the VM is built, and its roster is the match's, whatever
roster the caller passed (Continue plays the save's roster): a hidden pass save resumes on the hand-off screen
(HandOff), which names the saved turn seat and whose button shows it; an open pass save resumes in Playing on that
seat's frame. When the load refuses the save, the match reads the file once more to learn why
(`GameSaveStore::peekResume`, through the store's buffer, which nothing references then, and leaving its roster). A save
of this package that this host cannot start (a mode or seat count it cannot start, or a later firmware's save) stops in
the error view with its own reason, and the file is kept. So does a save that will not read, on either read. The title
screen offers no Continue for such a save, but its New rows ask before a new match replaces it, as over any save;
formats.md has the states. A Continue that finds no save at all starts a new match with the roster Continue passed: the
title screen's first New row that can start (solo for a game that starts solo). A save records no settings, so a Play
again after Continue runs `setup` with the title screen's current choices as `ctx.settings` (api-level-1.txt), which
may differ from those of the saved round. The VM task never touches the card (AD-5).

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

Owner decision (R10, e6pre-3, 2026-10-04), option (b): the supervision above stays paused under an overlay in solo and
pass matches, and the nearby link does not depend on it for pumping or detection (the match's reaction to a lost peer still waits for `loop()`, below). What pauses under an overlay: the watchdog, the timer poll, and the
store flush. What must not pause: the ESP-NOW link pump (draining the receive ring, `ReliableLink` resends and acks) and
the peer-silence detection (a peer silent for 10 s). Both run on the `GameLink` task (spine AD-18; detection on that task is new to the spine), not in
`GameMatchActivity::loop()`, so an open overlay cannot stall them; no `ActivityManager` change and no ledger row. The
constraint on that task: it never takes `RenderLock` and never touches activity state (no `ActivityManager` call, no
view or match-state write); it reports a lost peer to the match through the session's queue, and the match acts on it
in its next `loop()`, that is when the overlay closes, so the move to `PeerGone` waits for the overlay while the link and the detection do not. The queue is depth-bounded, so epic-play-nearby states its overflow policy for a long overlay (a lost-peer event is never dropped). The link is not built yet (epic-play-nearby).

### Leaving

A user exit runs from `loop()`. It takes `RenderLock`, then cancels the VM and joins it for up to 500 ms or abandons it,
writing the last pending snapshot as `resume.bin` between the two unless the round is over. In a hidden pass match whose
panel holds a seat's frame (a pause menu over it), it then pushes the blank, a plain white screen
(`FrameReplay::drawBlank`), with a half refresh (logged "leave: blank screen pushed"). It releases the lock, flushes a
dirty `ch.store`, and calls `goToGames()`. The blank goes up before Games is asked for: if `goToGames()` runs out of
memory, the match stays current in Leaving with no VM, and the seat's frame must not stay on the panel. A Leave from the
end-of-round menu (over seat 0's frame, which is everyone's) or from the hand-off screen's pause menu (on no frame)
pushes nothing. When an abandon leaves the task alive (the simulator always does, since it cannot stop a thread), the
slot is still flushed, then leaked with the task when the match is destroyed: the slot's mutex is held only for a copy
inside a locked binding, which an abandon never deletes or leaves suspended, so the flush waits at most one copy and
saves every set made before the leak.

### The forced exit

`onExit()` without a user exit (sleep, any Replace) runs under the `RenderLock` that `ActivityManager` already holds and
never takes it again (pitfall 12cc816). It notes `millis()` at its very start, and the order is:

1. cancel the VM and join it (up to 500 ms);
2. the pending snapshot is written as `resume.bin` (in Playing, Paused, Result, or HandOff only), before an abandon can
   free the memory it lives in;
3. abandon the VM if it did not join (up to 500 ms more); a VM that ends within that wait has its last published
   snapshot written before it is deleted;
4. a `resume.bin` delete that Over could not finish is retried;
5. the `ch.store` flush;
6. in a hidden pass match only, and only while a seat's frame may be on the panel (`GameMatchActivity::panel` is
   `Seat`), the blank (`FrameReplay::drawBlank`: a plain white screen, no icon, no text, never the game's `handoff.bmp`
   or `title.bmp`) is drawn and pushed with a half refresh (`HalDisplay::HALF_REFRESH`) and logged ("forced exit: blank
   screen pushed"), whether the VM joined or not, so no seat's frame stays on the panel while the device sleeps, and the
   sleep screen the user chose in CrossPoint's settings, an overlay mode included, draws over white (AD-12, AD-20; the
   order is e6pre-10's, deferred-work.md `## 5.6` option (a)). The match is Leaving, so render draws nothing, and the
   render task is not inside `render()` while the lock is held, so the loop task may use `replay` and the framebuffer
   here. The panel is `Seat` in Playing, in Result (the mover's frame under the banner), in a menu over a seat's frame,
   and in any state entered from those whose own screen is not drawn yet (it still holds the frame): a pause menu not
   yet drawn, or an error view not yet drawn after a script error or a stuck call. It is not `Seat` on the hand-off
   screen or the blank, in a pause menu opened from the hand-off, in Over (the menu sits over seat 0's frame, which is
   everyone's), in a drawn error view, or after a Leave (which pushed its own blank over a seat's frame): there the exit
   pushes nothing. With no VM (a stuck call freed it on the way to Error) there is nothing to join and steps 2 and 3
   are empty, under the same rule. Solo and open pass matches push nothing. The half refresh is AD-12's choice, where
   the hand-off between seats uses a full one; whether it leaves a ghost of the seat's frame is checked on an X4 Pro
   (epic-pass-and-play entry 11).

The blank is last because it is the one step that costs a fixed 1,654 ms and has no SD work to lose: any SD step still
pending would otherwise start after it and be skipped. It is the one step that is never skipped: the deadline below
gates the SD steps (2 to 5) only, and a skipped or failed step does not stop step 6. What each guard protects: the
panel check keeps the half refresh (and its time) out of every state where nothing private shows; `hiddenPass` keeps it
out of solo and open pass, which show nothing private; the forced-exit flag keeps a user Leave's own blank (pushed after
its stop, in `leave()`) from being pushed twice; the steps that come before it keep their own guards (writable state,
ready store, pending snapshot, dirty slot, back-off, deadline).

Those are the only SD writes in `onExit()` (AD-17). Each of steps 2, 3's write, 4, and 5 starts only if less than
`FORCED_EXIT_DEADLINE_MS` (1,500 ms) has passed since the start of `onExit()`; a step that would start later is skipped
and logged with one `LOG_ERR` line naming it. Step 6 is no SD step and the deadline does not gate it. A resume write
that failed less than 5 s (`FLUSH_INTERVAL_MS`) before is not retried at the exit either. Leave has no deadline and follows the same order (its delete retry and store
flush come after it releases `RenderLock`).

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

**Worst case, with the blank last.** The timeline of a hidden pass match whose seat's frame is on the panel, in the
order of the steps (clock from the start of `onExit()`):

| Moment | Joined at once | VM that never stops |
| --- | --- | --- |
| join ends | about 5 ms | about 500 ms |
| resume write starts (allowed if under 1,500 ms) | about 5 ms | about 500 ms |
| abandon ends (the resume write is inside this span, before it) | none | about 1,030 ms |
| delete retry and store flush start (each allowed if under 1,500 ms) | right after the write | right after the abandon |
| blank starts | when the last SD step that started in time ends | the same |
| `onExit()` returns | blank start + the half refresh (1,654 ms on an X4 Pro) | the same |

The SD steps are bounded where they start, not where they end: the deadline stops a step from starting at 1,500 ms or
later, and a step that started at 1,499 ms runs at the card's speed (91 ms for a resume write on the X4 Pro, a
tmp write and rename). So the blank starts by `1,500 ms + the longest SD step that started in time` at the latest
(`GameMatchTest`'s `AStoreFlushStartedInsideTheDeadlineMayRunPastItAndTheBlankStillFollows` runs that with a 1,000 ms
flush) and, with the usual 1,030 ms for a stuck VM and the measured 91 ms resume write, about 100 ms in when the VM
joins and about 1,150 ms when it is stuck (an estimate from those figures, unmeasured as a whole), against about 5 ms and
about 500 ms before the reorder. The seat's frame therefore stays readable for the
SD steps' time longer than before, a few hundred milliseconds at the device's speeds and never more than the deadline
plus one step; the blank still always runs, and it finishes the way it did, one half refresh after it starts. Before
the reorder the blank's own 1,654 ms pushed every pending SD step past 1,500 ms and they were skipped, losing the last
move's `resume.bin` and the `ch.store` flush. In the other states nothing is pushed and the steps start at once. The owner accepted the longer exposure on 2026-10-04 (option (a)); if B7.6 shows a stall, the alternative is a start-time bound for the blank on its own thread or an async push. The
host double's push costs no clock, so `GameMatchTest` moves the clock by hand where it needs a step or the push to take
time. Measured on an X4 Pro before the blank existed, so without it: a forced exit with a stuck VM held `RenderLock` for
502 ms (epic-install-and-launcher entry 14). B7.6 (a sleep with a dirty store on the X4 Pro, `pass-store`) has not been
run on the device: the reorder is verified on the host only.

**Resume before store.** The resume write (step 2) comes before the `ch.store` flush (step 5), so a slow exit can skip
the store flush after the resume write ran: Continue may then resume a board newer than `ch.store` holds. Deferred
(retrospective rev-5); its trigger is a device log line `forced exit past 1500 ms; skipped the ch.store flush`. The
blank no longer counts toward that window.

## Hidden pass states

A pass match whose manifest says `hidden` (each seat's frame is its own: cards, a fleet) never shows one seat's frame to
another. `MatchLifecycle(true)` runs the hidden pass machine (`lib/GameCore/MatchLifecycle.h`), and the match builds its
`GameVM` with the hand-off flag, so the VM draws only the seat the match asks for (`GameVM::drawShown`, never
`GameCore::NO_SEAT`): nothing after a round begins, the turn seat after `GameVM::showTurnSeat()`, and the mover after a
move that passes the turn. Solo and open pass matches run the solo machine above and never reach these states.

| State | Event | Next | What the match does |
| --- | --- | --- | --- |
| Starting | the VM started | HandOff | The VM runs `setup` and draws nothing; the hand-off screen replaces the title screen once the VM has named the round's first turn seat (below). A resumed hidden pass save starts here too: the VM restores the snapshot instead of running `setup`, and the hand-off screen names the saved turn seat. |
| HandOff | a tap on "I'm ready", or Confirm (plain targets: one tap passes, even while the screen is being pushed; see Taps) | Playing | The match asks the VM for the turn seat (`showTurnSeat`, which the VM serves ahead of any queued event) and keeps the hand-off screen on the panel until that seat's frame is published: the loop asks for no render and drops gestures, and `renderCanvas` draws nothing, until `GameVM::seatShownRequest()` reaches the request (`seatAwaited`), then gestures until render has pushed that frame (`seatDisplayed`). The frame is drawn on a cleared screen with a full refresh. |
| Playing | a move that passes the turn | Result | The VM draws the mover's own frame again and counts the move (`GameVM::turnsPassed`, `passedTo`); the match shows that frame with the banner "Tap to pass to player N" (a fast refresh, no button hints). |
| Playing | the status the game shipped is over | Over | As in solo: a move that ends the round is RoundOver, never a turn change, and the end-of-round menu sits over seat 0's frame, the one for everyone. |
| Result | a tap on the banner, or Confirm (as above) | HandOff | The hand-off screen, naming the seat the move passed to. |
| Result, HandOff | Back or Home | Paused | The pause menu. From Result it sits over the mover's frame; from HandOff it sits on no frame (a cleared screen), since the device is between players. |
| Paused | Resume, or Back | Result, HandOff, or Playing | The state the menu was opened from: Result redraws the mover's frame and banner, HandOff the hand-off screen. |
| Result, HandOff | a ScriptError or a stuck call | Error | As in solo: the loop watches the VM there as in a menu. |
| Over | Play again | HandOff | The VM begins the new round (`setup`) and draws nothing; the hand-off screen shows once the VM has named the new round's first turn seat (the end-of-round menu stays until then), and its button shows that seat. The Play-again gap's rules hold on top of the seat gate: no frame of the last round is drawn after the hand-off screen. |

**The hand-off screen.** The runtime alone draws it, and no game command, laid out as the title screen (the owner's
hand-off redesign, 2026-10-02; `GameSplashLayout`): `FrameReplay::drawBlank` clears the screen, and the match draws no
header and no status strip; the title screen's 480 x 480 splash band, at the same place under where its header would be,
shows the game's `handoff.bmp`, else its `title.bmp` (each at most 480 x 480, centred and clipped in the band), else its
own icon at 128 px centred in the band (its `icon.bmp`, else the manifest's library icon, else `game-controller`, as on
its launcher row; `GamePicture`), drawn by the same call the title screen draws its splash with; then, where the title
screen has its first menu row, "Player N's turn" as plain text (no action, no frame), centred in the row, whichever
picture the band shows; and where it has its second two-line row (as with a save: Continue, then New game), the "I'm
ready" button filling the row, framed as Result's banner is. `GameSplashLayout::rowRect` measures both rows as
FreeInkUI's list lays out the title screen's two-line rows, and `ModePickerTest` pins it to the rows the title screen
draws; without a save the title screen's second row is Options, one line and shorter, which the hand-off screen does not
copy. The match reads the pages and the icon in `onEnter` on the loop task, after the save has set the roster, for a
hidden pass match only (`handoff.bmp`, and `title.bmp` only when there is no usable `handoff.bmp`; PSRAM, at most 28,862
B); a page that is missing is skipped silently, and one that will not read, is not the converter's 1-bit layout, or is
larger than 480 x 480 is skipped with a log line ("... skipped"). N is the seat the hand-off passes to
(`GameVM::passedTo`): after a move, the seat the move passed to; at a round's start (a new match, a resumed save, Play
again), the round's first turn seat, which the VM stores and counts once the round has begun
(`GameVM::turnAnnouncements`); seat 0 (a round already over when it began) is no player's turn, and the screen draws no
line for it. Until the count reaches the one the match awaits (1 for the match's first round, Play again's count plus
one after it), a render in HandOff pushes nothing, so the screen before stays (the title screen at a match's start, the
end-of-round menu after Play again, and a pause menu opened during the wait until the seat is named) and no tap or
Confirm passes it; the loop asks for the render once the count moves. The match pushes the screen with a full refresh
when it replaces anything else on the panel, so nothing of the last seat's frame stays there. A render in HandOff with
the hand-off screen already on the panel (after the light panel closes, or any repaint) pushes it again with a fast
refresh: the match records what its last push left on the panel (`GameMatchActivity::panel`: a seat's frame, no seat's
(the hand-off screen or the blank), or anything else), under `RenderLock`.

A forced exit (sleep, any Replace) pushes the blank, plain white, with a half refresh, only while a seat's frame may be
on the panel (`panel` is `Seat`), with or without a VM, after the SD steps: see The forced exit, step 6; from the hand-off screen, the
blank, Over, or a drawn error view it pushes nothing. So does a Leave from a seat's frame: see Leaving.

**Taps.** Result's banner and the hand-off screen's "I'm ready" button are each their screen's one tap target (an
`fui::button` with `ACTION_PASS`, routed by `routeTouch`), plain ones like the title screen's: a tap on it, or Confirm
(the front button, the home key's action, the X4 Pro's power click), passes the device on on release (a tap) or on Confirm's release, even while
its screen is being pushed (a full refresh takes 1.7 s on the X4 Pro, and every tap made during one used to be dropped,
which cost several taps per step). A tap anywhere else (the band, the turn line) does nothing. The routing is published
before the push, and `handle()` closes it on every transition, so a tap in the instant between `handle()` and the new
screen's `renderUi` routes nothing. Confirm is not gated by routing: it passes at once on release once the state is
Result, or HandOff with its turn seat named (`loopHandOff`'s `named`); before the VM has named the seat (no screen is
pushed until then) neither a tap nor Confirm passes. The banner is a framed panel at the bottom of the screen and the button fills the title
screen's second two-line row, and on the X4 Pro the two overlap (the row at y = 648 to 722, the banner at y = 649 to 780),
which the layout keeps: the owner accepts (Decision, 2026-10-03, superseding the 2026-10-02 time guard) that a stray
second tap on the banner's spot can pass the hand-off, and that a stray second tap on "I'm ready" can become a move.

The first move after "I'm ready" is taken during its frame's push too, after every HandOff to Playing (a hidden pass's
Play again included; solo and open-pass Play again have no hand-off screen and still drop a touch during the first
frame's push). `handle()` sets `firstFramePushing` on the
HandOff to Playing transition, and `renderCanvas` clears it once that frame is on the panel (and any other transition
clears it). While it is set and the VM has published the frame, `loopPlaying` lets a touch whose touch-down came at or
after the transition (`playingSinceMs`; a tap's touch-down is `millis()` less `HalGPIO::lastTouchHeldMs()`, a held
contact's `touchDownMs`, the pass that latched it) through as that seat's move: it skips `touchDownFrame` and `frameAt`
and is posted with `GameVM::frameGen()`, so `GameVM::madeUnderAnotherSeat` keeps it. A contact that began before the
transition (the press that passed the hand-off, lifting late) is dropped and logged. Every other touch follows the path
under Moves below: an open-pass touch begun under one seat's frame and lifted under another's never reaches the second
seat, and a solo or Play-again touch during a first frame's push is dropped.

**Moves in Result.** A tap the mover made right after its turn-passing move, queued behind it, still reaches the mover's
own `input` (its `ui` may change, and the match redraws the banner over the new frame), and the move it returns is
discarded by the Session, since the mover is no longer the turn seat. While the match is in Result or HandOff, and until
the next seat's frame is on the panel, it posts no game input, so before the VM draws the next seat it plays whatever is
still queued under the view it was meant for (the mover, or on the hand-off screen nobody): no queued tap reaches the
next seat. The VM's view may lag the match: such a tap may be played, and the mover's frame published again, after the
match has moved on to the hand-off screen; HandOff draws no canvas, and neither does a pause menu opened from it, so
that frame never reaches the panel.

**A touch made under another seat's frame.** Any pass match, open or hidden: the loop posts each touch with the frame
the panel showed when it was made (`GameMatchActivity::frameDisplayed`, through `GameVM::postInput`, in the event's
`serial`, which only a timer reads). `renderCanvas` stores the number of the frame `GameVM::drawFront` took, read under
the frame mutex, so a frame the VM published while render ran is named when it is the one drawn. The VM notes the first
frame of each seat it draws, and drops a touch made before the first frame of the seat it draws now, comparing the two
numbers so that a wrapped count still orders them: one made under seat 1's frame and still queued when seat 1's move
passes the turn never becomes seat 2's move (open pass), and one queued behind the move that ends the round never
reaches seat 0, which is a frame, never an input seat. The mover's own late tap in Result was made under the mover's
frame and goes on, as above. A timer is never dropped for this: it goes to the turn seat (R11), but one that falls due
after the round is over is dropped, never delivered, in any mode (e6pre-2, below). A touch posted with `GameVM::UNTAGGED` (a direct
caller) is never dropped; the loop never posts that value, posting one less instead, which can only drop (a real frame
number reaches it only after 2^32 publishes). The tag is the
frame on the panel at the first Playing pass that sees the finger down (`GameMatchActivity::touchDownFrame`, from
`isScreenTouchHeld`, true from the contact's first sample), freed on the first Playing pass with no finger down (the
lift, a long press that suppressed the rest of the contact, or a contact that ended where the loop did not read it, such
as in Result or under the light panel). A contact the loop never saw down is tagged when its lift is read. A tap also
carries its touch-only held time (`HalGPIO::lastTouchHeldMs()`; `MappedInputManager::getHeldTime()` would answer a
button's hold on a pass with a button edge), so it is back-dated to its first touch sample against the last canvas push
(`frameAt`: render notes when `displayBuffer` returned, which it does once the panel's refresh has completed, and the
frame before it), and the older of the two frames is posted. Each early return in `loopPlaying` (Back, a VM check, a
round or turn change) leaves Playing, so what this covers is the same-pass race: a tap whose first sample came in a
pass's `update()` before the next seat's push completed, and whose latch was taken in the same pass after it. Touch is
sampled only in `update()` on the loop task, once a pass, so a contact's first sample is the first update after it
lands. A contact made under one seat's frame therefore never reaches the next seat when a Playing pass latched it before
the next seat's push completed, or when it is a tap sampled before that push completed. What remains:

- a touch whose finger came down while the loop task was blocked in an SD step after a move (the resume write, the
  `ch.store` flush, or the delete retry), during which the next seat's push completed, is first sampled after both and
  may reach that seat, whether a tap, a swipe, or a long press (closing it needs touch sampled off the loop task, in
  upstream input code; the device run, epic-pass-and-play entry 11, measures it);
- a swipe or long press whose first sample came in a pass's `update()` before the next seat's push completed, but whose
  latch was taken in the same pass after it, carries the next seat's frame and may reach that seat (only a tap carries
  a held time; back-dating the latch itself is deferred, deferred-work.md `## e5-xr`);
- the back-dating anchors to the loop's read, a few milliseconds after the release sample, whose time is not exposed;
- a finger already down when a Playing pass first runs after another state keeps the latch of an earlier contact not yet
  freed, so its touch is dropped (fails closed);
- a tap after "I'm ready" but before the VM publishes the next seat's frame (`GameMatchActivity`'s `awaitingRound` is
  still true) is dropped, with the log line "dropped a touch before the frame was on the panel": the first-move-accepted
  rule above covers only the window after the VM publishes the frame. The owner decided on 2026-10-03 to keep dropping
  it; that log line records how often it happens on a device (the same line also covers any other touch made while the frame is not yet on the panel and the first-move rule does not apply);
- as on the canvas (the first item), on the Result banner and the hand-off screen's button: a touch that lands and
  lifts while the loop task is blocked in an SD step is first sampled after it, so it reads as a fresh tap and passes
  the screen.
- the forced exit's blank takes about 1,654 ms on the X4 Pro (the owner's device log, 2026-10-03) and now comes after the
  resume write, the delete retry, and the store flush (e6pre-10, deferred-work.md `## 5.6` option (a)), so it no longer
  costs them their 1,500 ms window; a seat's frame stays on the panel for those steps' time first (see The forced exit,
  the worst case). B7.6 (a sleep with a dirty store, `pass-store`) has not been run on the device; the order is verified
  on the host only.

**Timers.** A timer that falls due in Result or HandOff (polled by the loop there, or already queued) is held by the VM and
delivered to the next seat right after its first frame; one the game re-armed or cancelled meanwhile is dropped as stale,
as anywhere else. Play again drops a held timer with the last round.

**No timer after the round is over.** A timer that falls due once the round is over (`status.over`) is never delivered to
`input`, in any mode: solo, open or hidden pass, and later nearby. It does not matter which seat the device shows (solo
and nearby show their one local seat, pass shows seat 0) or whether the roster has a local seat at all (once over); the test is
`status.over`, not a seat number (`GameScript::MatchRounds::lateTimer`, called by `GameVM::run` and `GameVM::stepHandOff`,
and by `MatchRounds::step` and `play`). The VM logs `Dropped a timer due after the round was over` at debug level, once,
and draws nothing (a timer held in Result or HandOff is dropped, and logged, when it would have been delivered to the next seat, or silently when Play again clears it); a stale timer (re-armed or cancelled by the game) is dropped before that, silently. A timer that fell
due before the round ended is delivered as always. A game may still cancel its timer in its `over` handler (the
fixtures do); it no longer has to.

**Saves.** A hidden pass match keeps `resume.bin` as a solo match does (Resume, above): each committed snapshot is
written in Playing, Paused, Result, and HandOff, Over deletes it, and Leave and the forced exit keep it. A move that passes the
turn is written on the loop pass that handles it (not the next one). The forced exit's blank, when it is pushed, comes
after the resume write (step 2, then step 6).

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
| Result banner (hidden pass) | -- | -- | "Tap to pass to player N" in a framed panel at the bottom, over the mover's frame | a tap on the banner | Confirm passes; Back pauses; no hints |
| Hand-off screen (hidden pass) | the title screen's splash band: the game's `handoff.bmp`, else its `title.bmp`, else its own icon at 128 px (no library view icon) | -- | "Player N's turn", plain text in the title screen's first menu row | "I'm ready", filling its second two-line row (as with a save) | Confirm passes; Back pauses; no hints |

The pause menu opened in the Play-again gap adds the line "Starting the next round" under its headline, centred like the
caption and the headline (see Paused -> Playing above). It sits on a cleared screen, with no frame under it, until the
round's first frame is published.

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
