# Story dlab-0: the scan backend seam (lane 0, serial)

Epic: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` (read it fully: R1, R2, R5,
R6, Boundaries, Decisions D1-D4). Ref: `dlab-0`. Plan: `_bmad-output/implementation-artifacts/plan-dlab-0-backend-seam.md`.

## Intent

**Problem:** Part A's scan primitive is a method on `DisplayLabActivity` that knows only the SSD1677 facade path and
takes a uniform fill byte per plane. The UC8179 and UC8279 benches and the grayscale engine (Part B) all need the same
primitive with per-pixel masks and a controller-specific encoding, and three lanes cannot edit one method in parallel.

**Approach:** Introduce `ScanBackend` (epic D1) and `ScanRequest`, move the SSD1677 path into `Ssd1677ScanBackend`
with its pure encoding in `lib/GameDisplayLab/Ssd1677Encoding.{h,cpp}` (two masks to BW/RED plane bytes, D2), add a
pure `MaskPlanes` helper in `lib/GameDisplayLab/` (two full-screen 1-bpp masks: clear, fill, set a rectangle, set a
pixel; byte order documented and tested), and make the frame-period and polarity screens use `ScanRequest` with
full-screen masks through the backend. `DisplayLabActivity` chooses the backend by `display.getController()` at
`onEnter` (only SSD1677 exists in this story; UC8179 and UC8279 return "no backend" and the launcher row stays
hidden for them, as now) and keeps the warning, session cap, timeout, dropped-scan floor and exit refresh, now in a
controller-neutral `ScanBudget`-backed wrapper around `backend->scan()`. Behaviour on an SSD1677 unit is unchanged.

## Amendment D1a (orchestrator, 2026-10-16, after the first review pass)

`ScanBackend` also exposes `const char* rowLabel(uint8_t row) const` (rows 1 and 2): short text naming what that
row drives on this controller (SSD1677: "LUT1 VSL" and "LUT2 VSH1", or the swapped codes when `swapPolarity`).
`PolarityActivity` builds its two row labels from it and logs `vs=<rowLabel>` instead of a VsCode name; no
SSD1677-specific string remains outside the SSD1677 backend. Apply this in the follow-up pass with the review's
own fixes.

## Boundaries & Constraints

**Always:** the public shape of `ScanBackend`/`ScanRequest` exactly as D1 (later lanes build on it; a change to it
is an intent gap, HALT); every pure part host-tested; the existing `test_scan_lut` and `test_scan_budget` suites keep
passing; `STR_GAMES_DLAB_*` keys only; the C3 `default` and `sticky` envs build with the lab compiled out; AGENTS.md
memory and lock rules.

**Never:** no change to what the SSD1677 backend sends the panel (the same LUT bytes, the same 0x22 value, the same
plane bytes for the same request); no UltraChip code in this story; no new ledger row; no edit to `freeink-sdk/`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Backend choice | controller SSD1677 | `Ssd1677ScanBackend`, launcher row shown | — |
| Backend choice | controller UC8179 or UC8279 | no backend; row hidden; lab (if reached) shows the not-supported notice | no scan sent |
| Frame-period scan | all-hold request (both masks clear), frames n, rate i | SSD1677 planes both 0x00; same bytes as Part A | — |
| Polarity scan | whiteMask all set, blackMask clear | BW plane 0xFF, RED 0x00 (row 1); swapPolarity swaps the LUT row codes, not the planes | — |
| MaskPlanes rect | set rectangle (x, y, w, h) on a cleared mask | exactly those pixels set, byte order = framebuffer's (MSB first per byte, row-major, 100 bytes per row at 800 px) | out-of-range rectangle clipped |
| Timeout / cap / dropped / inverted | as Part A | identical outcomes and log lines | as Part A |

## Code Map (from the orchestrator's investigation; verify in the tree)

- `src/activities/games/display_lab/DisplayLabActivity.{h,cpp}` — the current primitive (`scan(lut, bwFill, redFill, busyUs)`), the gate, planes, budget; refactor target.
- `src/activities/games/display_lab/FramePeriodActivity.{h,cpp}`, `PolarityActivity.{h,cpp}` — callers; keep their screens and log lines.
- `lib/GameDisplayLab/Ssd1677ScanLut.{h,cpp}`, `ScanBudget.h` — pure parts to keep; `test/game_display_lab/` — add `test_mask_planes.cpp` and `test_ssd1677_encoding.cpp` to the existing `test_scan_lut` target or a sibling target in `test/game_display_lab/CMakeLists.txt`.
- `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h` — `copyGrayscaleBuffers(lsb, msb)` (raw bytes to 0x24 and 0x26), `displayGrayBuffer(turnOff, lut, factoryMode)`; reached via `display.eink()`.
- `lib/GfxRenderer/GfxRenderer.h` — `getDisplayWidthBytes()`/`getBufferSize()` for the mask geometry; `scripts/check_layers.py` TABLE already lists `lib/GameDisplayLab` (C++ standard library only) and Screens may include it.
- `docs/crosshatch/display-lab.md` — update the code map section.

## Verification

- Host: `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'`; `python3 scripts/check_upstream_touches.py`; `python3 scripts/check_layers.py`; every `scripts/*_test.py`; `./bin/clang-format-fix` twice.
- Firmware (after review, under the build lock, with `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache`): `pio run -e x4pro`, `pio run -e default`, `pio run -e sticky`; `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`; `scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects` (record the figures against the epic's base measurement).
- Hardware: none (the owner's unit is a UC8179; the SSD1677 behaviour is pinned by the byte-level tests).
