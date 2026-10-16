### D2a. Four shades without dithering: how InkBoy does it

**Answer:** a gray is an interrupted transition. InkBoy models every pixel's position between black and white, pushes it toward the target shade with a counted number of one-frame pulses, then holds it; the counts come from a 12-entry table tuned by eye on one panel [1] (high confidence, code re-read).

**The physical trick.** One "dose" is one activation of the one-frame waveform, about 14 ms in grayscale mode. A pixel going from white to light gray gets two doses toward black and is then held with the 00 selector; black to light gray gets four; the full black-to-white swing gets seven [1]. The per-pixel level is a model from 0 to 192 (64 per shade), advanced after each delivered dose by the remaining distance divided by the remaining doses, so the last dose lands exactly on the target [1]. Each 33 ms slot can carry two scans: the drive scan and a follow-up that either drives again (endpoint targets, HOLD off) or holds (gray targets, HOLD on), which "ends the pulse promptly" [1].

**The dose table** (defaults, from black K, dark D, light L, white W) [1]:

| From, To | Dose | HOLD |
|---|---|---|
| K, D | 2 | ON |
| K, L | 4 | ON |
| K, W | 7 | OFF |
| D, K | 4 | OFF |
| D, L | 1 | ON |
| D, W | 6 | OFF |
| L, K | 4 | OFF |
| L, D | 1 | ON |
| L, W | 5 | OFF |
| W, K | 7 | OFF |
| W, D | 3 | OFF |
| W, L | 2 | ON |

The asymmetry is the finding: darkening and lightening the same two shades need different counts, which is why the transcript calls out 12 distinct transitions [5]. A pixel retargeted mid-transition gets a dose interpolated between the two neighbouring shades by its current level, scaled by a retarget percentage; an optional per-pixel history record (origin shade and steps since) corrects six round-trip routes with a return delta and a reversal percentage, defaulting to one fewer dose on L to K to L returns and 150 percent on reversals [1]. A "weak" white-to-light mode drives eligible pixels with VSH2 at 11 or 8.8 V instead of VSH1, which "can fix a dark stain behind a moving white object" [1].

**Memory and compute at 200x200.** Two bytes per pixel of state (level, remaining doses, target, source) plus masks and previous frames, about 100 KB in internal RAM with PSRAM fallback; an optional 120 KB history array in PSRAM; a 20 KB precomputed pulse table; and the planner only re-walks rows whose input changed, split across both cores with identical results verified by a differential host test [1]. Scaled to the X4 Pro's 384,000 pixels that is roughly 770 KB of pixel state and 1.15 MB of history (derived), which only PSRAM can hold.

**What keeps it stable.** Unchanged pixels are never in either mask, so they receive no drive; counters advance only after an acknowledged scan; a hold-only scan ends each burst; a forced full refresh after 2,400 partials and a lifetime cap of 2,402 per session; cleaning refreshes on mode change, game entry and exit [1]. The author's own verdict: it "still leaves faint trails after some shade changes", pausing and restarting a game re-establishes the reference, and the defaults are "the author's own tuning for the author's panel. Panels vary" [1].

**Calibration as shipped.** A transition-test screen shows four shade bands with three 12-pixel rectangles of the other shades sweeping across each for 5 s; the reader judges rectangle shade against background (dose), the track behind it (history), build-up over passes, endpoints and motion blur, changing one value per run; doses and HOLD first, then history routes, then the lightening voltage together with the doses; then a game with thin fast sprites [1]. Defaults are exported from firmware to a JSON preset and a test fails on drift; the planner has a frozen reference implementation and a 9,264-case exhaustive equivalence test [1]. There is no photo, video or reflectance ground truth in the repository; the transcript's photo-and-video rig is the only mention of it [1][5].

**For the game system.** The method is a per-pixel state machine over a two-plane selector, nothing SSD1681-specific except the LUT byte offsets, the frame-rate nibble and the 5,000-byte planes [1]; D3 shows the SSD1677 has the same selector and a 112-byte LUT with the same four voltage rows [31]. The port cost is the dose table, which must be re-tuned per panel, and the memory, which forces PSRAM.
