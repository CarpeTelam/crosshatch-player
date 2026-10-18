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
  - summary: "a JPEG MCU longer than the 512 bytes JPEGDEC keeps past its refill mark reads past the data in its buffer once before the fork's JPEGDEC patch 0004 ends the decode with an error: reachable by a valid file of extreme detail (4:2:0 colour noise at quality 100, MCUs of 510 to 600 bytes: stale bytes inside the 2 KiB buffer in the tests, under the poisoned guard) and by a crafted one of the longest codes (up to about 730 bytes past the buffer, into the decoder's own Huffman tables); the file shows 'did not decode'"
    evidence: 'sixth batch: 8 of 8 Pillow 320x320 4:2:0 quality-100 colour-noise JPEGs, JPEG_NOISE_420_OUTRUN (64x64) and fixtures::longCodeColourJpeg end in JPEG_DECODE_ERROR (ImageDecoders.AColourMcuPastTheBufferEndsTheDecode); the read stays inside the JPEGDEC object (its Huffman tables follow the buffer), read-only, once per file. Exposure: the firmware converters (covers, chapter images) decode the same JPEGs with the same library and get 0004 too (every env), so they now end the same way; before 0004 the bit pointer ran on past the object for every later MCU. Decoding such a file needs JPEGDEC to keep a MCU-sized margin or refill inside a MCU: a change to its refill rule for every JPEG (more, smaller reads), past a build-time stop'
    location: 'scripts/jpegdec_patches/0004-stop-a-bit-pointer-past-the-vlc-data.patch'
    severity: 'low'
  - summary: "a detached sweep task stuck inside an SD read holds the Storage mutex: the next card access after Home or sleep waits behind it for as long as that read lasts"
    evidence: 'fifth batch, kept: reachable only with a card read that never returns (a failing card), then a press (Home, sleep); a timed Storage lock on the sleep path is a follow-up outside this story (HalStorage is upstream)'
    location: 'lib/hal/HalStorage.cpp'
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
| Stop past its bound | a task that has not ended 20 s after the first stop (a decoder that never returns, a scan in the SDK's 30 s BUSY wait); Home's stop and the exit after it share the 20 s | Back: logged once, task left undeleted, Status (transition test: Run) "Still stopping", no end, cover or speed read until `reap()` joins it; leaving the screen or sleep: one LOG_ERR naming the screen (its buffers leaked by design if it never ends), the runner detached to the task (`endLocked`, at once when a stop already gave up), which frees it and deletes itself when it ends; Run shows "Still stopping" while this screen's task is undeleted and "Restart the device to run the lab again" (also on opening) while any task is detached | a stopped run never holds off auto-sleep (`SweepControl::keepsAwake`); a detached task touches only its runner (every hook handed the runner, the inert client, the sticky stop checked after long hooks and before every slot); a detached task inside an SD read holds the Storage mutex, so the next card access waits behind it (reopening the lab or the viewer freezes, sleep waits without a bound: a timed Storage lock on the sleep path is a follow-up); the restart notice clears to "Not run" when the task ends, and the screens allocate their working set only then (at Run; nothing reads the picture, the row scratch or the model before it); the inert client's answers leave the run's end to the stop; a decoder that spins where no read or row reaches it resets the device through the task watchdog (5 s), one that reads on without progress within about 10 s; the task leaves the watchdog around each blocking file call, so a long wait on the card's mutex is not a reset |
| Unreadable file | open (missing, unreadable, a read that fails, a JPEG header read dropped, a corrupt header), format (limits, depth, a colour type that does not exist, an interlaced or 16-bit PNG, an extended or lossless JPEG), short (under a signature), no end (no IEND, SOS, or EOI after it), memory or decode error | run ends; Status row names the reason (`ImageRead` Open, Format, Short, NoEnd, Memory, Decode); logged; the file handle closed on every path | the Image row still steps; the chart still works; a stop during the read or the walk is no error (Stopped) |
| Truncated or malformed file | a PNG: a chunk up to IEND past the end, a bad signature or chunk length, a PLTE over 768 bytes or not whole triples, a tRNS over 256, over 65,536 chunks (what follows IEND is never given to PNGdec); a JPEG: a marker or segment before SOS past the end, a bad SOI or segment length, a SOF0/SOF2 of 0 or over 4 components or a wrong length, a DQT not whole tables, a DHT not whole tables, of a class past 1, an index past 1, over 256 codes or over-subscribed (the Kraft bound), a table segment, SOS or Exif IFD past JPEGDEC's 2 KiB parse buffer or its segment, over 256 buffer moves; no IEND, SOS, or EOI after it; under 8 (PNG) or 4 (JPEG) bytes | refused before the decoder reads it (`walkPngChunks`, `walkJpegSegments`): "format or size not supported", "The picture file has no end: it may be cut short", "The picture file is too short to be a picture" | the decoder is given the walked size (a PNG through IEND; a JPEG through the first EOI after SOS, so a motion photo, a picture with an appended preview or a padded file paints); reads give nothing past it, after a refused seek or once stopped; PNGdec: the rest 0xFF; JPEGDEC: the rest zeros, an open() read from before the header's end kept whole or dropped (the whole parse buffer zeroed, every later header read given nothing, the header's reads capped by the walk's buffer moves, a dropped one "could not open"), every other read the decode's, never dropped: the whole span, runs of 0xFF 0xFF that would starve it tamed (stuffed bytes and restart markers never), `decoderReadCap` reads of the file, open()'s top-ups at most four; PNGdec's palette copy re-reads its buffer and JPEGDEC ends a decode whose bit pointer passed its data (build-time patches); the task watchdog resets a spin no read or row reaches; a stop waits at most 20 s (row above) |
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
  decoders on the host behind the read answers, under AddressSanitizer and UndefinedBehaviorSanitizer, built only when
  `CROSSHATCH_PNGDEC_DIR` and `CROSSHATCH_JPEGDEC_DIR` hold their sources (PlatformIO's library folder by default),
  with `scripts/jpegdec_patches` applied and `platformio.ini`'s PNG buffer size.
- `lib/GameDisplayLab/TaskHandoff.h` and `test/game_display_lab/test_task_handoff.cpp` (fifth batch): the runner's
  end-against-detach exchange and stop bookkeeping, pure. `test/game_display_lab/jpegdec_as_device.{h,cpp}` (fifth
  batch): JPEGDEC built and seen as the device builds it, for the decoder test.
- `.github/workflows/crosshatch-ci.yml` (fourth batch): the Display lab decoders job, which fetches both libraries at
  the pinned versions and runs that test; in the status job's needs.
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
- **Four layers against a decoder that never returns or writes past its object (third batch; fourth: JPEG hardened,
  shared deadline; fifth: the walked size, Kraft, whole-buffer drops, the watchdog; sixth: the forward EOI, two DC
  and two AC tables, the latch's scope, the narrowed tame, the watchdog's gaps, two build-time library patches, the
  callbacks in one header):**
  - *Complete walks.* `walkPngChunks` walks through IEND and bounds PLTE (768, whole triples) and tRNS (256), which
    PNGdec copies into its palette unbounded (png.inl 850, 868), and caps the count at 65,536. Its `WalkExtent::end`,
    IEND's end, is the walked size (fifth batch: the fourth's "Complete at IEND whatever follows" handed the decoder
    the whole file, and PNGdec has no IEND case: a short zlib stream, IEND and a PLTE over 1,024 bytes reached its
    memcpy; `APaletteAfterIendNeverReachesPngdec` shows the canary trip without the walked size and hold with it).
    `walkJpegSegments` walks SOI to SOS at the offsets JPEGParseInfo visits, not a textbook segment walk: JPEGDEC
    steps over a fill byte or anything under 0xFFC0 three bytes at a time (jpeg.inl 1642) and reads a length after SOI
    and RSTn (its default, jpeg.inl 1762). It tracks JPEGParseInfo's 2 KiB buffer (it moves to the parse offset once
    that is 1 KiB in, jpeg.inl 1613-1636; fifth batch: at most 256 moves, `WalkExtent::windowMoves`) and checks
    every segment JPEGDEC reads out of it against both the segment and the buffer: SOF0/SOF2 (1 to 4 components,
    length 8 + 3n: a 4-entry table), DQT (whole 65- or 129-byte tables: its 16-bit count wraps otherwise, jpeg.inl
    1732-1757), DHT (whole tables of class 0 or 1, index 0 or 1, at most 256 codes: any other class or an index past 7
    never shortens JPEGGetHuffTables' count, jpeg.inl 844-870; sixth batch: index 0 or 1 only, as JPEGDEC keeps two DC
    and two AC tables and builds a DC table of index 2 or 3 past its two into the AC tables, jpeg.inl 1093-1100, an
    in-object write; an AC index of 2 or 3 makes JPEGMakeHuffTables fail; the DHT fixtures now use ids 0x01 and 0x11,
    which JPEG_BASELINE's own DHT does not redefine, so the decoder tests build them; fifth batch: code lengths within the Kraft bound, the running
    `code = (code + count[i])` never over 2^i before `code <<= 1`, or JPEGMakeHuffTables writes past its tables,
    jpeg.inl 1066-1275: the review's twelve 1-bit codes walked Complete and open() wrote past its object), SOS, and an
    APP1 JPEGDEC takes for Exif: the IFD offset and tag count it reads, each IFD's count and 12-byte entries
    (GetTIFFInfo, jpeg.inl 1346-1376) and the next IFD's offset when it follows it (jpeg.inl 1654-1677; an offset
    negative as its int, or whose int sum with the buffer offset wraps, is refused; the second test is implied by the
    held-in-segment one, kept to say what JPEGDEC does). An EOI must follow SOS (fifth batch): the first FF D9 after
    SOS, read forward once (sixth batch: no tail shortcut, which handed an appended picture, an MPF preview ending in
    FF D9, to the decoder; the batch's items 1 and 9 asked for a backward look first and for the forward search, and
    the forward search was taken, since a backward look finds the appended picture's EOI) in reads of a 4 KB buffer the
    reader allocates for the walk (`JPEG_EOI_SEARCH_BYTES`; `walkJpegSegments` takes the buffer, its own frame 160 B at
    `-Os`, was 416 B), a 0xFF ending one read carried into the next (`TheEoiSearchCarriesAnFfAcrossItsReads`, every
    split at four buffer sizes); its end is the walked size, `WalkExtent::headerEnd` the SOS segment's end. The first
    EOI rather than the last: appended data (a motion photo's video, an MPF second picture) can hold FF D9 too, and the
    decoder needs only the picture. Outcomes:
    Complete, TooShort, Truncated, NoEnd, Malformed, ReadFailed (Open), Stopped; the reader's `ImageRead` Open,
    Format, Short, NoEnd. `JpegdecOpensEveryFileTheWalkCallsComplete` and
    `EverySyntheticCaseTheWalkPassesDecodesCleanly` pin the mirror against the real JPEGDEC.
  - *Reads that cannot stall.* The decoder's size is the walked size; `readAllowance` gives no file bytes past it or
    once stopped, and none after a seek the file refused; a negative read is 0. PNGdec gets the requested length, the
    rest 0xFF (`fillPngRead`): its IDAT loop subtracts what it read (png.inl 885-904), an all-0xFF chunk length is
    negative and refused (png.inl 828), and inflate meets bad data (png.inl 953). JPEGDEC gets the rest zero-filled:
    not 0xFF (JPEGParseInfo's loop, jpeg.inl 1611, compares a buffer offset kept under 1 KiB with the size, so endless
    0xFF keeps it reading) and not the buffer as it was (after a segment sends its offset past the buffer it seeks and
    reads at the buffer's start, jpeg.inl 1615-1636, and an empty read there leaves SOI and then the APP1 marker read
    as a length, which send it round for ever). Fifth batch: a read starting before `headerEnd` is the header parse's,
    from there on the decode's, so open()'s last top-up (JPEGGetMoreData at jpeg.inl 1780) is answered as the decode.
    Sixth batch: only while open() runs; decode()'s reads are all the decode's and are never latched, the header cap
    counts header reads only (a cap trip on open()'s top-up had latched into the decode and starved it), open()'s reads
    from `headerEnd` on are the decode's for `JPEG_OPEN_TOP_UPS` (4: it makes one) and then get a zeroed span and 0, so
    a parse that ran past the walked header still ends, unlatched, and a latched header failure is `ImageRead::Open`
    whatever open() returned. The bookkeeping is `DecoderReads.h` (`answerPngRead`, `answerSeek`, `answerJpegRead`,
    `answerJpegSeek` over a small file interface), which `GrayImageReader.cpp` runs over `HalFile` and
    `test_image_decoders.cpp` over its fake file, so the decoder tests run the device's callback logic, not a copy.
    The header parse's read (`answerJpegHeaderRead`) is kept whole or dropped: a stop (honoured again, now safe), a
    short or failed read, a refused seek, or header reads past `jpegHeaderReadCap(windowMoves)` (the walk's moves
    and four). A drop zeroes the whole 2 KiB parse buffer (`buf - (2048 - requested)`: JPEGParseInfo always reads
    into the buffer's tail, jpeg.inl 1597, 1634), the bytes a move-down kept too, and latches: every later header
    read is dropped (the file's position is no longer JPEGDEC's iFilePos). The parse then steps over zeros to its
    `iOffset > iBytesRead` error (jpeg.inl 1623) and no segment it began meets zeros: the fourth batch's zeros in the
    span alone left a DQT straddling a move-down refill with its real length over zero tables read as 8-bit ones, its
    count wrapping (`AFailedRefillInsideAHeaderSegmentErrsCleanly` straddles the 2 KiB mark; with span-only zeros
    AddressSanitizer stops it). The decode's read (`answerJpegDecodeRead`) is told the whole span: JPEGGetMoreData
    (jpeg.inl 1544-1566) then tops its buffer up and moves its data down once a MCU ends past FILE_HIGHWATER (1536,
    jpeg.inl 5367), so the bit pointer stays in `ucFileBuf`; short reads would let it pass the data it has, where
    JPEGGetMoreData returns without moving it (jpeg.inl 1549-1550; sixth batch: the fork's JPEGDEC patch 0004 ends the
    decode there). And JPEGFilter (jpeg.inl 1431-1538) drops 0xFF pairs as markers and the 0 of a stuffed 0xFF, so a
    run of 0xFF (an erased sector) filters to nothing; when a refill would leave the buffer under FILE_HIGHWATER + 256,
    the tame rewrites it. Sixth batch, narrowed: the estimate (`jpegStarvationLength`) counts a restart marker (FF D0
    to D7) as its two bytes, and the rewrite turns only a 0xFF followed by another 0xFF into 0, never a stuffed FF 00
    or a marker's second byte: the fifth batch's rule (every 0xFF to 0 when the filtered length was short) turned the
    restart markers of flat line art with a short restart interval into data. `RestartMarkersAreNeverTamed` decodes
    a Pillow line-art JPEG with a restart marker after every block (`JPEG_LINE_ART_RESTARTS`, whose refills the fifth
    batch's rule would have tamed, counted in the test) pixel-identical through the reader's answers and through
    answers that never tame, at every scale; `ScanDataThatWouldStarveTheDecoderIsTamed` and
    `RestartMarkersCountWholeTowardStarvation` pin the rule; an erased sector still decodes to an end
    (`AnErasedSectorInTheScanIsTamed`). Data that still starves the decoder (pairs of FF 00) ends in 0004's error. `iPos` is held under the
    size so JPEGGetMoreData keeps asking to the last MCU; the decode gets the file's bytes for `2 x size / 2048 + 64`
    reads, then only zeros.
  - *Bounded exits.* `stop()` (Back, Home) and `endLocked` (leaving, sleep) share one deadline, `STOP_WAIT_MS` (20 s)
    from the first stop asked. `stop()` then leaves the task undeleted (it may hold the Storage mutex), logged once;
    the screen shows "Still stopping" and reads no end, no stats and no cover until `reap()` joins it (a cover seen
    meanwhile is applied by the join). `endLocked` detaches past the deadline, at once when a stop already gave up,
    after first deleting a task blocked on the render mutex inside the panel. The detach sets the inert client and
    counts the registry before the one exchange on `taskState`, undoes both when the task's end wins, and touches
    nothing of the runner after a win. Fifth batch: those decisions are the pure `TaskHandoff.h` (`detachTask`,
    `finishTask`, `StopTracker`), pinned with the three races (`TaskHandoff.*`: the exchange lost, a cover while
    stopping, a Back that gave up then Home detaching at once, and two threads racing 2,000 times). Every client hook
    is handed the runner; the inert client answers every hook; the runner checks the sticky stop after `onReplay`
    and `prepareSlot` and before every slot. Sixth batch: the inert answers are neutral (`InertAnswers`: no slot
    finishes the run, a cap clean never ends it, a paint not made is the stop's), because `detachTask` publishes the
    inert client before its exchange and a hook in that window gets them even when the task then ends first and the
    owner joins it and reads its end, which stays the stop's; the race test asserts from the task thread, on a lost
    finish, that the inert client and the registry were already published. A task that never returns keeps its buffers and its registry entry for
    good (one LOG_ERR says so); the screens then show "Restart the device to run the lab again", fifth batch: without
    allocating their own working set (the detached runner still holds its model), clearing the notice to "Not run"
    when the task ends after all and allocating at Run. `preventAutoSleep` is `SweepControl::keepsAwake`.
  - *The task watchdog (fifth batch; sixth: the gaps).* The x4pro env builds on the prebuilt `dio_opi` Arduino
    variant (`board_build.arduino.memory_type = dio_opi`, no custom sdkconfig), whose
    `framework-arduinoespressif32-libs/esp32s3/dio_opi/include/sdkconfig.h` has CONFIG_ESP_TASK_WDT_EN 1,
    CONFIG_ESP_TASK_WDT_INIT 1, CONFIG_ESP_TASK_WDT_PANIC 1, CONFIG_ESP_TASK_WDT_TIMEOUT_S 5 and
    CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 1 (CPU1's idle task, the sweep task's core, is not checked); a
    `-fsyntax-only` probe with the x4pro flags asserts the timeout and the panic setting through the reader's own
    includes. The sweep task subscribes for the whole read of a picture, its walk included (`DecodeWatchdog` in
    `readGrayImage`, only when not already subscribed), and feeds it on progress: every row a decoder hands over and
    every read that gives file bytes (the walk's too), with a tick yielded at most every 250 ms of progress. Nothing in
    the SD card feeds it (the fifth batch's note that SDCardManager does was wrong for this reader): every blocking file
    call (open, read, seek, position, close: each waits on the Storage mutex with `portMAX_DELAY`, which another task
    may hold for longer than 5 s, legitimately) runs in a gap (`DecodeWatchdog::Gap`: `esp_task_wdt_delete`, the call,
    `esp_task_wdt_add`). ESP-IDF's task_wdt.c feeds the timer on a delete or an add when every other watched task has
    reset, so reads alone would keep it fed: the gaps stop once the task has spent the timeout outside file calls
    without progress (`WatchdogQuiet`, pinned by `TheWatchdogIsLeftForFileCallsOnlyWhileTheReadMakesProgress`), and
    from then on file calls are watched too. A decoder that spins where no read or row reaches it resets the device
    within 5 s; one that keeps reading without progress within about 10 s. A watchdog not configured (status
    `ESP_ERR_INVALID_STATE`) or a join that fails is logged once a boot (`gray viewer: ...; decodes run unwatched`).
    The timeout is the core's, shared by every watched task (the batch's ~10 s would mean `esp_task_wdt_reconfigure`
    for the whole firmware, which this story does not touch). The restart status stays for what the watchdog does not
    see: a task blocked in the SD card behind the Storage mutex (in a gap, unwatched).
  - *Library patches (sixth batch).* Two findings inside the decoders are fixed at build time, the way upstream
    patches JPEGDEC: `scripts/pngdec_patches/0001-refill-before-palette-copy.patch` (PNGdec copied a PLTE or tRNS out
    of its 2 KiB file buffer without checking it was all there: an indexed PNG with 1.3 to 2 KB of iCCP or text
    before its palette read past the decoder object; the patch refuses a PLTE over 768 or a tRNS over 256 bytes and
    reads the buffer again from the chunk's data when it runs past the bytes read), applied to the x4pro env's
    library copy by `scripts/patch_pngdec.py` (a new pre-script in that env's `extra_scripts`, ledger row 1) and to the
    host test's copy by the CMake; `scripts/jpegdec_patches/0004-stop-a-bit-pointer-past-the-vlc-data.patch` (once a
    MCU leaves the bit pointer past the data in the buffer, JPEGGetMoreData as released returned for good and every
    later MCU read further past `ucFileBuf`; the patch puts the pointer at the data's end, sets JPEG_DECODE_ERROR, and
    DecodeJPEG's MCU loop takes it into `iErr`, so the decode ends at the next check; a MCU over the 512 bytes kept
    past the refill mark gets there: a crafted one of the longest codes, and a valid 4:2:0 picture of noise-like
    detail at quality 100 too, whose MCUs run 510 to 600 bytes: such a file now shows "did not decode" after one
    MCU's read past the data, inside the object, deferred with its exposure), applied with 0001 to 0003 by
    upstream's `scripts/patch_jpegdec.py` in every env (so the converters' cover and chapter path gets it too) and by
    the CMake. The host decoder test's JPEGDEC copy also has a test-only 64-byte `ucGuard` after `ucFileBuf`
    (inserted by the CMake), poisoned for AddressSanitizer from open() to close(), and the bit-pointer canary checks
    `iVLCOff <= iVLCSize`, not the buffer's 2 KiB.
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
  Back shows "Still stopping" on the Status row, Run shows it too, and the run's end (and the transition test's
  speed) appears once the task ends; Home during it returns within about 20 s in all (not 40), sleep likewise, with
  the `detached. It frees its runner's buffers when it ends; ...` line, then `the detached sweep task ended; its
  buffers freed`, and the device sleeps normally; reopening the screen before that shows "Restart the device to run
  the lab again";
- a camera JPEG over 64 KB with a large Exif block: Back pressed while it is being opened and while it decodes
  returns at once (no hang in the header parse), and the picture paints when left alone;
- a JPEG cut short (no EOI) shows "The picture file has no end"; a PNG with bytes after IEND, a motion photo and a
  JPEG with padding after its EOI paint;
- a camera JPEG with an APP1, an APP2 ICC profile and maker notes (a few dozen buffer moves) paints, and the log
  shows the bytes after a motion photo's EOI as not read;
- the watchdog, if a decoder spin can be produced: the device resets within about 5 s (the reset reason in the next
  boot's log is the task watchdog), rather than the screen showing "Restart the device"; no `decodes run unwatched`
  line at the first picture (the x4pro build's watchdog is configured);
- the watchdog's gaps, if another task can be made to hold the SD card for several seconds while a picture opens:
  the picture paints late, without a reset;
- an indexed PNG with a long iCCP profile or text block before its palette (1.3 to 2 KB) paints with the right
  colours; a JPEG with restart markers (a scanner's or Photoshop's line art saved with a restart interval) paints as
  on a computer; a picture with an appended preview (a phone's MPF JPEG) paints the main picture;
- after a detach (Home during a stuck run), reopening the screen shows "Restart the device to run the lab again"; if
  the task then ends, the Status (transition test: Run) row returns to "Not run" and Run works.

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

### Fourth batch (2026-10-18, on top of `e5dfd6f2`; the owner chose to harden JPEG rather than defer it)

All 12 items were patched. Every change is staged; nothing was committed, and no `pio` was run. The container had
restarted: `build/test` was still configured and `.pio/libdeps/x4pro/{PNGdec,JPEGDEC}` present, the JPEGDEC patches
applied.

1. **(high) JPEGDEC's header-parse spin:** `fillJpegRead` zero-fills the unread span (`jpegKeptBytes`: a header read
   whole or not at all; `jpegPositionAfterRead`; `decoderReadCap` per open() and per decode()). Two departures from
   the batch's wording, both forced by the library and pinned on it: the header parse is not stopped (zeros inside a
   DQT it began overran its object under AddressSanitizer), and the decode is told the whole span (short reads let
   its bit pointer leave `ucFileBuf`). Tests: `bigExifJpeg()` (66,713 bytes, a 4,098-byte APP1, two COM seeks) at
   every cut (walked and shrunk) and with a stop at every read; the stale-buffer and 0xFF spins shown on the library.
2. **(high) Detach order:** `GrayRunner::detach` copies `who`, sets the inert client and counts the registry before
   the exchange, undoes both and joins on failure, and touches nothing after success.
3. **(high) Hooks:** every `Client` hook takes `GrayRunner&`; both screens use it and never their `unique_ptr` from
   the task; the inert client overrides every hook; the runner checks the stop after long hooks and before each slot;
   the header says what a detached task may touch.
4. **(medium) JPEG walk bounds:** EOI as the last two bytes; JPEGParseInfo's buffer tracked; SOF, DQT, DHT, SOS and
   Exif IFDs inside segment and buffer; pinned by `JpegSegments.*` with crafted segments. JPEGGetMoreData's pointer:
   inside `ucFileBuf` with the whole-span answer; with short reads it was not (Design Notes), now bounded.
5. **(medium) Tests:** `PNG_NOISE` (3,188 bytes, IDAT of 3,131) in the every-cut test; `got == 0` cases in
   `DecoderReads`; `JPEG_SCALE_QUARTER`; stops at every read for both decoders; AddressSanitizer and
   UndefinedBehaviorSanitizer on `test_image_decoders` and the decoders (alignment and shift checks off for the
   decoders' own sources, which load unaligned words and shift by negative counts by design, jpeg.inl 1115); ctest
   `TIMEOUT 300` and label `display-lab-decoders`; `PNG_MAX_BUFFERED_PIXELS` read from `platformio.ini`; the CMake block
   applies or checks `scripts/jpegdec_patches`. CI: a new **Display lab decoders** job in `crosshatch-ci.yml` (in the
   status job's needs) fetches PNGdec 1.1.6 and JPEGDEC at the pinned commit (versions read from `platformio.ini`),
   configures with the two paths, and runs `ctest -L display-lab-decoders --no-tests=error`; simulated here end to end
   (patches applied, 10 tests passed). Not in `ci.yml`'s unit-tests job as the batch said: `ci.yml` is upstream's and
   not in the touch ledger, and this batch adds no ledger row (the fork's own workflow is where fork-only jobs go).
6. **(low) Home:** one deadline from the first stop asked; `endLocked` detaches at once when it has passed.
7. **(low) Still stopping:** `leaveView(joined)` skips the transition test's stats when not joined; `viewEnds` keeps a
   pending cover; `join()` (from `stop`, `reap`, `endLocked`) applies it; the stats row is filled after `reap()`.
8. **(low) PNG walk:** Complete at IEND; `PngChunks.WhatFollowsIendIsNotWalked`; the doc follows.
9. **(low) Never-returning detach:** `STR_GAMES_DLAB_RESTART_DEVICE`, shown on opening and on Run while
   `detachedTasks() > 0`; the detach's one LOG_ERR says the buffers are leaked by design if it never ends; the
   registry's comment says it never clears then.
10. **(low) Awake:** `SweepControl::keepsAwake` (static over a word, and over `snapshot()`), pinned by
    `RunKeepsTheDeviceAwakeAndAHoldOnlyForItsBound`; `preventAutoSleep` calls it.
11. **(low) paintEntry's frame:** measured with `-fstack-usage` on the x4pro toolchain at `-Os`: **176 bytes**
    (`GrayViewerActivity.su`), under 256. The path and name (97 bytes) moved to the runner's client text, and the
    JPEG scale buffer (32 bytes) went for two log calls.
12. **(low) Wording:** the doc and this plan say that after a detach the next card access (Home's screen, sleep's
    steps) can wait behind the Storage mutex a stuck SD read holds; `ImageRead` Short and NoEnd are in the matrix; the
    transition test's edge-path changes are listed below.

The transition test's edge-path changes, all from the shared runner: "Still stopping" on its Run row after a Back
that outlasts 20 s, with its end and Speed row filled once `reap()` joins; Run refused ("Still stopping", or the
restart text while a task is detached); the detach on onExit and prepareForSleep, sharing Home's 20 s; auto-sleep
allowed as soon as a stop is asked; its hooks handed the runner (`onReplay`'s history and weak-row line, `afterSlot`'s
hold).

Device check: the five changed device files compile `-fsyntax-only` with the x4pro toolchain and regenerated I18n
headers, with no errors or warnings.

### Fifth batch (2026-10-18, on top of `7b859e22`)

All 12 items were patched. Every change is staged; nothing was committed, and no `pio` was run.

1. **(high) PNG walked size:** `WalkExtent::end` is IEND's end and `checkLayout` hands it to the decoder; the
   fourth batch's instruction is reverted as the batch said. `fixtures::pngPaletteAfterIend(1500)` (a short stream,
   IEND, a 1,500-byte PLTE): with the walked size PNGdec errs and the row-buffer canary holds; given the whole file
   the canary trips (a 3,000-byte PLTE would also read past PNGdec's object, which AddressSanitizer stops the binary
   at, so the evidence uses 1,500).
2. **(high) Kraft:** the DHT check above; the review's file and three more over-subscribed tables are Malformed, two
   exact ones Complete; `OverSubscribedHuffmanTablesAreRefusedBeforeOpen`.
3. **(high) Header reads:** (a) a drop zeroes the whole parse buffer; (b) latched, every later open() read dropped;
   (c) the mode switches at `headerEnd`; (d) the stop is honoured in the header, open()'s reads capped at the walk's
   moves and four, over 256 moves Malformed. Tests: the straddling and 1,100-byte DQT files failing at every byte
   over 400 and 500 bytes; a stop at every read of open() (`JpegdecReturnsAtAStopOnEveryRead`, which asserts open()
   ends within two reads of a header stop).
4. **(medium) Decode coverage:** `JPEG_NOISE_BASELINE` and `JPEG_NOISE_PROGRESSIVE` (Pillow, 128 x 128 noise,
   quality 60) through every cut (walked and shrunk), every stop and the whole decode, with decode-phase reads
   asserted; an erased sector (4 KB of 0xFF) and a whole scan of 0xFF; the 0xFF tame (above). A mutation run showed a
   short decode answer, a missing Kraft check, the IEND-less walked size, span-only drops and a header end at EOI
   each fail the suite; the tame and the window cap are caught by the pure tests only, and the IFD-sum test is implied
   by the segment bound (Design Notes).
5. **(medium) Differential:** `fixtures::jpegSegmentCases()` (64 synthetic headers, each with the walk's verdict,
   built on JPEG_BASELINE's frame so they decode) is shared by `JpegSegments.EverySyntheticCase` and
   `ImageDecoders.EverySyntheticCaseTheWalkPassesDecodesCleanly`; single-byte changes over JPEG_BASELINE's header
   (`SingleByteChangesToARealHeaderThatTheWalkPassesDecodeCleanly`, the reader's size limit applied after open() as the
   reader does, since a changed SOF can claim 65,535 rows). Canaries inside the objects (the test reads their fields
   through `#define private public` around the two headers): JPEGDEC's bit pointer within `ucFileBuf` at every strip
   and after the decode, its `ucHuffDC` / `usHuffAC` after the buffer unchanged by the decode; PNGdec's `ucPixels`
   past 256 bytes (no row of these samples reaches it) unchanged. Guard bytes around the objects were not used: the
   objects are their own allocations, so AddressSanitizer already sees past them, and a wrapper would hide that.
   `REGISTER_WIDTH` 32: `jpegdec_as_device.h` hides `__x86_64__` (after the system headers) while JPEGDEC's header is
   read, and `jpegdec_as_device.cpp` builds its sources the same way, so the bit reader, the scalar paths and
   `ALLOWS_UNALIGNED` are the device's (a define of `REGISTER_WIDTH` alone cannot: the header sets it from the
   architecture macros). Sanitizers narrowed per source, with the reason in the CMake comment: alignment off for
   both decoders; for JPEGDEC also shift (GCC still reports the negative exponent at jpeg.inl 1115 with only
   shift-exponent off) and signed-integer-overflow (its IDCT on corrupt coefficients, jpeg.inl 2673, wraps on the
   device), found by the single-byte changes.
6. **(medium) EOI:** the tail first, else the first FF D9 after SOS, its end the walked size (the first rather than
   the last: Design Notes); `TheWalkedSizeEndsAtTheFirstEoiAfterSos`; the appended-tail decoder tests run on.
7. **(low) Negatives:** in `jpegSegmentCases`: DQT of 33 tables; 17 tables at the start and after a 996-byte APP0; SOS
   past the buffer; Exif one entry across the segment's end, 200 entries across the buffer, a count 3 with the next
   IFD offset past the segment, an IFD ending exactly at the segment's end and one byte past, a little-endian overrun,
   the IFD1 sum at INT32_MAX and one past; an SOS whose last two bytes are FF D9 at the file's end.
8. **(low) Restart status:** cleared in loop() when `detachedTasks()` is 0; onEnter defers the working set
   (`deferred`), Run allocates it (both screens; the transition test's allocation is `allocateWorkingSet()`).
9. **(low) Patches:** JPEGDEC's sources are copied into the build tree on every configure and the patches applied
   there (`GIT_CEILING_DIRECTORIES` makes git a plain patch tool in the worktree's build tree); verified with an
   unpatched clone, inside and outside the worktree, the clone left untouched.
10. **(low) Logs and the Storage note:** the NoEnd and Malformed lines name their causes, a line says when bytes
    after the end are not read; the doc's Storage-mutex note adds the re-entry freeze and the unbounded sleep, a timed
    Storage lock on the sleep path named as a follow-up outside this story.
11. **(low) Pure handoff:** `TaskHandoff.h` and `test_task_handoff.cpp` (above).
12. **(low) Watchdog:** in, as Design Notes say (5 s, the core's).

Device check: the five changed device files compile `-fsyntax-only` with the x4pro toolchain, no errors or warnings.
Stack frames at `-Os` (`-fstack-usage`): `paintEntry` 176 B, `walkJpegSegments` 416 B (its 256-byte scan buffer),
`jpegRead` 48 B; the walk runs before the decode, not under it.

### Sixth batch (2026-10-18, on top of `e9bf676a`)

All 12 items were patched. Every change is staged; nothing was committed, and no `pio` was run.

1. **(medium) Watchdog around file calls:** `DecodeWatchdog` now spans the whole read (`readGrayImage`, before the
   walk); every blocking `HalFile` / `Storage` call (open, read, seek, position, close) runs in a `Gap`
   (`esp_task_wdt_delete`, the call, `esp_task_wdt_add`); progress (a row, a read or walk read that gave file bytes)
   feeds it and yields a tick every 250 ms. A gap is skipped once the task has spent the timeout outside file calls
   without progress (`WatchdogQuiet`, since a delete or add feeds the timer). A missing watchdog or a failed join or
   rejoin is logged once a boot. The x4pro sdkconfig values (Design Notes) are recorded and checked with a
   `-fsyntax-only` probe; the doc's and the code's claim that the card manager feeds the watchdog is gone. The EOI
   search runs inside the watchdog with a 4 KB heap buffer, forward only (item 9, below; the item's "backward first"
   and item 9's "prefer the forward search" conflict, and the forward search was taken).
2. **(medium) Tame:** `jpegStarvationLength` counts FF D0-D7 whole; the rewrite touches only a 0xFF followed by
   0xFF. `JPEG_LINE_ART_RESTARTS` (Pillow, 256 x 192, a restart marker after every block) decodes pixel-identical
   through the reader's answers and through never-tamed answers at every scale, and the test counts the refills the
   fifth batch's rule would have tamed (`RestartMarkersAreNeverTamed`).
3. **(medium) PNGdec palette:** `scripts/pngdec_patches/0001`, applied by the new `scripts/patch_pngdec.py` in the
   x4pro env (idempotent like `patch_jpegdec.py`; ledger row 1's text names it) and by the CMake, which now copies
   and patches both decoders (`display_lab_patched_copy`). `APaletteAfterALongTextChunkIsReadWhole`: an indexed
   PNG_PALETTE_256 with 1,000 to 1,900 bytes of tEXt before its 768-byte PLTE decodes to the pixels of the file without
   the text, plus every cut and stop of the 1,500-byte case. `APaletteAfterIendNeverReachesPngdec`'s unwalked case
   now holds too (the patch refuses a PLTE over 768).
4. **(medium) Latch scope:** `answerJpegRead` (DecoderReads.h): the header branch only while open() runs and below
   `headerEnd`, the cap counting header reads only; every other read is the decode's and never latched; open()'s reads
   from `headerEnd` on get the decode's answer four times (`JPEG_OPEN_TOP_UPS`) and then a zeroed span and 0, so the
   no-SOS test's parse still ends (a past-the-header variant added); `readJpeg` returns Open when a header read was
   dropped, whatever open() returned. Pinned by `OnlyOpensReadsBeforeTheHeadersEndAreLatched`,
   `JpegHeaderReadsAreDroppedAtAStopAFailureOrTheirCap`.
5. **(medium) JPEGDEC 0004:** `scripts/jpegdec_patches/0004-stop-a-bit-pointer-past-the-vlc-data.patch` (the iDelta < 0
   branch sets `iVLCOff = iVLCSize`, `bb.pBuf = ucFileBuf + iVLCSize`, `iError = JPEG_DECODE_ERROR`; DecodeJPEG copies
   it into `iErr` after the refill call, and both MCU loops stop on `iErr`; `iError` is 0 at decode start, open()
   clearing the object). Applies after 0001-0003 to a fresh clone (verified). Colour fixtures `JPEG_NOISE_420` (48 x
   48) and `JPEG_NOISE_444` (32 x 32), quality 100, through every cut, every stop and the whole decode at every scale;
   the canary checks `iVLCOff <= iVLCSize`. Found while proving the patch: valid 4:2:0 colour noise at quality 100
   reaches it (8 of 8 Pillow 320 x 320 files, and `JPEG_NOISE_420_OUTRUN`, 64 x 64): such a file now ends in a decode
   error, which is the deferred entry; `fixtures::longCodeColourJpeg` (the longest standard codes) does too.
6. **(low) DHT index:** class and index each 0 or 1; DC 0x02 and 0x03 and AC 0x12 cases; the Complete DHT cases use
   0x01 and 0x11, which JPEG_BASELINE does not redefine, so JPEGDEC builds them (`DHT AC 256 codes` and `DHT AC two
   1-bit codes` added).
7. **(low) Tests:** `reads.decodeReads > 0` (decode() only, reset at `startDecode`) in the noise tests, and `== 0`
   for the small samples; `JPEG_PROGRESSIVE_512` (flat 8 x 8 blocks of random gray, a DC scan of about 3 KB) refills
   inside decode() at an eighth; `TheEoiSearchCarriesAnFfAcrossItsReads`; the race test asserts from the task thread
   on a lost finish that the inert client and the registry were published; `InertAnswers` are neutral and pinned
   (`TheInertAnswersLeaveTheRunsEndToTheStop`); `StopTracker::joined` resetting the give-up is pinned.
8. **(low) Guard:** the CMake inserts `uint8_t ucGuard[64]` after `ucFileBuf` in the build tree's JPEGDEC.h (a
   static_assert keeps it whole ASan granules); the test poisons it in its open callback (after JPEGDEC's memset) and
   unpoisons it after close. The mutation run's span-only drop is now an AddressSanitizer use-after-poison.
9. **(low) EOI:** the forward search only, no tail shortcut; an appended picture is no longer handed to the decoder.
10. **(low) Walk buffer:** `walkJpegSegments(..., extent, search, searchBytes)`; the reader allocates
    `JPEG_EOI_SEARCH_BYTES` (4 KB) on the heap for the JPEG walk; a null or small buffer falls back to the walk's
    17 bytes. Frames at `-Os`: `walkJpegSegments` 160 B (was 416), `readJpeg` 240 B and `readPng` 224 B (now
    `noinline`, so the two decoders' contexts do not share `readGrayImage`'s frame, 80 B).
11. **(low) Shared reads:** `lib/GameDisplayLab/DecoderReads.h` (`DecoderReads`, `readUnderAllowance`,
    `answerPngRead`, `answerSeek`, `answerJpegRead`, `answerJpegSeek`, `WatchdogQuiet`); the reader adapts `HalFile`
    (`ReaderFile`, with the gaps), the decoder test its fake file (`FakeAccess`); five pure tests in
    `test_image_rows.cpp`.
12. **(low) Comments and audit:** `ImageRead::Open`, `Format` and `NoEnd`; the CMake's "Three UBSan checks"; the
    watchdog comments; ImageRows.h's layer count. Audit: in both screens the picture (`picture`, `scene`), the row
    scratch (`grayRow`, `shadeRow`) and the model are read only by the task's hooks, after Run allocated them; Rows,
    render, loop and reap read the runner's stats and the screens' own text only.

Mutation run (`vg6/mut6.py`, 18 mutants of the walk, the answers, DecoderReads.h and the watchdog's quiet time):
every one fails the suite. The decoder tests catch the walk's SOS, DQT, DHT and IFD bounds and the DC index (as
verdict mismatches in the shared cases), the unbounded open top-ups and the reset of decode() reads; the span-only
drop through the guard; the pure tests catch the rest (EOI carry and tail shortcut, every form of the tame, the
latch's scope, the header cap, progress on reads, the quiet limit).

Device check: `GrayImageReader.cpp`, `GrayRunner.cpp`, `GrayViewerActivity.cpp` and `TransitionTestActivity.cpp`
compile `-fsyntax-only` with the x4pro toolchain, no errors or warnings; the watchdog probe passes. The fork CI's
decoder job was simulated with fresh clones of PNGdec 1.1.6 and JPEGDEC: all five patches applied to the build
tree's copies, 20 of 20 passed.

Reachability of what remains (the finishing rule):

| Remaining | Valid file | Damaged file | Press | Crafted file |
| --- | --- | --- | --- | --- |
| A JPEG MCU over JPEGDEC's 512-byte margin (deferred) | 4:2:0 colour noise at quality 100: "did not decode", no hang, no crash | not seen (zeros, erased sectors and cuts decode to an end) | no | the longest codes: one MCU's read up to about 730 B past the buffer, inside the object, then "did not decode" |
| A detached task inside a stuck SD read holds the card's mutex (deferred, fifth batch) | no | a card whose read never returns | Home or sleep then waits behind it | no |
| A decoder spin no read or row reaches | no | none known | no | none known; the watchdog resets within 5 s |
| A decoder reading on without progress | no | none known (every read path is capped) | no | none known; the watchdog resets within about 10 s |

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

### 2026-10-18 — Third follow-up review pass (same four lenses, orchestrator-run, over commit 0a5e3b73; the second follow-up's high was patched; the owner chose to harden JPEG rather than defer it)
- verdicts: 33 findings — high 1, medium 2, low 14, false 0, maybe-false 0, rejected-by-rule 16
- findings:
  - `[high]` `[patch]` (third follow-up blind 1, 3, 4, edge 1, 2, 9) three paths still reach a hang or a use-after-free: JPEGDEC's header parse spins in its seek branch when a read gives 0 bytes after a segment jumped past the first 2 KiB buffer (a stop or an SD read error during a camera photo's APP1: the stale buffer's SOI and 0xFFE1 length send it round for ever), the detach exchange runs `client_.store`, the registry increment and a log that reads a runner member after the task may have deleted the runner, and client hooks on the sweep task read the screen's released `unique_ptr` and screen members after a detach — the JPEG read callback zero-fills the span it cannot fill (so the parser steps to its own length error) under a per-open and per-decode read cap, verified against jpeg.inl and tested with a fixture over 64 KB whose header exceeds the buffer at every read index; the detach stores the inert client and bumps the registry before the exchange, copies the name, and undoes on failure; every client hook receives the runner by reference and the detached client is inert for every hook.
  - `[medium]` `[patch]` (third follow-up blind 6, 7, edge 7, 8) the JPEG walk ends at SOS and bounds nothing JPEGDEC reads in place: a cut inside scan data walks as whole and empty reads let `JPEGGetMoreData` advance past its buffer, an Exif IFD offset from the file is a wild read, and DHT and DQT segments are read past the 2 KiB buffer — the walk requires EOI as the file's last marker, bounds the Exif IFD to the segment and the buffer, validates DHT tables (class and index, sixteen counts summing to at most 256, total equal to the length) and DQT tables (whole tables within the buffer); pinned.
  - `[medium]` `[patch]` (third follow-up verification-gap 1, 2, blind 8, 9) the every-cut tests used fixtures under the decoders' 2 KiB buffer, so the refill path where the hang lived never ran (a mutant returning 0 on an empty read passed), and the decoder tests are skipped in CI — fixtures over 2 KiB for PNG (multi-buffer IDAT) and over 64 KB for JPEG, the `got == 0` read case, a stop at every read index, the quarter scale, AddressSanitizer and UndefinedBehaviorSanitizer on the decoder target, a ctest timeout, and CI fetching the pinned PNGdec 1.1.6 and JPEGDEC commit (with the fork's JPEGDEC patches applied) so the target runs with `--no-tests=error`; the PNG buffer size read from one source.
  - `[low]` `[patch]` (third follow-up blind 5, edge 3) Home waited 20 s in `stop()` and 20 s more in `endLocked` — one deadline, and an immediate detach when the task is already known to be stopping.
  - `[low]` `[patch]` (third follow-up edge 4, 5) `leaveView` read the stats fields and `viewEnds` wrote `sweepEnd` under a live detached task — skipped while still stopping, applied after `reap()` joins.
  - `[low]` `[patch]` (third follow-up edge 6, intent) the PNG walk refused a valid file with trailing bytes after IEND — Complete at IEND: the filler read makes trailing bytes harmless.
  - `[low]` `[patch]` (third follow-up blind 2) a detached task that never returns left both screens refusing Run with no explanation and the registry never clearing — a terminal status ("Restart the device to run the lab again") with one `LOG_ERR`, and the doc says the task's PSRAM is leaked by design.
  - `[low]` `[patch]` (third follow-up verification-gap 3) the awake decision (`preventAutoSleep` over the word's snapshot and a clock) is pure — moved into `SweepControl` and pinned; the detach arbitration stays a device check.
  - `[low]` `[patch]` (third follow-up intent) `paintEntry`'s locals come to about 270 B by hand against the 256 B rule — the frame is measured (`-fstack-usage`) and brought under.
  - `[low]` `[patch]` (third follow-up blind 10, edge claims 10, 11) after a detach the sleep and Home paths can wait behind the Storage mutex the task holds inside an SD read (the SDK's BUSY wait runs to 30 s) — the doc says so; the plan lists `Short` and `NoEnd` among the read outcomes and the transition test's edge-path changes (still stopping, the Run refusal, the detach on exit, auto-sleep after a stop).
  - `[low]` `[patch]` (third follow-up verification-gap other) the host decoder build used JPEGDEC without the fork's patches — the CMake block applies or checks them.
  - `[low]` `[reject]` (intent 1–6) the settled readings restated; the story's "next entry still works" row is amended at the merge.

### 2026-10-18 — Fourth follow-up review pass (same four lenses, orchestrator-run, over commit b0fc7069; the third follow-up's high was patched)
- verdicts: 36 findings — high 1, medium 3, low 15, false 0, maybe-false 0, rejected-by-rule 17
- findings:
  - `[high]` `[patch]` (fourth follow-up blind 1, edge 1; verification-gap other; blind 2, 3, 4, edge 2, 3, 5, 6) three memory-safety paths and the named residual: the PNG walk's "Complete at IEND whatever follows" (the orchestrator's own instruction of the previous batch) handed PNGdec the whole file, so a short zlib stream followed by a PLTE over 1,024 bytes after IEND reaches an unbounded palette copy (heap corruption); an over-subscribed Huffman table (twelve codes of length one) walks Complete and JPEGDEC's table builder writes past its object; a header-parse read dropped short at the move-down refill lets a DQT's length wrap and read past the buffer, and the dropped read leaves the file position shifted from the decoder's; and open()'s final top-up read answered in header mode leaves the bit pointer able to run past the buffer — the PNG walk returns IEND's end as the walked size (filler after it); the JPEG walk validates Huffman code lengths (the Kraft bound per length) and refuses the over-subscribed table; a dropped header read zeroes the whole 2 KiB parse buffer and seeks back (or latches), so the parse ends in the decoder's own error; reads at or past the walked SOS end are answered in decode mode; the header parse honours the stop at segment boundaries and is capped by the walk's window-move count (a header with more than 256 window moves is refused); each pinned, the first two through the real decoders.
  - `[medium]` `[patch]` (fourth follow-up verification-gap 1, blind 6, edge 4) no JPEG test reached the decode-phase refill (every fixture's scan was the 40×24 sample's) and an erased-sector run of 0xFF in scan data can starve the refill below the decoder's high-water mark — noise fixtures over 128×128 (baseline and progressive) run every cut, shrink and stop with an assertion that decode-phase reads happened, plus an 0xFF-run fixture; the decode answer replaces bytes the file cannot safely give.
  - `[medium]` `[patch]` (fourth follow-up verification-gap 4, blind 7) the walk and JPEGDEC were checked against each other on three well-formed files only, and the sanitizers could not see intra-object overruns or the device's 32-bit bit reader — a differential test opens and decodes every synthetic file the walk calls Complete under the sanitizers, with canaries after the decoder's file buffer and palette and the register width forced to 32; the `-fno-sanitize` list narrowed to what the decoders need, with the reason.
  - `[medium]` `[patch]` (fourth follow-up intent, edge 7, blind 5) requiring EOI as the file's last two bytes refuses motion photos and padded files — the walk finds the last EOI after SOS and uses its end as the walked size; appended tails are tested again.
  - `[low]` `[patch]` (fourth follow-up verification-gap 2, 3, edge 3) negative cases for the DQT and SOS buffer bounds, the Exif bounds (an entry crossing at count 1, a count of 200, a following IFD out of bounds, an IFD ending at the segment's end, a little-endian overrun), the IFD sum overflow near `INT32_MAX`, and the SOS-then-EOI two-byte survivor — added.
  - `[low]` `[patch]` (fourth follow-up edge 8, intent) the restart status never cleared when a detached task ended while the screen was open, and `onEnter` allocated a second model while a detached task held its own — cleared in `loop()` when the registry drops to zero; no allocation while it is above zero.
  - `[low]` `[patch]` (fourth follow-up edge 9) the host-test configure patched JPEGDEC in `.pio/libdeps` while a firmware build could patch the same folder — the patches apply to a copy in the build tree.
  - `[low]` `[patch]` (fourth follow-up blind 8, 10) `checkLayout`'s log lines named the wrong causes; the Storage-mutex note missed that re-entering either lab screen lists the folder through Storage and that sleep is unbounded behind it — reworded; a timed Storage lock on the sleep path is noted as a follow-up outside this story.
  - `[low]` `[patch]` (fourth follow-up blind 9) the detach, deadline and covered-pending decisions have no host test — a pure helper with the three races pinned (a lost exchange, a cover while stopping, a Back give-up then Home).
  - `[low]` `[patch]` (orchestrator) a last layer against a spin no read reaches: the sweep task joins the ESP task watchdog for the length of a decode (fed from the per-row yield) where the core's watchdog panics and resets, so a spin ends in a reboot within seconds rather than a leaked task; documented, with the "restart" status kept for the Storage-mutex case.
  - `[low]` `[reject]` (fourth follow-up intent, edge claims 10, 11) the readings restated; the header parse's stop behaviour and the transition test's edge paths are documented decisions.

### 2026-10-18 — Fifth follow-up review pass (same four lenses, orchestrator-run, over commit 59f970fa; the fourth follow-up's high was patched)
- verdicts: 38 findings — high 0, medium 5, low 16, false 0, maybe-false 0, rejected-by-rule 17
- finishing rule (orchestrator): the story's promise is that no damaged file, no valid file and no press hangs or crashes the device; a finding reachable only by a crafted file inside a third-party decoder is fixed when the fix is a walk rule or a build-time library patch, and otherwise deferred with its exposure stated (the firmware's cover path decodes the same formats with the same libraries and no walk). The next pass classifies every finding by reachability and closes the story when nothing non-crafted remains.
- findings:
  - `[medium]` `[patch]` (fifth follow-up blind 5, edge 4; valid file, real) the decode watchdog is not fed by `HalFile` reads or the Storage lock, so a legitimate wait over 5 s (another task streaming a file, a slow card) resets the device during a decode, and the doc's claim that the card manager feeds it is wrong for this reader — the task leaves the watchdog around every blocking file call and rejoins after (or waits with a timeout and feeds between waits); the watchdog is fed on every PNG read that makes progress (crafted slow decodes) and the EOI search runs inside it with a larger buffer, backward from the end first; a missing or unconfigured watchdog is logged once at join time, and the x4pro build's generated sdkconfig is checked for the panic setting (recorded in the plan).
  - `[medium]` `[patch]` (fifth follow-up edge 3; valid file) the starvation tame turns restart markers (FF Dx) into 00 Dx on valid pictures with small restart intervals and tiny MCUs, flat line art first — the tame counts FF D0–D7 as kept and touches only FF FF runs, never FF 00 or FF Dx; a restart-interval line-art fixture decodes pixel-identical with and without the tame.
  - `[medium]` `[patch]` (fifth follow-up blind 2; ordinary file) PNGdec copies a PLTE or tRNS out of its 2 KiB buffer without refilling when the chunk straddles the buffer's end, so an indexed PNG with a long iCCP or text chunk before its palette reads past the decoder object — a fork-side PNGdec patch applied at build time as the JPEGDEC patches are (refill before the copy, or copy in two parts), plus a decoder test with about 1.5 KB of tEXt before PLTE under the sanitizers.
  - `[medium]` `[patch]` (fifth follow-up edge 1, blind 8; damaged file) a read-cap trip on open()'s last top-up read latched into decode, starving it — the latch and the open cap apply only to reads below the walked header end; a latched header failure returns `Open` after the open result; decode reads are never latched.
  - `[medium]` `[patch]` (fifth follow-up edge 2, blind 1; crafted, library patch) JPEGDEC's refill returns early for good once a large colour MCU pushes the bit offset past the high-water mark, and the pointer leaves the buffer — a fourth JPEGDEC patch ends the decode with an error in that case; colour fixtures (4:2:0 and 4:4:4 noise at quality 100) run every cut and stop, and the canary checks the bit offset against the decoder's own size, not the buffer.
  - `[low]` `[patch]` (fifth follow-up edge 6, blind 3, verification-gap 4) DC Huffman tables with index 2 or 3 write past the decoder's two DC tables — the walk refuses class-0 (and class-1) indices over 1; DC index 2 and 3 fixtures in the differential, and the DHT fixtures moved to ids the baseline does not redefine so the builder runs them.
  - `[low]` `[patch]` (fifth follow-up verification-gap 1, 5) the decode-phase assertion was met by open()'s last top-up and the progressive fixture never refilled inside decode; the EOI search's byte carry across reads was untested — `decodeReads > 0` asserted, a 512×512 progressive fixture, the carry's boundary case.
  - `[low]` `[patch]` (fifth follow-up verification-gap 2, blind 10) the handoff's publish-before-exchange order was unpinned and a hook in the window gets the inert answer even when the detach loses — the task-side assertion on a lost finish; the inert answers are neutral and the screen reads the task's own end after a join; pinned.
  - `[low]` `[patch]` (fifth follow-up verification-gap 3) the canaries cannot see header-parse over-reads inside the decoder object — a poisoned guard field after the file buffer in the build-tree copy (test-only layout), or the header comment narrowed to what is watched.
  - `[low]` `[patch]` (fifth follow-up edge 5) the EOI tail shortcut hands an appended JPEG (an MPF preview) to the decoder — the forward search for the first EOI after SOS is preferred.
  - `[low]` `[patch]` (fifth follow-up intent, blind 9, edge 7) `walkJpegSegments` keeps a 256-byte buffer on the sweep task's stack (frame 416 B) — a smaller or caller-provided buffer, frame under 256 B; the hand-kept copy of the read callbacks in the decoder test shares one header with `GrayImageReader.cpp`; stale comments (NoEnd, Format, the UBSan list); the `onEnter` deferral audited so no code reads the picture or row scratch before Run.
  - `[low]` `[reject]` (fifth follow-up intent) the readings restated; the story is amended at the merge.

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

- **Host suite:** `ctest` 2356 of 2356 passed after the sixth batch (2345 after the fifth, 2332 after the fourth, 2321 after the third,
  2298 after the follow-up batch, 2290 after the review batch, 2278 before it; dlab-g1's merge had 2,236). The display
  lab binary has 272 tests; `test_image_decoders` has 20, under AddressSanitizer and UndefinedBehaviorSanitizer (built
  here from `.pio/libdeps`, and by the fork CI's Display lab decoders job, simulated here with fresh clones; a
  checkout without the sources builds 2336).
- **Fast checks:**
  - `scripts/check_layers.py`: passed, 887 edges.
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
| Truncated or malformed file | `PngChunks.*` (9), `JpegSegments.*` (9, `EverySyntheticCase` covering 69 headers), `DecoderReads.*` (14), `ImageDecoders.*` (20, where the decoders' sources are present, and in the fork CI) |
| Previous/Back | `SweepControl.StopIsStickyAndGatesEntry` (existing) |
| Stop past its bound | the decisions in `TaskHandoff.*` (6, the inert answers' neutrality included); the watchdog's gaps in `DecoderReads.TheWatchdogIsLeftForFileCallsOnlyWhileTheReadMakesProgress`; the runner's wait, detach and reap themselves device-only (FreeRTOS); the awake decision in `SweepControl.RunKeepsTheDeviceAwakeAndAHoldOnlyForItsBound`; the stop the reads see in `ImageDecoders.AStopEndsEitherDecodeAndItsReads`, `PngdecReturnsAtAStopOnEveryRead`, `JpegdecReturnsAtAStopOnEveryRead` |
| Unreadable file | `ImageRows.SizeLimitsAreTheConverters`, `PngDepthsAreTheConverters`, `PngColourTypesThatDoNotExistAreRefused`, `PngBufferNeedIsTheConverters` (the reader's Format checks), `PngChunks.AFailedReadIsSaidSo`; the Status text is device-only |
| Dither | the `GrayQuantiser.*` tests at 4 and 7 levels |
| Fit | `ImageRows.FitKeepsTheAspectAndCentres`, `FitAlwaysInsideTheScreen`, `FitAxis*`, `TapsAreNearestWhenShrinkingAndBlendWhenEnlarging`, `BlockSpansPartitionTheOutput`, `RowsReadyAtPartitionTheOutputOnceTheirRowsHaveArrived`, `SampleBlockIs*`, `BlendsCrossStripAndBlockSeams`, `SeamsHoldForJpegdecShapedBlocks`, `SeamKeeperHandsOutTheRowAboveAndTheColumnLeft`, `JpegScaleAndOptionForAFit` |
| Levels | `GrayLevels.TheReadoutNamesTheEnginesLevels`, `GrayQuantiser.TheEnginesLevelCountIsUsed` |
| Session cap mid-paint | `GrayPictureHold.ASessionCapCleanMidMarkerPaintsAgain` |
| Cover, sleep, Home | the shared runner, so the transition test's existing `SweepControl` tests apply |

Device-only and not host-tested: the reader's glue (its `HalFile` adapter and the watchdog calls, the sink into the
model) and the runner's wait, detach and reap. The decoders themselves are host-tested through the reader's own
callback logic (`DecoderReads.h`, which both compile; `test_image_decoders`, here and in the fork CI).

Firmware figures: unmeasured (orchestrator builds after review). Not run by this agent: `pio run -e x4pro`,
`-e default`, `-e sticky`, `pio check -e x4pro`, and the flash gate with D6 (expect +0).

Residual risks:

- The viewer task's 12 KiB stack is an unmeasured estimate for the decode chain on the sweep task. The run's
  `gray sweep task ended` line measures it.
- A multi-second decode runs on the sweep task: a stop normally waits for one output row (at most 20 s, then the
  task left undeleted or detached), and the cover check only ends the run at the next scan. A detached task keeps
  its runner's PSRAM (the model, about 1.2 MB at 800x480) until it ends, and no run starts meanwhile.
- The task watchdog's 5 s is the core's. The task leaves it around every blocking file call, so a long wait on the
  Storage mutex is not a reset; a task stuck in such a call is not watched (the restart status covers it).
- The walk reads every JPEG forward from SOS to its first EOI (sixth batch: no tail shortcut), so the scan is read
  twice, once by the walk in 4 KB reads and once by the decoder: about a second more for a multi-megabyte photo on
  the SD card, Back working throughout.
- A MCU over JPEGDEC's 512-byte margin past its refill mark (valid 4:2:0 colour at quality 100 with noise-like
  detail; a crafted file of the longest codes) ends the decode in an error after one MCU's read past the data, inside
  the object (the fork's JPEGDEC patch 0004, every env): deferred, with its exposure.
- JPEGDEC 0004 lives in `scripts/jpegdec_patches`, which upstream's `patch_jpegdec.py` applies in every env: the
  release images' JPEGDEC changes by the patch (both of the flash gate's images carry it, so its delta stays +0).
- A detached task stuck inside an SD read holds the Storage mutex: the next card access after Home or sleep waits
  behind it for as long as that read lasts (reopening the lab freezes, sleep waits without a bound); a timed Storage
  lock on the sleep path is a follow-up outside this story.
- `PNGdec` is about 59 KB with the firmware's 16 KB row buffer; the free-heap floor is now its real size plus
  16 KiB (about 75 KB), stricter than the converters' 60 KB.
- The decoder libraries run on the host only in `test_image_decoders`, which upstream's unit-tests job does not
  build; the fork CI's Display lab decoders job does.

