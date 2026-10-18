# Story dlab-g3: the gray viewer (lane G, epic branch)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R3, R4,
Boundaries, Decisions D4, D5, D10, D13, D14, D15 and D16; the Story results for dlab-c4 and dlab-g1). Ref: `dlab-g3`.
Plan: `_bmad-output/implementation-artifacts/plan-dlab-g3-gray-viewer.md`. Builds on `dlab-g1` (the Ladder's paint,
settle, hold sequence on the sweep task; `GrayModel::driveRect`), `dlab-c4` (the hold and its pause-symbol marker)
and the firmware's image decoders (`lib/Epub/Epub/converters`: `ImageDecoderFactory`, `PngToFramebufferConverter`,
`DitherUtils.h`). Before `dlab-g2` it shows four shades; after it, seven, with no change of its own.

## Intent

**Problem:** The owner wants to see how the engine's shades look on real artwork, line art first (D16), not only on
bands and rectangles. Nothing in the lab paints a picture.

**Approach:** A **Gray viewer** screen in the lab menu (D10's list, after the transition test), behind
`CROSSHATCH_DISPLAY_LAB`, that paints a picture through the gray engine on the panel:
1. **Sources.** PNG and JPEG files in `/.crosshatch/display-lab/images/` on the SD card (listed by name, any size,
   scaled to fit the logical screen keeping the aspect, letterboxed in white), and a built-in **test chart** drawn by
   pure code when the folder is empty or as the first entry: a horizontal ramp of the engine's levels, a continuous
   gradient, hatching at three pitches, concentric circles and a few diagonal strokes, so the viewer works with no
   files and the chart is the same on every device.
   The repo's `docs/crosshatch/display-lab-images/` holds four generated 480 × 800 PNGs (ink line art, hatch pitches
   and weights, gradients and halftone, text) as the fixed set the owner copies to the folder; they are not compiled
   into the firmware.
2. **Decode.** A file decodes through the existing decoder into the renderer's grayscale framebuffer (as a cover
   does, `useGrayscale`), which is never shown (the view owns the panel, D4); the framebuffer's gray is read back
   into the model's targets through a pure `Quantiser`: **plain** (nearest level) or **ordered** (the 4×4 Bayer
   matrix, scaled to the level count) per the **Dither** row; the framebuffer is cleared afterwards. The chart is
   written into the targets directly.
3. **Paint.** Run cleans the panel to white, paints every pixel's target through `GrayModel::retargetRect` (one
   retarget per run of equal targets per row), lets the run settle (no pixel active for a slot) and holds with the
   pause marker (dlab-c4's mechanics; the viewer always holds, as the Ladder does). Logs `gray viewer: <name> painted
   after N slots, M pixels per level [...]`. Next paints the next image (clean first); Previous or Back stops with a
   FULL refresh. A **Levels** readout row shows the engine's level count and names.
4. **Rows.** Image (the current entry; Confirm or tap steps), Dither (Plain / Ordered), Run, Levels (readout),
   Controls, Status, as the transition test lays its rows out.
The slot runner, the task, the hold and the marker are the transition test's: extract what the viewer needs into a
shared piece (a `GrayRunner` the two activities use, or a base class) rather than copying it; the plan says which
and why. The quantiser, the chart generator, the fit-to-screen scaling and the per-row run splitting are pure in
`lib/GameDisplayLab` and host-tested.

## Boundaries & Constraints

**Always:** every scan still goes through `DisplayLabActivity::scan` on the sweep task; D4 and D5 unchanged; no
drawing through the renderer while the view is up (the framebuffer is a decode target only); the preset file,
`GrayPreset`, the Tuning screen and the other lab screens unchanged; the level count read from the engine
(`SHADE_COUNT`), never hard-coded, so dlab-g2 changes nothing here; the model's 3 B/pixel budget unchanged (no
second full-screen buffer beyond the renderer's own); locals under 256 B; `tr(STR_…)` keys in `english.yaml` only;
C3 `default` and `sticky` still build; the flash-budget gate (D6) unchanged; a decode failure logs and shows the
Status row's reason, the chart still works.

**Never:** no change to `ScanBackend.h`, the backends, `DisplayLabActivity::scan`, the SDK seam or `freeink-sdk/`; no
change to the decoders or `DitherUtils.h` beyond what reading the framebuffer back needs (prefer none; a new helper
beside them is fine); no new ledger row; no edit of generated I18n files; no change to the stock bench or the
frame-period and polarity screens; no change to the transition test's behaviour (the shared extraction is
behaviour-preserving and its tests still pass).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| No folder, no files | `/.crosshatch/display-lab/images/` absent | the Image row offers the chart only | — |
| Files present | PNG/JPEG files | listed by name after the chart; other files ignored | an unreadable file: Status says so, the next entry still works |
| Run | Run row | clean, paint, settle, hold with the marker; `gray viewer: … painted` | a scan failure ends the run as the transition test does |
| Next while holding | NavNext / Confirm | next entry: clean, paint, settle, hold | at the last entry: back to the chart |
| Dither | Dither row | Plain ↔ Ordered; the next Run uses it | — |
| Fit | image larger or smaller than the screen | scaled to fit, aspect kept, white letterbox | — |
| Levels | any engine | the readout names the engine's levels (4 today, 7 after dlab-g2) | — |
| Session cap mid-paint | cap reached | the cap clean runs; the paint restarts, logged | — |

## Verification

- Host suite, fast checks and formatting as in `story-dlab-0-backend-seam.md`; tests for the quantiser (plain and
  ordered at 4 and 7 levels), the chart (deterministic pixels, every level present), the fit scaling and the run
  splitting; the transition test's tests unchanged after the extraction.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`,
  `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro`; the flash-budget gate with D6 (expect +0).
- Hardware (owner, experimental branch): the chart paints and holds; a line-art PNG from the SD card paints plain and
  ordered; photos of both for the record.
