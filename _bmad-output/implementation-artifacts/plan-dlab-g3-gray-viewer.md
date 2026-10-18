---
title: 'dlab-g3: the gray viewer (a lab screen painting a PNG or JPEG, or a built-in test chart, through the gray engine)'
type: 'feature'
ticket: 'story-dlab-g3-gray-viewer'
created: '2026-10-18'
baseline_revision: 'c50e6dd6808a0147d75f2befb5dd76794e0bfff3'
status: 'in-review'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/story-dlab-g3-gray-viewer.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md'
  - '{project-root}/docs/crosshatch/display-lab-images/README.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/refactor-for-review/SKILL.md'
warnings: ['oversized']
deferred:
  - summary: "the shared runner's slot sequencing has no host test; a pure sequencer with an injected scan and clock would pin it"
    evidence: 'review pass 2026-10-18, verification-gap 1: serve, runSlot and the Client hooks order are checked by review and a syntax-only compile; only their pure parts (SweepControl, SlotScheduler, the scenes) are host-tested'
    location: 'src/activities/games/display_lab/GrayRunner.cpp'
    severity: 'low'
---

<intent-contract>

## Intent

**Problem:** The owner wants to see the engine's shades on real artwork, line art first (D16). Today the lab paints
only bands and rectangles.

**Approach:** Add a **Gray viewer** screen to the lab menu, after the transition test. It paints a picture through the
gray engine:

- **Sources:** the built-in test chart first, then the PNG and JPEG files in `/.crosshatch/display-lab/images/`.
- **Decode:** files go through a fork-side gray-row reader that drives PNGdec and JPEGDEC directly (D16's amendment,
  option B). It scales each picture to fit with white letterbox and feeds 8-bit rows to a pure quantiser.
- **Dither:** Plain (nearest level) or Ordered (Bayer, scaled to `SHADE_COUNT`).
- **Paint:** one `retargetRect` per run of equal targets per row; the paint settles, then holds with the pause marker.
- **Shared runner:** the transition test's sweep runner is extracted into a `GrayRunner` that both screens use, with
  the transition test's behaviour unchanged.

## Boundaries & Constraints

**Always:**
- Every scan goes through `DisplayLabActivity::scan` on the sweep task, and D4 and D5 are unchanged.
- The renderer's framebuffer is never drawn while the view is up. The only renderer call is the lab's existing
  clean-to-white.
- The level count comes from `SHADE_COUNT` and the names from `shadeName`. White is `SHADE_COUNT - 1` and black is 0
  in the new code, never `Shade::W` and never 4 written out as a number.
- Memory:
  - The model stays 3 B a pixel.
  - No second full-screen buffer: decoding writes straight into the model on the sweep task, after the clean.
  - Locals stay under 256 B.
  - Fallible allocations use `makeUniqueNoThrow` or `new (std::nothrow)`.
- Strings: `tr(STR_GAMES_DLAB_…)` keys go in `english.yaml` only.
- The reader's row conversion is faithful to the converters: PNG gray, truecolor, indexed (with tRNS alpha), gray and
  alpha, truecolor and alpha, at 1/2/4/8-bit depth where the converters accept it; alpha composited on white; JPEG
  decoded as 8-bit gray at JPEGDEC's coarse scale (progressive forced to an eighth); the converters' heap floors and
  size limits.
- A decode failure ends the run and puts the reason on the Status row. The chart and the other entries still work.
- The transition test's logs, rows and behaviour are unchanged after the extraction (Design Notes lists each guard
  that moved).

**Never:**
- No change to:
  - `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`;
  - the decoders or `DitherUtils.h`;
  - `GrayPreset`, the preset file or the Tuning screen;
  - the stock bench, or the frame-period and polarity screens.
- No new ledger row, and no edit of generated I18n files.
- No PNGdec or JPEGDEC include outside `GrayImageReader.cpp`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| No folder | images folder absent or empty | Image row offers the chart only (`1/1`) | logged at onEnter |
| Files present | PNG/JPEG plus other files, dot files, folders | files listed after the chart, sorted case-insensitively by name; others ignored; at most `MAX_IMAGES`, the first by name whatever the directory order | names over `NAME_BYTES - 1` skipped, logged; files past the cap logged |
| Run | Run row | clean, paint (chart or decode), settle, marker out (0 or 6, whichever is farther from the mean level under the bars), hold; logs `gray viewer: <name> painted after N slots, M pixels per level [...]` | a non-Done scan ends the run as in the transition test |
| Next/Confirm while painting | the slots or the marker in flight | pause or resume (`SweepControl::press`, one compare-exchange); ignored while the run's first picture or a next one is being cleaned and read; a resume then a pause before the task serves them keeps a hold (the task, not the press, ends it) | a press racing the hold is never lost |
| Next/Confirm while holding | held | the next entry: clean, paint, settle, hold; after the last entry, the chart; decided in the same compare-exchange as the press (Held cleared; Replay, Run and Replaying set), further presses ignored until that picture is painted | a resume that ever reaches a held picture holds again |
| Previous/Back | view up | stop, rows back with a FULL refresh, Status "Stopped" | — |
| Stop past its bound | a task that has not ended 20 s after a stop (a decoder that never returns, a scan in the SDK's 30 s BUSY wait) | Back: logged, task left undeleted, Status (transition test: Run) "Still stopping", no end read until `reap()` joins it; leaving the screen or sleep: logged once naming the screen, the runner detached to the task (`endLocked`), which frees it and deletes itself when it ends; Run on either screen shows "Still stopping" while a task is undeleted or detached | a stopped run never holds off auto-sleep; a detached task touches only its runner (inert client, sticky stop) |
| Unreadable file | open (missing, unreadable, a read that fails, a corrupt header), format (limits, depth, a colour type that does not exist, an interlaced or 16-bit PNG, an extended or lossless JPEG), memory or decode error | run ends; Status row names the reason; logged; the file handle closed on every path | the Image row still steps; the chart still works; a stop during the read or the walk is no error (Stopped) |
| Truncated or malformed file | a PNG: a chunk past the end (IEND's included, and 8 or more bytes after IEND that are not whole chunks), a bad signature or chunk length, a PLTE over 768 bytes or not whole triples, a tRNS over 256, over 65,536 chunks; a JPEG: a marker or segment before SOS past the end, a bad SOI or segment length, a SOF0/SOF2 of 0 or over 4 components or a wrong length, a DQT not whole tables; no IEND or SOS; under 8 (PNG) or 4 (JPEG) bytes | refused before the decoder reads it (`walkPngChunks`, `walkJpegSegments`): "format or size not supported", "The picture file has no end: it may be cut short", "The picture file is too short to be a picture" | the decoder is given the walked size; reads give nothing past it or once stopped (PNGdec: the rest 0xFF; JPEGDEC: nothing); a stop waits at most 20 s (row above) |
| Dither | Dither row | Plain ↔ Ordered for the next Run | — |
| Fit | any size or aspect | scaled to fit, aspect kept, centred, white letterbox; bilinear along an axis it enlarges (the previous PNG row, the previous JPEG strip's last row and the previous block's last column kept by `SeamKeeper`, so blends cross seams), nearest at pixel centres along one it shrinks; a JPEG's EXIF orientation ignored, as the converters ignore it | dimensions outside the converters' limits: format error |
| Levels | any engine | readout `N levels: 0, 2, 4, 6` from `SHADE_COUNT`/`shadeName` | — |
| Session cap mid-paint | cap reached | the runner's cap clean; the hold in progress dropped; the picture repainted (file decoded again); logged | repaint failure ends the run as above |
| Cover, sleep, Home | as the transition test | identical (the shared runner) | — |

</intent-contract>

## Code Map

- `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}`: the runner to extract. It keeps the rows, the
  scene, the Ladder stage stats (`ladderPauses…`, `logLadder*`) and every scene log line, and implements
  `GrayRunner::Client`.
- `lib/GameDisplayLab/TransitionScene.{h,cpp}`: `PanelTurn`, `ScreenRect`, `toPanel`, the file-local `rotate`, the
  private `retargetLogical` and the marker bars and settle check move to `PanelGeometry`, and the scene delegates to
  it. Public API and constants are unchanged, so `test_transition_scene.cpp` stays untouched.
- `lib/GameDisplayLab/GraySlot.h` (`runGraySlot`), `SweepControl.h`, `SlotScheduler.h`, `GrayModel.h`
  (`retargetRect`, `level`, `target`, `remaining`, `LEVEL_STEP`): reused as they are.
- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}`: `Row` gains `GrayViewer` after `TransitionTest`, so
  `ROW_COUNT` becomes 7; `openScreen<GrayViewerActivity>`. The stock menu is unchanged.
- `lib/Epub/Epub/converters/{Png,Jpeg}ToFramebufferConverter.cpp`: read-only references for the reader. From PNG:
  `convertLineToGray`, `readPackedSample`, `expandSampleToByte`, `isSupportedBitDepth`,
  `requiredPngInternalBufferBytes`, the file callbacks, `MIN_FREE_HEAP_FOR_PNG`. From JPEG: `chooseJpegScale`, the
  read and seek callbacks with `iPos`, the progressive handling, `EIGHT_BIT_GRAYSCALE`, `MIN_FREE_HEAP_FOR_JPEG`.
  Limits from `ImageToFramebufferDecoder.h`: `MAX_SOURCE_DIMENSION` 32767 and `MAX_SOURCE_PIXELS` 8 MP.
  `PNG_MAX_BUFFERED_PIXELS` is 16416 (`platformio.ini`).
- `scripts/check_layers.py` and `check_layers_test.py`: `classify_external` learns `PNGdec.h` and `JPEGDEC.h` as
  `libdep:PNGdec` and `libdep:JPEGDEC`. `FILE_EDGES` gives only `GrayImageReader.cpp` those two edges. One test passes
  the reader and fails another screen file.
- `lib/I18n/translations/english.yaml`: the new `STR_GAMES_DLAB_*` keys sit beside the existing ones.
- `test/game_display_lab/test_image_decoders.cpp`, `image_fixtures.h` and `CMakeLists.txt` (third batch): the real
  decoders on the host behind the read answers, built only when `CROSSHATCH_PNGDEC_DIR` and `CROSSHATCH_JPEGDEC_DIR`
  hold their sources (PlatformIO's library folder by default).
- `docs/crosshatch/display-lab.md`: a Gray viewer section and code map entries.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameDisplayLab/PanelGeometry.{h,cpp}`
  - Move `PanelTurn`, `ScreenRect` and `toPanel` here.
  - Add `retargetLogicalRect` (clip to the logical screen, map, `retargetRect`).
  - Add `pauseBars(box, gap)` and `barsSettled(model, bars, turn, panelW, panelH)`.
  - `TransitionScene` includes this file and delegates its `retargetLogical`, `markerBars` and `markerSettled` here.
  - Why: the marker and the logical-to-panel paint are shared, not copied.
- [ ] `lib/GameDisplayLab/ImageRows.{h,cpp}`
  - Pure helpers:
    - `ImageKind imageKindOf(name)`, `nameLess`, `imageSizeAccepted`;
    - `PngPixel`, with the same values as PNGdec;
    - `pngDepthSupported`, `pngInternalBufferBytes`, `pngRowToGray` (composited on white);
    - `jpegScaleDenominator(targetScale×1000)`;
    - `FitRect fitToScreen`;
    - `FitAxis` (`srcOf`, `firstDst`).
  - Why: the device reader's arithmetic is host-tested.
- [ ] `lib/GameDisplayLab/GrayPicture.{h,cpp}`
  - Pure helpers:
    - `Dither`, `levelGray`, `quantise(gray, x, y, dither)`;
    - `testChartGray(x, y, w, h)`;
    - `forEachRun`;
    - `levelCounts(model)` (targets per level).
  - The `GrayPicture` scene:
    - `paintGrayRow` (quantise, split into runs, `retargetLogicalRect`) and `paintChart`;
    - `restart` (a run's start, Next, a session-cap clean: the marker dropped), `slotDone` (Painting → MarkerOut → Held);
    - `markerRect` (32×16 at the bottom-right), `markerBars`, `markerShade`.
  - Why: the story's pure parts.
- [ ] `test/game_display_lab/test_gray_picture.cpp` and `test_image_rows.cpp` (registered in `CMakeLists.txt` with
  the new sources).
  - Quantiser: plain and ordered at 4 and at 7 levels (the formula takes the level count as a parameter), extremes,
    monotonicity, ordered averages.
  - Chart: deterministic, every level present under both dithers.
  - Fit and `FitAxis`: partition and centring.
  - Runs; the PNG row conversion for each pixel type and depth; kinds; size limits; the JPEG scale.
  - Painted rows: targets in all four turns. The hold sequence and marker shade, and `levelCounts`.
- [ ] `src/activities/games/display_lab/GrayRunner.{h,cpp}`
  - Move from the transition test, with the code unchanged except for client calls:
    - the PSRAM working set and `allocate(who)`;
    - the task (create, entry, run loop, stop joined, stop locked), `SweepControl`, the heartbeat and covered check,
      `preventAutoSleep`;
    - serve (pause, replay, clean, resume), `cleanToWhite`, `admitted`, the clock;
    - `runSlot` (the cap clean, the scan lambda with its refusal checks and headroom, `runGraySlot`, the scheduler);
    - the stats and the stack log;
    - `holdForPhoto()`.
  - `Client` hooks: `onReplay`, `onPaused`, `onResume`, `onCapClean`, `prepareSlot`, `slotPreset`, `afterSlot`,
    `clockStill`.
  - `End` gains `PaintFailed`. A replay clears `heldForPhoto`.
  - The stack size is a constructor argument: 8192 for the transition test, 12288 for the viewer.
- [ ] `src/activities/games/display_lab/TransitionTestActivity.{h,cpp}`
  - Becomes the runner's client and keeps every log line and row.
  - Why: no copied runner.
- [ ] `src/activities/games/display_lab/GrayImageReader.{h,cpp}`
  - `readGrayImage(path, kind, screenW, screenH, GrayRowSink&)` drives PNGdec or JPEGDEC with HalFile callbacks (as
    the converters do) and emits fitted 8-bit row segments.
  - It yields one tick every 250 ms and aborts when `sink.stopped()`.
  - Errors: Open, Format, Memory, Decode, Stopped.
- [ ] `src/activities/games/display_lab/GrayViewerActivity.{h,cpp}`
  - Rows: Image, Dither, Run, Levels, Controls, Status.
  - At onEnter: the file list (heap, `MAX_IMAGES` 32 × `NAME_BYTES` 64), the runner allocation and the two row
    scratches (logical width each).
  - Implements the runner's `Client`. The current entry is an atomic index the loop task advances.
  - Input: Next or Confirm is one `GrayRunner::press(true)`: while holding the next entry (`SweepControl::press`'s
    Next, a replay from the hold), ignored while a replay is pending, and a pause or resume otherwise;
    Previous or Back stops.
- [ ] `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}`: the menu row.
- [ ] `lib/I18n/translations/english.yaml`:
  - `GRAY_VIEWER`, `GRAY_VIEWER_DESC`, `IMAGE`, `CHART`, `IMAGE_OF` (`%s (%u of %u)`), `DITHER`, `DITHER_PLAIN`,
    `DITHER_ORDERED`, `VIEWER_RUN_DESC`, `LEVELS`, `LEVELS_FMT` (`%u levels: %s`), `VIEWER_CONTROLS`, `STATUS`;
  - `IMAGE_OPEN_FAILED`, `IMAGE_FORMAT`, `IMAGE_NO_MEMORY`, `IMAGE_DECODE_FAILED`.
- [ ] `scripts/check_layers.py` and `check_layers_test.py`: the vendor headers and the reader's FILE_EDGES (D16).
- [ ] `docs/crosshatch/display-lab.md`: the Gray viewer section (rows, controls, quantiser, chart layout, the reader,
  logs, memory) and the code map.

**Acceptance Criteria:**
- Given the lab menu, when it opens, then it lists Frame period, Polarity, Transition test, Gray viewer, Tuning, Save
  preset, Exit.
- Given the viewer with the chart, when Run is pressed, then the panel is cleaned and the chart painted and held, and
  the log line names the chart, the slots and `SHADE_COUNT` per-level counts.
- Given a held picture, when Next is pressed, then the next entry is cleaned and painted, and the last entry wraps to
  the chart.
- Given the extraction, when the host suite runs, then every pre-existing transition-scene, sweep-control and
  scheduler test passes unchanged.
- Given `SHADE_COUNT` 7 (the formula with N = 7 in tests), when a gradient is quantised, then all seven levels appear,
  and plain gives the nearest.

## Design Notes

- **Why a runner class with a client interface, not a base activity:** the two screens differ in rows and in their
  scene, and they share the task, the protocol, the clock, the scan and the stats. Composition keeps the transition
  test's scene logic (Static flips, Slow measurement, Ladder stage stats) in the transition test, while the runner
  owns everything that touches the task or the panel. The client's `afterSlot` calls `runner.holdForPhoto()` at the
  point where the old `afterSlot` held, so the log order stays the same.
- **What each moved guard protects:**
  - `startRun`'s `if (sweepTask) return`: no second task. It stays in both screens' `startRun`, and since the review
    the runner itself refuses `prepare`, `launch` and `release` (logged) while a task is up, so no caller can reset or
    free what a live task uses.
  - `stopSweep`: polls the task's end before `vTaskDelete`, so the task is never deleted mid-scan (since the third
    batch the end is `taskState`, Running to Done in one exchange against `endLocked`'s Running to Detached).
  - `endSweepLocked`'s delete only while `insidePanel()` and `eBlocked`: the onExit and sleep deadlock on the render
    mutex (12cc816).
  - The task never deletes itself while the screen owns it: no notification ever reaches a freed task. Since the third
    batch it does delete itself once detached, when the screen has given it up and neither notifies nor deletes it.
  - The scan lambda checks `stopRequested` first, so a stop always reads as Stopped. Its refusal checks run outside
    the panel, so no log call happens inside a region where the task may be deleted.
  - `admitted`: the cover and stop gate before every scan and clean.
  - The session-cap clean happens before a slot, never between its two scans.
  - `sweeping = false` after an already-reported cap clean, so the empty window is not reported.
  - `heldForPhoto` was stored before `holdRun`, so `preventAutoSleep` never saw neither. Since the review the hold is
    the request word's Held bit, set with Run cleared in one compare-exchange after the hold's start is stored, and
    `preventAutoSleep` reads both bits from one load: the same guarantee, with no window between them.
  - `reportStats` keeps the last real speed.
  - Ordering comments (memory orders, `RenderLock` placement) move with their code.
- **The viewer's marker:** a pause symbol 32×16 at the logical bottom-right, driven to 0 or `SHADE_COUNT - 1`,
  whichever is farther from the mean model level under its bars (a tie goes to 0). It is not driven back, because
  Next cleans first. The bars and the settle check are `PanelGeometry`'s, shared with the transition test.
- **Quantiser:** N = `SHADE_COUNT`. Plain is `(g·(N−1) + 127) / 255`. Ordered is
  `(g·(N−1)·32 + (2b+1)·255) / (255·32)`, clamped to N−1, where b is the Bayer 4×4 entry at the logical (x, y).
  Ordered keeps the exact level grays (`levelGray(s) = round(s·255/(N−1))`) on their level, because the rounding
  error, at most (N−1)/2 out of 255·32, stays inside every threshold.
- **The chart** (logical w×h, top to bottom, in fifths): the level ramp (N steps of `levelGray`); the continuous
  gradient 0..255; hatching at pitches 3, 6 and 12 (thirds of the width, 1 px diagonals); concentric 2 px rings every
  16 px in the left half; diagonal 45° strokes of weight 1, 2, 4 and 8 px in the right half.
- **Decode on the sweep task, straight into the model, after the clean:** this needs no second picture buffer. A
  session-cap clean repaints by decoding again.
- **Reader stack:** the decoders run on the stock 8 KiB loop stack on non-vector-font boards. The decode chain and
  the scan chain never nest, so the need is the larger of the two (the transition test's 8 KiB scan provision, or
  the decode chain), not their sum; the viewer's task gets 12 KiB, provisional until the measured high-water marks
  (a PNG, a baseline and a progressive JPEG) the story's Verification asks for.
- **Scaling (revised at review and follow-up):** bilinear along an axis the fit enlarges (centre-aligned taps per
  axis; only the 16.16 blend arithmetic, horizontal then vertical, is the JPEG converter's, which aligns corners and
  blends only when both axes enlarge), so a progressive JPEG decoded at an eighth is not blocky; nearest at pixel
  centres (`srcOf(d) = ⌊(2d+1)·S/(2D)⌋`) along an axis it shrinks, so line art stays crisp. Which output pixels a
  decoded unit makes: those whose last needed source pixel is in it (`outputsEndingIn`; a PNG row, `rowsReadyAt`),
  with the source pixel before it kept (`SourceBlock`'s `above`: the previous PNG row, or the previous JPEG strip's
  last row across the whole scaled width; `left`: the previous block's last column in the same strip), so blends
  cross strip and block seams exactly as over the whole image (pinned by `BlendsCrossStripAndBlockSeams`; since the
  third batch the bookkeeping is one pure `SeamKeeper` both readers drive, pinned with JPEGDEC-shaped blocks by
  `SeamsHoldForJpegdecShapedBlocks`). A JPEG
  output row is painted per block segment (one sink call per block and row). The fit is computed from the original dimensions; for JPEG the axes map onto
  JPEGDEC's scaled source (`jpegScaleFor`: `⌈src/denom⌉`). EXIF orientation is ignored, as the converters ignore it:
  honouring it means a transposed paint path through the reader and the sink, more than this story's scope.
- **The hold handshake (review, revised at follow-up):** `holdRun` sets Held and clears Run in one step. Every owner
  press is `SweepControl::press(nextFromHold)`, one compare-exchange that decides on the word it changes: Ignored
  while Replaying (a replay with presses waiting: the viewer's first paint, via `reset(.., true)`, or a next
  picture); with `nextFromHold` (the viewer) while Held and no stop, Next (Held cleared; Replay, Run and Replaying
  set); otherwise Toggled (Run flipped), so a hold landing between a look and a write can no longer drop a press.
  The press never clears Held: the task does (`releaseHold`) when it serves a resume, a replay or a clean, so a resume
  then a pause before it serves them keeps the hold and preventAutoSleep's bound. Replay's Run is set in the same
  exchange and no press can clear it while Replaying, so serve needs no Run repair. The viewer writes the next entry
  (`requested`) before pressing and puts it back unless the press was Next; the task reads it only when it serves a
  replay, and none is pending (the viewer checks `replayPending` first, and only it asks). A resume that ever reaches
  a held picture is answered before the resume line and the slot clock (`Client::keepHolding`): Run cleared again,
  the hold's start unchanged.
- **What the extraction changed in the transition test, all intended:** the hold is the word's Held bit (no
  `heldForPhoto` atomic) and preventAutoSleep reads Run and Held from one snapshot; its pause press is
  `press(false)`, one compare-exchange (the old read-then-write could vanish into a hold landing between them);
  Held is released when the task serves the resume (as `heldForPhoto` was); a speed report after a slot needs
  `sweeping` and a signed difference, so no empty `gray scans_s=0.0` follows a hold; the session cap's clean
  repaints before the slot clock and the window restart (the reapply's CPU time no longer inflates that slot);
  `serve` releases Held on a replay or a clean; `prepare`, `launch` and `release` refuse while a task is up.
- **Three layers against a decoder that never returns (third batch, replacing the follow-up's bounded stop):**
  - *Complete walks.* `walkPngChunks` walks to the end of the file (whole chunks after IEND too, fewer than 8 trailing
    bytes tolerated), bounds PLTE (768, whole triples) and tRNS (256), which PNGdec copies into its palette unbounded
    (png.inl 850, 868), and caps the count at 65,536. `walkJpegSegments` walks SOI to SOS at the offsets
    JPEGParseInfo visits, not a textbook segment walk: JPEGDEC steps over a fill byte or anything under 0xFFC0 three
    bytes at a time (jpeg.inl 1642) and reads a length after SOI and RSTn (its default, jpeg.inl 1762), so a walk
    that treated those as the standard does would pass offsets JPEGDEC never visits and miss the ones it does. It
    also checks the two segments JPEGDEC copies into fixed tables: SOF0/SOF2's component count (a 4-entry table)
    and length, and DQT's whole tables (its 16-bit count wraps otherwise). Outcomes: Complete, TooShort, Truncated,
    NoEnd, Malformed, ReadFailed (Open), Stopped. `JpegdecOpensEveryCutTheWalkCallsComplete` pins the mirror against
    the real JPEGDEC.
  - *Reads that cannot stall.* The decoder's size is the walked size; `readAllowance` gives no file bytes past it or
    once stopped; a negative read is 0. PNGdec gets the requested length, the rest 0xFF (`fillPngRead`): its IDAT
    loop subtracts what it read (png.inl 885-904), an all-0xFF chunk length is negative and refused (png.inl 828),
    and inflate meets bad data (png.inl 953). JPEGDEC gets what the file gave (`clampJpegRead`), *not* filler, a
    deliberate departure from the batch's wording: JPEGParseInfo's loop (jpeg.inl 1611) compares a buffer offset,
    kept under 1 KiB by each refill, with the file size, so endless 0xFF keeps it reading in any file over 1 KiB,
    while an empty read ends it (jpeg.inl 1623). `JpegdecIsGivenNoFillerBecauseItsHeaderParseWouldSpin` shows both
    on the real library.
  - *Bounded exits.* `stop()` (Back) waits `STOP_WAIT_MS` (20 s) and leaves the task undeleted (it may hold the
    Storage mutex); the screen shows "Still stopping" and reads no end until `reap()` joins it. `endLocked` (leaving,
    sleep) waits as long, then detaches: one exchange on `taskState` decides against the task's own end; the runner
    is released to the task with an inert client, counted in a static registry (`detachedTasks()`), and the task
    frees it and deletes itself when `sweepRun` returns. It can return only through the read or client call in
    flight, both of which check the sticky stop first and touch only the runner and locals (the viewer's paint copies
    its name, dither and runner pointer before reading and keeps its read error in the runner). A task blocked on
    the render mutex inside the panel is still deleted where it waits, so a detached task is never inside the panel.
    `preventAutoSleep` is false once Stop is set; Run refuses with "Still stopping" while any task is undeleted or
    detached.
- **The decode window (review):** a stop during a decode waits for the task outside the panel. The reader checks the
  sink's stop before every output row, so the wait is normally one row's decode and SD read (bounded at 20 s in
  `stop` and `endLocked`, above). `endLocked` spins holding
  the render mutex meanwhile. It cannot deadlock: the decode takes the Storage mutex inside the decoder but never the
  render mutex, and no Storage-mutex holder waits on the render mutex.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'`: all pass.
- `python3 scripts/check_layers.py && python3 scripts/check_layers_test.py`: exit 0.
- `python3 scripts/check_upstream_touches.py`: exit 0.
- `./bin/clang-format-fix`, run twice: the second run changes nothing.
- After review, by the orchestrator: `pio run -e x4pro`, `-e default`, `-e sticky`; `pio check -e x4pro`; the flash
  gate with D6 (expect +0).

**Manual checks:** on hardware (the owner):
- the chart paints and holds; a line-art PNG paints with Plain and with Ordered; Next steps through the files, a
  double press advancing one picture;
- the transition test through the extracted runner: Static with Hold on and off, Slow, Fast, and Ladder through to
  `gray ladder: done`, their log lines against a pre-extraction run (no empty `gray scans_s=0.0` after a hold is the
  one intended difference), and one run across a session-cap clean;
- the viewer task's `gray sweep task ended` high-water mark for a PNG, a baseline JPEG and a progressive JPEG,
  against the 12 KiB provision;
- the viewer's presses: Next while held paints the next picture; a double Next during that picture's clean and decode
  advances one picture; a pause press at the moment the hold lands leaves the run held or paused, never lost;
- a truncated PNG and a truncated JPEG (copies cut inside a chunk, or inside the JPEG's header) show "format or size
  not supported" at once, a PNG cut just before IEND shows "The picture file has no end", and Back works;
- the bounded exits, if a stuck task can be produced (for instance a scan held in the SDK's BUSY wait past 20 s):
  Back shows "Still stopping" on the Status row, Run shows it too, and the run's end appears once the task ends;
  Home or sleep during it returns within about 20 s with the `detached, its buffers kept until it ends` line, then
  `the detached sweep task ended; its buffers freed`, and the device sleeps normally.

## Implementation Notes

The fallback run of orchestrated-epics.md § 3, with no nested subagents: this agent planned and implemented in one
context. No review step and no lens were run (the orchestrator runs the four lenses). No commit was made: every change
is staged. No `pio` command was run.

- **The geometry move.** `PanelTurn`, `ScreenRect`, `toPanel` and the per-pixel rotate (now `toPanelPixel`) moved
  from `TransitionScene` to `PanelGeometry`, along with:
  - `TransitionScene::retargetLogical`'s clip-and-retarget (now `retargetLogicalRect`);
  - the marker's bars (`pauseBars`) and their settle check (`barsSettled`).

  The scene delegates to them through its new private `screen()`. Every guard moved with them:
  - the clip of x0, x1, y and h to the logical screen;
  - the empty-rectangle return;
  - the too-narrow bar case;
  - the skip of an empty bar in the settle check.

  The 172 pre-existing tests in the display lab's test binary pass unchanged, and `test_transition_scene.cpp` is not
  touched.
- **The runner extraction.** The code moved verbatim; the comparison against `c50e6dd6`'s functions differs only as
  follows:
  - names: `End` for `SweepEnd`, trailing underscores;
  - the scene calls became `Client` hooks;
  - `who` in the two allocation log lines, which keep their text for the transition test ("transition test: ...");
  - `RenderLock lock;` instead of `RenderLock lock(*this);`: the same constructor;
  - one addition, `heldForPhoto_.store(false)` on a replay. The transition test replays only at a run's start, where it
    is already false;
  - a client paint failure ends the run as `PaintFailed`, or `Stopped` when a stop was asked; the transition test's
    hooks never fail.

  One ordering change: on a resume from a hold, the transition test's `gray hold: resumed` line is now logged just
  before the runner clears `heldForPhoto` instead of just after. The flag is read only by `preventAutoSleep`.
- **The viewer's entry list and level names** became pure (`ImageList`, `levelNames`) during the matrix audit, so the
  first and last I/O rows (chart only; Next wraps to the chart) are host-tested.
- **A one-file include clash.** `JPEGDEC.h` and `PNGdec.h` both define `INTELSHORT`, `INTELLONG`, `MOTOSHORT` and
  `MOTOLONG`, differently. The reader includes JPEGDEC's header, undefines the four macros, then includes PNGdec's;
  it uses none of them.
- **Device compile check without pio.** The changed device files were compiled `-fsyntax-only` with the x4pro
  toolchain (`xtensa-esp32s3-elf-g++`, Arduino-ESP32's `flags/*` for esp32s3 `dio_opi`, the x4pro defines,
  `FREEINK_CAP_DISPLAY_LAB=1`) and I18n headers regenerated into scratch by `scripts/gen_i18n.py`. The script is
  `{scratch}/dlab-g3/syntax_raw.sh`, and a deliberate error probe confirmed it catches errors. All compile with no
  error and no warning in the changed files. This is not the firmware build, which the orchestrator runs.
- **Files.**
  - New: `lib/GameDisplayLab/{PanelGeometry,ImageRows,GrayPicture}.{h,cpp}`,
    `src/activities/games/display_lab/{GrayRunner,GrayImageReader,GrayViewerActivity}.{h,cpp}`,
    `test/game_display_lab/{test_gray_picture,test_image_rows}.cpp`.
  - Changed: `TransitionScene.{h,cpp}`, `TransitionTestActivity.{h,cpp}`, `DisplayLabActivity.{h,cpp}`,
    `english.yaml`, `scripts/check_layers.py` and its test, `test/game_display_lab/CMakeLists.txt`,
    `docs/crosshatch/display-lab.md`.
  - The orchestrator's amendments to the epic (D16) and the story are its own commit `361b12e3`, under this staged
    work.

### Review batch (2026-10-18, applied on top of WIP `dc064587`)

All 16 items were patched. Every change is staged; nothing was committed, and no `pio` was run.

1. **Reader closers:** the `Closer` comes before the open result check, so a rejected header closes its file.
   `closeFile` tolerates the null handle of a failed open.
2. **Hold handshake:** `SweepControl` gains Held and Replaying (`holdRun` sets Held, `requestRun(true)` and
   `releaseHold` clear it, `replayFromHold`, `replayPending`, `replayServed`, `snapshot`, `Requests::replaying`). The
   runner's `heldForPhoto_` atomic is gone; `replay()` became `replayFromHold()`; the replay path sets Run for a hold
   replay and marks it served after the paint. The viewer asks only while held, ignores presses while pending, and
   writes `requested` before asking (the task copies it into `painted` at each replay).
   `NextWhileHeldReplaysRunning` moved to the SweepControl suite, which gains five more handshake tests.
3. **Re-hold:** the viewer's `onResume` re-holds a Held picture through the new `GrayRunner::rehold` (no new start,
   no stats report). The Controls row already says Next is the way on.
4. **PNG open failures:** `openFile` sets a file-level `openFailed` (one reader at a time), and both readers report
   `Open` from it.
5. **Library rejections:** `PNG_UNSUPPORTED_FEATURE` and `PNG_TOO_BIG` at open map to `Format`, as does
   `JPEG_UNSUPPORTED_FEATURE` from `getLastError()`.
6. **Heap floors:** `sizeof(PNG)` or `sizeof(JPEGDEC)` plus 16 KiB. The doc and header say about 59 KB for PNGdec.
7. **Runner guards:** `prepare`, `launch` and `release` refuse (`LOG_ERR`) while a task is up.
8. **Pure decode helpers:** `jpegScaleFor` (denominator, option, scaled size), `outputSpan`, `rowsReadyAt`,
   `FitAxis::tap`, `sampleRow`, all pinned in `test_image_rows.cpp`. The reader uses them; its own code is the
   library calls and the callbacks' glue. A `static_assert` pins `JPEG_SCALE_*` to the options' values.
9. **Width limits:** the PNGdec buffer and the gray row each log their own reason.
10. **Decode window:** `endLocked`'s comment states the window and the lock order. Both draw callbacks check the stop
    before every output row.
11. **EXIF and limits:** EXIF orientation is documented as ignored (as the converters ignore it), because honouring it
    needs a transposed paint path, more than a small change. The doc lists the viewer's limits.
12. **First 32 by name:** `ImageList::add`, when full, replaces the greatest name if the new one sorts before it, and
    the result is pinned (`AFullListKeepsTheFirstNamesByName`).
13. **Empty speed line:** `runSlot` reports the speed only while sweeping and with a signed difference, so no empty
    `gray scans_s=0.0` line follows a hold. This is the transition test's one intended log difference.
14. **Bilinear enlarging:** bilinear when the fit enlarges an axis, nearest when it shrinks one. A PNG keeps its
    previous source row for the vertical blend; a JPEG clamps to its block, as the converter does.
15. **Plan wording:** corrected (`restart`, not `cancelHold`; the i18n keys as `english.yaml` has them).
16. **Hardware checks:** added to the story's Verification and to this plan's manual checks, and the runner-sequencing
    item is in `deferred`.

A surprise along the way: PNGdec's `zutil.h` defines `local` as `static`, so a variable named `local` in the reader
broke the device compile. It was renamed and the reason noted.

### Follow-up batch (2026-10-18, on top of the story commit `26bc6699`)

All 13 items were patched. Every change is staged; nothing was committed, and no `pio` was run.

1. **(high) Truncated PNG:**
   - (a) `walkPngChunks` walks the chunk headers (length + 12 each, through IEND) against the file size, through the
     reader's `checkPngLayout`, before PNGdec reads anything. A truncated or malformed file is `Format`; tests
     `PngChunks.*`.
   - JPEG needs no such check: JPEGDEC's DecodeJPEG loops are bounded per MCU and call back each strip (comment in
     the reader).
   - (b) `stop()` waits at most 20 s and then logs, without deleting. `reap()` joins the task later from both
     screens' rows view. `endLocked` logs once at 20 s and keeps waiting.
2. **Press:** `SweepControl::press(nextFromHold)` returns Ignored, Next or Toggled. `requestRun(true)` no longer
   clears Held; the task's serve releases Held on a resume, a replay and a clean. `reset(now, pressesWait)` lets the
   viewer ignore presses until its first paint. `replayFromHold`, `togglePause` and `rehold` are gone. Five handshake
   tests replace the earlier ones, among them the old read-then-write losing a press, which `press()` does not.
3. **Seams:** `SourceBlock`, `sampleBlock` and `outputsEndingIn` replace `sampleRow` and `outputSpan`. The JPEG
   reader keeps the previous strip's last row (the scaled width, allocated only when the fit enlarges vertically) and
   the previous block's last column. `BlendsCrossStripAndBlockSeams` pins the blocks against one whole block, and
   that dropping the kept seams would differ.
4. **Cap repaint timing:** after a session-cap clean the repaint (`prepareSlot` with stale) runs before the clock,
   the slot and the window restart.
5. **Wording:** only the 16.16 blend arithmetic is the converter's (header, ImageRows, doc, tests).
6. **Serve:** the `replaying && !run` branch is removed, with the reason Run is always set there.
7. **Clean releases Held:** the clean path releases Held, pinned on the word by
   `ACleanDoesNotTouchTheHoldTheTaskReleasesIt`.
8. **Stack comment:** the decode and scan chains never nest, so the need is the larger of the two; 12 KiB is
   provisional.
9. **Quiet stop:** a read the stop ended logs at INF with no paint error, and the run ends Stopped.
10. **Log lines:** the JPEG scale is printed for JPEGs only; "read and painted in"; the name limit is in bytes.
11. **Keep holding:** `Client::keepHolding()` is asked before the resume line and the scheduler start, and holds again
    without either.
12. **Colour types:** a colour type that does not exist (1, 5, 7) is refused as `Format`, pinned by
    `PngColourTypesThatDoNotExistAreRefused`.
13. **Plan wording:** the extraction's intended changes are listed in Design Notes; the press, the JPEG per-block
    segments and the device checks are named.

### Third batch (2026-10-18, on top of `cac2ed01`)

All 7 items were patched. Every change is staged; nothing was committed, and no `pio` was run.

1. **(high) Complete walks:** `FileLayout` (Complete, TooShort, Truncated, NoEnd, Malformed, ReadFailed, Stopped)
   replaces `PngLayout`. (a) `walkPngChunks(size, readAt, stopped)` walks to the end of the file, bounds PLTE and
   tRNS, caps the count at 65,536 and takes the stop. (b) `walkJpegSegments` walks SOI to SOS, mirroring
   JPEGParseInfo's offsets (Design Notes say why that, not the standard's fill and standalone rules), and checks
   SOF0/SOF2 and DQT. The reader's `checkLayout` runs either before the decoder's `open()`; ReadFailed maps to Open.
   Tests: `PngChunks.*` (a chunk after IEND, a partial trailing chunk, a zero-length IDAT, a short zlib stream with
   trailing bytes, oversized PLTE and tRNS, a failed read 1 and 2, the cap, the stop, every cut of two sample PNGs)
   and `JpegSegments.*` (cuts in APP0 and SOF0, no SOS, a complete header, fill and standalone markers, the table
   checks, failed reads, the stop, the cap).
2. **(high) Reads:** `readAllowance`, `fillPngRead`, `clampJpegRead` in ImageRows, with png.inl and jpeg.inl lines in
   their comments; `openFile` hands the decoder the walked size; the reads see the sink's stop. JPEGDEC gets no
   filler (Design Notes). `test_image_decoders.cpp` drives the real PNGdec 1.1.6 and JPEGDEC through these answers:
   every cut, told the cut size and told the whole size (a file that shrank after its walk), and five appends of four
   sample pictures (`image_fixtures.h`, made with Pillow), at three JPEG scales, each decode bounded at 4,000 reads
   and draws; the whole samples decode; a stop ends both. It builds only when the decoders' sources exist
   (`CROSSHATCH_PNGDEC_DIR`, `CROSSHATCH_JPEGDEC_DIR`, by default `.pio/libdeps/x4pro/...`), because CI's host job has
   no PlatformIO library folder; CMake says so when it skips it.
3. **(high) Bounded exits:** `endLocked(std::unique_ptr<GrayRunner>&)` waits `STOP_WAIT_MS` then detaches (above);
   both screens own their runner through a `unique_ptr` (null-guarded: the runner is allocated without throwing).
   `preventAutoSleep` is false once Stop is set. New `STR_GAMES_DLAB_STILL_STOPPING`; `stop()` returns whether it
   joined; the Status / Run row says "Still stopping" until `reap()` joins, and Run refuses meanwhile and while
   `detachedTasks() > 0`. The viewer's read error moved into the runner (`setClientCode`), so a late task writes
   nothing of the screen. Header comments and the doc no longer promise an endless wait.
4. **(low) Messages:** a failed read is Open; `ImageRead::Short` and `NoEnd` with `STR_GAMES_DLAB_IMAGE_TOO_SHORT` and
   `STR_GAMES_DLAB_IMAGE_NO_END`; the doc says a file without IEND (or SOS) is refused and lists the row buffers by
   decoder in Memory, the JPEG's two strip rows included.
5. **(low) Seams:** `SeamKeeper` (reset with two caller rows, `begin` returning the `SourceBlock`, `end` keeping the
   last row and column) is driven by both readers and by the seam tests with JPEGDEC-shaped blocks (a right-edge
   block narrower than its pitch, a trimmed last strip) and PNG-shaped rows; `SeamKeeperHandsOutTheRowAboveAndTheColumnLeft`
   pins its hand-outs.
6. **(low) Unused API:** `requestRun` and `held()` removed; `requestReplay` and `requestClean` private, reached by
   the tests through `SweepControlTestAccess`; `heldForPhoto()` removed. The old toggle tests are
   `TwoPressesBeforeTheTaskServesThemCancelOut` and `APressPausesAndTheNextResumes`, in terms of `press()`.
7. **(low) Test comment:** `PngChunks.ATruncatedFileIsRefused` now names its cuts by byte range, one of them inside
   IDAT's CRC.

Device check: the five changed device files (`GrayImageReader.cpp`, `GrayRunner.cpp`, `GrayViewerActivity.cpp`,
`TransitionTestActivity.cpp`, `ImageRows.cpp`) compile `-fsyntax-only` with the x4pro toolchain and regenerated
I18n headers, with no errors or warnings. That check found the screens' `makeUniqueNoThrow<GrayRunner>(..., *this)`
could not reach the private `Client` base from inside the helper; the screens now pass
`static_cast<GrayRunner::Client&>(*this)`.

## Plan Change Log

## Review Triage Log

### 2026-10-18 — Review pass (orchestrator-run lenses, per orchestrated-epics.md § 3's fallback: the session's subagents cannot nest)
- verdicts: 33 findings — high 0, medium 3, low 15, false 0, maybe-false 0, rejected-by-rule 15
- findings:
  - `[medium]` `[patch]` (blind 2, edge E1, E2) the reader's `Closer` was created after the `open()` result check, so a PNG or JPEG the library rejects at the header leaked its `HalFile` and SD handle on every Run or Next — the closer precedes the check (the converters' order).
  - `[medium]` `[patch]` (blind 4, verification-gap other 2) Next while held raced the hold handshake two ways: a second Next during the replay's clean (the hold flag clears only after it) advanced the entry twice and skipped a picture; a Next between the task's `heldForPhoto` store and its `holdRun()` left Replay set and Run clear, so the next picture was painted into the model but never swept and the panel stayed white — the loop side owns the hold's end: Next while held clears the hold and queues the replay in one step, further Nexts are ignored while a replay is pending, and the runner's replay sets Run itself after the hold word; pinned where pure.
  - `[medium]` `[patch]` (edge E5) a pause-toggle while the picture is Held set Run again (the viewer had no `onResume`), so slots spun driving nothing and `runRequested` kept the device awake past the hold's bound — the viewer's `onResume` re-holds while Held (a pause-toggle on a finished picture is a no-op; Next is the way on).
  - `[low]` `[patch]` (blind 1, edge E3) `PNG::open` returns 0 (`PNG_SUCCESS`) when the open callback fails, so a missing or unreadable PNG reported "format or size not supported" — the open failure is recorded and reported as `Open`.
  - `[low]` `[patch]` (blind 3, edge E4) files the libraries reject at `open()` (interlaced or 16-bit PNGs, extended or lossless JPEGs) reported "could not open" — `PNG_UNSUPPORTED_FEATURE`, `PNG_TOO_BIG` and `JPEG_UNSUPPORTED_FEATURE` map to `Format`.
  - `[low]` `[patch]` (blind 5) the PNG free-heap floor used the converter's 44 KiB estimate against a `PNGIMAGE` of about 59 KB at this firmware's `PNG_MAX_BUFFERED_PIXELS` — derived from `sizeof(PNG)` plus the headroom; the header's figure follows.
  - `[low]` `[patch]` (blind 6) the old `startRun`'s "no second run" guard did not move into the runner: `prepare`, `launch` and `release` with a live task would reset state under it — the runner refuses them (early return, logged) while a task is alive.
  - `[low]` `[patch]` (blind 7) `jpegDraw`'s block-to-output mapping, the permille scale choice, the `JPEG_SCALE_*` mapping and `pngDraw`'s row range were device-only though the header called them pinned — pure helpers in `ImageRows` (block span to output span, scaled size and option) with tests, used by the reader.
  - `[low]` `[patch]` (blind 8) one width-limit branch logged the PNGdec buffer for both limits — each limit logs its own reason.
  - `[low]` `[patch]` (blind 9) `endLocked`'s safety argument did not cover the decode window (SD reads under the Storage mutex inside the decoder, the stop checked once per draw callback) — the comment states the window and the lock order (no Storage-mutex holder waits on the render mutex), and the reader checks the stop per decoded row.
  - `[low]` `[patch]` (blind 11, intent) EXIF orientation was ignored without a word — the documented route (honouring it needs a rotated paint path through the reader and the viewer): the doc says it is ignored, as the converters do, and lists the viewer's limits (32 files, names under 64 bytes, dot-files ignored, 8 MP, the PNG row cap).
  - `[low]` `[patch]` (edge E6) with more than 32 files the listed set followed FAT order — the first 32 by name (the list replaces its greatest name when a smaller one arrives); pinned.
  - `[low]` `[patch]` (verification-gap other 1) an empty speed line followed every hold (pre-existing: `holdForPhoto`'s stats window starts after the slot's end, the unsigned difference wraps) — clamped; the transition test loses the line too.
  - `[low]` `[patch]` (intent, scaling) enlarging used nearest sampling where the converters use bilinear, so a progressive JPEG decoded at an eighth came up blocky — bilinear when enlarging, nearest when shrinking, pure and pinned.
  - `[low]` `[patch]` (edge claims C7, C8) the plan named a `cancelHold` the picture scene does not have and i18n keys that differ from `english.yaml` — plan wording.
  - `[low]` `[patch]` (verification-gap 1, blind 10) the transition test's path through the extracted runner is checked by review and a syntax-only compile; the viewer's 12 KiB stack is a guess — the story's Verification gains the hardware check (Static with Hold on and off, Slow, Fast and Ladder to `done` after the extraction, their log lines against a pre-extraction run, one run across a cap clean) and the stack high-water marks for a PNG, a baseline and a progressive JPEG; a host harness for the runner's sequencing is a design addition outside this story (deferred).
  - `[low]` `[defer]` (verification-gap 1) the shared runner's slot sequencing (`serve`, `runSlot`, the hooks' order) has no host test — a pure sequencer with an injected scan and clock would pin it; joins the device-wiring family with a named route.
  - `[low]` `[reject]` (intent 1–8) the reader in the lab's own folder rather than the converters' (the layer edge and the ledger say so), the limits the libraries impose, Next pausing while a paint runs, the lab's own clean through the renderer, seven levels tested on parameters with the device path `SHADE_COUNT`'s (dlab-g2's job), the chart as a function of the logical size, the `LIBDEP_HEADERS` class, the composed runner over a base class, the log wording, one JPEG segment per block row — settled readings, recorded; no change.

### 2026-10-18 — Follow-up review pass (same four lenses, orchestrator-run, over commit 26bc6699; three mediums were patched)
- verdicts: 29 findings — high 1, medium 2, low 14, false 0, maybe-false 0, rejected-by-rule 12
- findings:
  - `[high]` `[patch]` (follow-up blind 1) a truncated PNG hangs PNGdec's IDAT loop (the read callback returns 0, `inflate` reports it needs more data, the loop spins without a draw callback, so the sink's stop is never seen); `GrayRunner::stop()` waits without a bound and `endLocked` spins holding the render mutex, so Back, Home and sleep hang — the reader walks the chunk lengths against the file size before decoding and refuses a truncated file as `Format`; JPEG's truncation path checked the same way; the stop wait is bounded with a logged failure; a truncated-file row in the matrix.
  - `[medium]` `[patch]` (follow-up edge 4 deletion, blind 7) the removed `heldForPhoto` cleared only when the task served the resume; the Held bit clears at the press, so a resume-then-pause before the task serves leaves the scene held with Held clear (auto-sleep ends the photo hold), and `togglePause`'s read-then-write drops a press when `holdRun` lands between — one compare-exchange `press()` on the word (Ignored while a replay is pending, Next while held, else a toggle that sets or clears Run) and the task clears Held when it serves the resume (`releaseHold`), not the press; pinned in the SweepControl suite (the viewer's press decision moves there too, follow-up verification-gap 1).
  - `[medium]` `[patch]` (follow-up blind 3) vertical bilinear sampling is clamped inside each decoded JPEG strip, so at the eighth scale every progressive JPEG enlarges with no vertical blend (stair-step bands about 8 px tall) and full-scale blending breaks at every strip seam — the reader keeps the previous strip's last converted row and blends across the seam; pinned in the pure row mapping.
  - `[low]` `[patch]` (follow-up blind 2, edge 1) after a session-cap clean the viewer's re-decode ran inside the slot's timing, counted as dozens of skipped periods — the window, clock and stats restart after a stale `prepareSlot`.
  - `[low]` `[patch]` (follow-up blind 4) the header, doc and a test comment said the sampling matches the JPEG converter's; it is centre-aligned per axis where the converter is corner-aligned and blends only when both axes enlarge — the texts say only the 16.16 blend arithmetic is shared.
  - `[low]` `[patch]` (follow-up blind 6, verification-gap other) `serve`'s `replaying && !run` branch cannot fire (a replay from a hold sets Run in the same exchange and the task runs no slot while held) and its comment described a race that does not exist — removed, with the reason stated.
  - `[low]` `[patch]` (follow-up blind 8) `requestClean` left Held set with Run clear (no caller today) — the clean path clears Held; pinned.
  - `[low]` `[patch]` (follow-up blind 9) the 12 KiB viewer stack adds two chains that never nest — the comment says the need is the larger of the two and the figure is provisional until the measured high-water marks the story's Verification asks for.
  - `[low]` `[patch]` (follow-up blind 10, edge 3) a stop during a decode logged an error line and set a paint error — handled as a stop, quietly.
  - `[low]` `[patch]` (follow-up blind 11, 12, 14) the PNG success line printed a JPEG scale, "decoded in" included the paint, the long-name log said characters for a byte limit — reworded.
  - `[low]` `[patch]` (follow-up blind 13) the rehold path logged a resume and started the scheduler before the client re-held — the client is asked first.
  - `[low]` `[patch]` (follow-up edge 2) an 8-bit PNG with an invalid colour type painted flat mid-gray — refused as `Format`.
  - `[low]` `[patch]` (follow-up edge claims 5–7, intent) plan wording: the extraction changed semantics (the Held bit, the snapshot `preventAutoSleep`, the signed gated stats, replaying in `serve`), Next while held uses `replayFromHold`, a JPEG run is per block segment — stated.
  - `[low]` `[defer]` (follow-up blind 5, verification-gap 2) the task side of the handshake (`serve`'s replay and hold branches, `replayServed`, `rehold`) and `pngDraw`'s row selection and swap are device-only — the plan names the device checks: Next while held, a double Next during the decode, a pause press at the hold; joins the deferred runner sequencing.
  - `[low]` `[reject]` (intent 1–9) the settled readings of the first pass, restated — no change.

### 2026-10-18 — Second follow-up review pass (same four lenses, orchestrator-run, over commit ca560341; the first follow-up's high was patched)
- verdicts: 30 findings — high 1, medium 0, low 14, false 0, maybe-false 0, rejected-by-rule 15
- findings:
  - `[high]` `[patch]` (second follow-up blind 1, 3, 4, 5, 8, edge 1, 2, 5, 7) the truncated-file fix left the hang reachable: the walk stops at the first IEND while PNGdec parses on when rows remain (an early IEND with a trailing partial chunk, or a short zlib stream, still spins), a read error or a 0-byte read mid-IDAT still spins and a negative read is passed through, the stop is never seen inside the library loop, a truncated JPEG header can spin `JPEGParseInfo` (its marker loop measures a buffer offset against the file size and ignores a failed seek), and the 20 s bound covers Back only: Home and sleep run `endLocked`, which waits forever holding the render mutex, while `preventAutoSleep` keeps the device awake — three layers: (1) the walks are complete: the PNG walk runs to the end of the file (every trailing chunk whole), refuses PLTE over 768 bytes or not a multiple of 3 and tRNS over 256 (PNGdec copies them unbounded), caps the chunk count and checks the stop; a JPEG segment walk from SOI to SOS checks every marker segment's length against the file; (2) the decoders cannot spin whatever the file: both read callbacks clamp a negative return, and on a stop, a read error, end of file or a read past the walked size they return the requested length of filler bytes (0xFF) so inflate fails with a data error and a chunk length reads as invalid; the walked size is the decoder's file size (verified against png.inl and jpeg.inl, stated in comments); (3) the exit paths are bounded: `endLocked` waits the same bound and then detaches, leaving the task and every buffer it may touch allocated and owned by a reap registry until the task ends (never freed under it), logs once and returns; `preventAutoSleep` is false once Stop is set; the screen shows "still stopping" and leaves `sweepEnd` alone until `reap()` joins; `startRun` says why it refuses. A host test builds PNGdec's `png.inl` with a RAM reader (if it compiles on the host with a small shim; else the plan says why not) and feeds every truncation and append of a sample file under an iteration bound.
  - `[low]` `[patch]` (second follow-up blind 9, edge 6) a failed read in the walk was reported as a format problem and shared the "chunk runs past" message with a missing IEND and a file under 8 bytes — a read failure maps to `Open`, each refusal has its own message; the doc says a file without IEND is refused and the Memory section lists the JPEG's two strip rows.
  - `[low]` `[patch]` (second follow-up blind 10, verification-gap 1) `jpegDraw`'s seam bookkeeping (the strip-change swap, `haveAbove`, the edge reset, the clipped copy) and `pngDraw`'s `above` are device-only while the seam test runs its own copy — a pure keeper in `ImageRows` takes the decoder's block and returns the `SourceBlock`, driven by both readers and by the test with JPEGDEC-shaped callbacks (a right-edge block narrower than its pitch, a trimmed last strip).
  - `[low]` `[patch]` (second follow-up blind 11, verification-gap 2) the walk's tests missed a chunk after IEND, a zero-length IDAT, an oversized PLTE or tRNS, a short zlib stream with trailing bytes and a failing read — added.
  - `[low]` `[patch]` (second follow-up blind 12) after `press()`, `requestRun`, `requestReplay`, `requestClean`, `held()` and `heldForPhoto()` have no firmware caller and the old toggle tests still model the racy read-then-write — removed or made private; the tests speak `press()`.
  - `[low]` `[patch]` (second follow-up blind 7, edge 4) the walk had no stop check, yield or chunk cap — the stop and a cap of 65,536 chunks; the reader yields as during a decode.
  - `[low]` `[patch]` (second follow-up edge 3, blind 6) `leaveView(endText())` after a gave-up stop read `sweepEnd` under a live task — covered by the exit-path layer above.
  - `[low]` `[patch]` (verification-gap other) a test comment names a CRC cut that no cut lands in — wording.
  - `[low]` `[defer]` (verification-gap 3) the hold landing between `press()`'s load and its compare-exchange cannot be run on the host — the device check stands.
  - `[low]` `[reject]` (edge claim 8, intent) the transition test's resume now leaves Held for the task to clear — the intended change of the first follow-up, documented.
  - `[low]` `[reject]` (intent 1–7) the readings restated: an unreadable file ends the run with the reason and the Image row steps on (the story's matrix row is amended at the merge), the log format, the chart by logical size, the clean through the lab's own path — no change.

## Auto Run Result

Status: **in-review**. The plan and implementation are done under the orchestrator's fallback (no review, no lens, no
commit, no pio); every change is staged in `/home/user/crosshatch-laneg3` on `dlab-lane-g3`. Blocking condition: none.
The first pass's intent gap (2-bit decoders) was settled by the orchestrator as option B, in the D16 amendment.

What was built:

- **Lab menu:** a Gray viewer row after the transition test.
- **The viewer's rows:** Image, Dither, Run, Levels, Controls, Status.
- **Sources:** the test chart, or PNG/JPEG files from `/.crosshatch/display-lab/images/`.
- **Decoding:** a fork-side reader drives PNGdec and JPEGDEC directly into 8-bit gray, scaled to fit with white
  letterbox.
- **Quantising:** Plain (nearest level) or Ordered (Bayer, scaled to `SHADE_COUNT`).
- **Painting:** one retarget per run of equal shades per row.
- **The hold:** the paint settles, then the pause-symbol marker is driven to 0 or 6, whichever is farther from the
  picture under it, and the run holds. Next paints the next entry, and wraps to the chart after the last.
- **The shared runner:** `GrayRunner`, extracted from the transition test with its behaviour kept.
- **The layer check:** PNGdec and JPEGDEC are classified, and only `GrayImageReader.cpp` may include them (FILE_EDGES,
  D16).

Host verification (this commit's staged tree):

- **Host suite:** `ctest` 2321 of 2321 passed after the third batch (2298 after the follow-up batch, 2290 after the
  review batch, 2278 before it; dlab-g1's merge had 2,236). The display lab binary has 250 tests; the new
  `test_image_decoders` binary has 7 (built here from `.pio/libdeps`; a checkout without it builds 2314).
- **Fast checks:**
  - `scripts/check_layers.py`: passed, 879 edges.
  - `check_layers_test.py`: 54 tests OK, the new decoder-edge test included.
  - `scripts/check_upstream_touches.py`: PASS. `english.yaml` is ledger row 2.
  - `./bin/clang-format-fix`, run twice: the second run changed nothing, and nothing changed outside these paths.
- **Device files:** compiled `-fsyntax-only` with the x4pro toolchain, with no errors or warnings in the changed files
  (method in the Implementation Notes).

Matrix test audit:

| Matrix row | Test |
| --- | --- |
| No folder | `ImageList.NoFolderOrNoFilesIsTheChartOnly` |
| Files present | `ImageList.FilesAreListedByNameAfterTheChart`, `AFullListKeepsTheFirstNamesByName`, `ImageRows.KindsByExtensionAnyCase`, `ImageRows.OtherFilesAreIgnored` |
| Run | `GrayPictureHold.PaintSettlesThenTheMarkerGoesOutThenHolds`, `GrayPaint.TheChartIsTheQuantisedChart` |
| Next while painting | `SweepControl.APressPausesAndTheNextResumes`, `TwoPressesBeforeTheTaskServesThemCancelOut` |
| Next while painting / holding | `SweepControl.APressWhileHeldIsTheNextPicture`, `APressWhileAReplayIsPendingIsIgnored`, `TheViewersFirstReplayMakesPressesWait`, `APressWithoutAHoldToggles`, `AResumeThenAPauseBeforeTheTaskServesKeepsTheHold`, `AHoldBetweenTheOwnersLookAndItsWriteCannotDropAPress`, the wrap in `ImageList.FilesAreListedByNameAfterTheChart` |
| Truncated or malformed file | `PngChunks.*` (8), `JpegSegments.*` (6), `DecoderReads.*` (3), `ImageDecoders.*` (7, where the decoders' sources are present) |
| Previous/Back | `SweepControl.StopIsStickyAndGatesEntry` (existing) |
| Stop past its bound | device-only (the runner's wait, detach and reap need FreeRTOS); the stop the reads see is in `ImageDecoders.AStopEndsEitherDecodeAndItsReads` |
| Unreadable file | `ImageRows.SizeLimitsAreTheConverters`, `PngDepthsAreTheConverters`, `PngColourTypesThatDoNotExistAreRefused`, `PngBufferNeedIsTheConverters` (the reader's Format checks), `PngChunks.AFailedReadIsSaidSo`; the Status text is device-only |
| Dither | the `GrayQuantiser.*` tests at 4 and 7 levels |
| Fit | `ImageRows.FitKeepsTheAspectAndCentres`, `FitAlwaysInsideTheScreen`, `FitAxis*`, `TapsAreNearestWhenShrinkingAndBlendWhenEnlarging`, `BlockSpansPartitionTheOutput`, `RowsReadyAtPartitionTheOutputOnceTheirRowsHaveArrived`, `SampleBlockIs*`, `BlendsCrossStripAndBlockSeams`, `SeamsHoldForJpegdecShapedBlocks`, `SeamKeeperHandsOutTheRowAboveAndTheColumnLeft`, `JpegScaleAndOptionForAFit` |
| Levels | `GrayLevels.TheReadoutNamesTheEnginesLevels`, `GrayQuantiser.TheEnginesLevelCountIsUsed` |
| Session cap mid-paint | `GrayPictureHold.ASessionCapCleanMidMarkerPaintsAgain` |
| Cover, sleep, Home | the shared runner, so the transition test's existing `SweepControl` tests apply |

Device-only and not host-tested: the reader's glue (its HalFile callbacks, the sink into the model) and the runner's
wait, detach and reap. The decoders themselves are host-tested against the read answers where their sources are
present (`test_image_decoders`), not in CI's host job.

Firmware figures: unmeasured (orchestrator builds after review). Not run by this agent: `pio run -e x4pro`,
`-e default`, `-e sticky`, `pio check -e x4pro`, and the flash gate with D6 (expect +0).

Residual risks:

- The viewer task's 12 KiB stack is an unmeasured estimate for the decode chain on the sweep task. The run's
  `gray sweep task ended` line measures it.
- A multi-second decode runs on the sweep task: a stop normally waits for one output row (at most 20 s, then the
  task left undeleted or detached), and the cover check only ends the run at the next scan. A detached task keeps
  its runner's PSRAM (the model, about 1.2 MB at 800x480) until it ends, and no run starts meanwhile.
- JPEGDEC's EXIF parse (jpeg.inl 1654-1678) follows IFD offsets from the file without bounds; the walk does not
  check APP1, as the firmware's own JPEG converter does not.
- `PNGdec` is about 59 KB with the firmware's 16 KB row buffer; the free-heap floor is now its real size plus
  16 KiB (about 75 KB), stricter than the converters' 60 KB.
- The decoder libraries run on the host only in `test_image_decoders`, which CI's host job does not build.

