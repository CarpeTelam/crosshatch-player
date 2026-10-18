# Story dlab-c4: hold for photos (lane C, epic branch)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R3, R4,
Boundaries, Decisions D4, D5, D11, D12 and D13; the Story results for dlab-c2 and dlab-c3). Ref: `dlab-c4`. Plan:
`_bmad-output/implementation-artifacts/plan-dlab-c4-hold-for-photos.md`. Builds on `dlab-c3` (`TransitionScene`'s
modes and `slotDone`, `TransitionTestActivity`'s rows and sweep task, `SweepControl::holdRun`) and on the
orchestrator's baseline commit that holds Static at every settle unconditionally (the commit before this story).

## Intent

**Problem:** The owner photographs the panel at specific moments of a run (D13): Static's settled patterns, Slow's
painted bands and its rectangles after a full dwell, Fast's end-of-sweep pause. Catching them by hand means pressing
pause within half a slot of the moment. The baseline commit holds Static at every settle, always; the other modes
have no hold, and nothing on the panel says the test is holding rather than running or stuck.

**Approach:** Three additions to the transition test, all host-tested in `lib/GameDisplayLab` where they are pure:
1. **A Hold row** on the test screen (after Mode, before Clean), on by default, a tap or Confirm toggles On/Off; a
   lab preference, not a preset value (not saved to the file). Off means the run never holds itself (the baseline
   Static hold becomes conditional).
2. **Photo spots per mode**, decided by `TransitionScene` (a pure "hold now?" in `slotDone`'s result): Static: every
   settle (as the baseline). Slow: when the paint has settled (bands, before the rectangles enter); when the
   rectangles are fully on-screen and every pixel under them has had the preset's longest run (`longestRunSlots`
   slots since their trailing edge entered); at the end of the crossing, rectangles off the right edge, trails
   behind them. Fast: at the end of each sweep, in its pause. Each hold logs `gray hold: <spot>` and the Controls row
   says Next resumes.
3. **A hold marker**: a 32 px dash in the bottom-right corner of the logical screen, in the bottom band's margin below
   its last lane (the review found a 16 px square shared rows with that lane's rectangle), retargeted to the opposite
   shade of the band it sits in (`(band + 2) mod 4`) when a hold begins, driven by the engine's own slots (the task
   runs slots until the marker settles, then holds); on resume it is retargeted back to the band shade and settles
   before the run continues, so it never contaminates a flip or a photo of the bands. The marker's pixels are
   excluded from Static's settle detection and from Slow's measurement (they are the scene's, not the pattern's).
   The marker is drawn only while Hold is on.

## Boundaries & Constraints

**Always:** no drawing through the renderer while the view is up (D4: after lab scans the driver's next ordinary
refresh is a full clear, so the marker must be scan-driven); every scan still goes through `DisplayLabActivity::scan`
on the sweep task; `SweepControl`'s protocol unchanged beyond `holdRun`; the hold and marker decisions pure and
tested (`TransitionScene`): which slot holds, per mode; the marker's rect per orientation; that a resume drives the
marker back before the pattern moves; D5 unchanged; locals under 256 B; `tr(STR_…)` keys in `english.yaml` only; C3
`default` and `sticky` still build; the flash-budget gate (D6) unchanged.

**Never:** no change to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`;
no new ledger row; no edit of generated I18n files; no change to the stock bench, the frame-period or polarity
screens, or the Tuning screen; no change to the preset file format.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Hold row | tap / Confirm on the rows | toggles On/Off; the row shows the state; default On each visit | — |
| Static hold | Hold on, a settle | marker driven to the opposite shade, then the task holds; `gray hold: static settle N` | Hold off: the flip follows at once (dlab-c3 behaviour before the baseline) |
| Slow holds | Hold on | three holds per crossing: bands settled; rectangles fully dwelt; crossing ended | Hold off: none |
| Fast hold | Hold on | one hold per sweep, in the pause | Hold off: none |
| Resume | Next or Confirm while holding | marker driven back to the band shade, settles, then the run continues (the flip, the crossing, the next sweep) | — |
| Stop while holding | Previous or Back | the view ends with a FULL refresh, marker and all | — |
| Marker placement | any orientation | bottom-right of the logical screen, in the bottom band's margin below its last lane, 32 px wide | — |
| Session cap during a hold's marker slots | cap reached | the cap clean runs as today; the hold is logged as restarted | — |

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`; new tests for the hold decisions, the
  marker rect and the resume sequence.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`,
  `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro`; the flash-budget gate with D6 (expect +0).
- Hardware (owner, experimental branch): the marker appears within about a second of a settle and is gone when the
  flip starts; the three Slow holds and the Fast hold land where described.
