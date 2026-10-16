# Story sdk-seam: the display-lab API in freeink-sdk (fork branch `display-lab-seam`, upstream PR)

Repository: the fork `CarpeTelam/freeink-sdk`, local clone `/home/user/freeink-sdk`, branch `display-lab-seam` from
`32a265fa` (the commit crosshatch-player's submodule records). This is SDK work, not crosshatch work: follow the SDK's
own conventions (its README "Architecture" section, `docs/testing.md`, and `CLAUDE.md` if present), not crosshatch's
AGENTS.md. Epic context: `_bmad-output/initiative-crosshatch-player-v1/epic-display-lab/epic-display-lab.md` in
`/home/user/crosshatch-player` (read-only; R1, D1-D3 explain what the consumer needs).

## Intent

**Problem:** A firmware bench needs to drive a caller-supplied waveform with caller-supplied raw image planes on the
UltraChip X4 Pro batches (UC8179, UC8279 via `Uc8279X4Driver`), as the SSD1677 driver already allows through its
overlay-grayscale path. On the UltraChip drivers `setCustomLut` is a no-op, `displayGray` ignores its `lut`
argument, `copyGrayscale*` folds the planes with the driver's base image, and the PLL frame-rate register is a
compile-time constant, so no caller can reach the controller's waveform registers.

**Approach:** Add an opt-in "display lab" API, compiled only under `FREEINK_CAP_DISPLAY_LAB` (default 0, so stock
builds carry no code), to `PanelDriver` (virtuals with safe defaults) and `FreeInkDisplay` (thin forwarding), and
implement it on `Ssd1677Driver`, `Uc8179Driver` and `Uc8279X4Driver`:

```cpp
// PanelDriver (all default to "unsupported" / no-op)
virtual bool labSupported() const { return false; }
// Controller-specific waveform bytes: SSD1677 = the 112-byte LUT (bytes 0-104 via 0x32, 105 via 0x03, 106-108 via 0x04, 109 via 0x2C);
// UltraChip = five register-prefixed tables of 43 bytes (0x20 VCOM, 0x21 WW, 0x22 KW, 0x23 WK, 0x24 KK), uploaded with PSR REG=1.
virtual bool labUploadWaveform(EpdBus& bus, const uint8_t* bytes, size_t len) { return false; }
// Frame-rate code: UltraChip writes PLL (0x30); SSD1677 returns false (its rate lives in the LUT bytes).
virtual bool labSetFrameRate(EpdBus& bus, uint8_t code) { return false; }
// One activation with raw full-screen planes in the framebuffer's byte order (no inversion, no folding with any base;
// the driver applies only its gate padding / row order mapping): SSD1677 writes plane1 to 0x24 and plane2 to 0x26,
// then 0x21=0x00, 0x22=0xCC, 0x20 and waits BUSY; UltraChip writes plane1 to DTM1 (0x10) and plane2 to DTM2 (0x13),
// PON if needed, DRF (0x12), waits BUSY, and leaves power on. busyUs = microseconds from activation to BUSY release.
virtual bool labScan(EpdBus& bus, const uint8_t* plane1, const uint8_t* plane2, uint32_t& busyUs) { return false; }
// Leaves the lab: restores the driver's registers to its config (PSR REG bit, CDI, PLL, TSSET) and marks its state so
// the next ordinary refresh does a full resync (SSD1677: _needsGrayClear; UltraChip: _needFullClear / old plane invalid).
virtual void labEnd(EpdBus& bus) {}
```

`FreeInkDisplay` gets the same four plus `labSupported()`, forwarding with its usual `DisplayPmLock` and
`syncPendingAsync()`, refusing while inverted (return false), and invalidating its shadow/RED-sync flags as
`displayGrayBuffer` does. `EpdBus` needs no change unless timing requires a BUSY-release timestamp helper; prefer
measuring around the existing `waitRefreshComplete`.

## Boundaries

- Zero footprint when `FREEINK_CAP_DISPLAY_LAB` is 0: every declaration and definition inside `#if`.
- No change to any existing public method's behaviour; no change to stock refresh sequences, configs, or LUT tables.
- Never emit an OTP/MTP programming command (SSD1677 0x30; UltraChip OTP program commands); the lab uploads to
  volatile registers only, and `labEnd` returns PSR to the config's OTP/REG selection.
- Voltages: the lab never writes gate/source voltage or VCOM registers on UltraChip; on SSD1677 the LUT bytes
  105-109 are written as the caller supplies them (the caller is responsible for stock values), as the existing
  overlay path already does.
- Plane geometry: use each driver's existing plane streaming helpers (`streamPlane` with inversion off on UltraChip,
  the SSD1677 window/counter setup) so a caller's framebuffer-order plane lands where the ordinary refresh would put
  the same bytes; document any row reversal or gate padding the driver applies.
- Keep UltraChip power on between scans (no POF) and PSR REG=1 while the lab is active; `labEnd` restores.

## Tests

- Extend the recording-bus host suites: `libs/display/FreeInkDisplay/test/host/run_pro.py` (SSD1677 facade) and
  `run_uc8279.py` (UltraChip), plus a UC8179 harness if the suites compile that driver (check `test/host/*.py` and
  `pro_stubs/`); assert the exact command/data sequences of `labUploadWaveform`, `labSetFrameRate`, `labScan` and
  `labEnd` on each driver, that stock sequences are byte-identical with the flag off and on, and that
  `labScan` refuses before `labUploadWaveform` on UltraChip (REG bit not yet set) or documents that it does not.
- All existing display suites pass with `FREEINK_CAP_DISPLAY_LAB` unset and set.

## Deliverables

- The code and tests on branch `display-lab-seam`, one or a few clean commits with messages that explain the API for
  the upstream reviewer; a short section in the SDK README (or `docs/`) describing `FREEINK_CAP_DISPLAY_LAB` and the
  API; the commit messages end with the attribution lines this session uses.
- Do not push and do not open the upstream pull request; the orchestrator does both after reading the diff.
- Final message under 150 words: the commit hashes, the test commands run and their results, the register sequences
  implemented per driver, and anything left open.
