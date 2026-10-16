---
title: 'technical research: fast e-paper display driving for X4 Pro'
type: 'technical'
topic: 'fast e-paper display driving for X4 Pro'
decision: 'Whether and how crosshatch-player should build a custom fast, per-pixel-driven e-paper display pipeline (short waveform, per-pixel drive budget, partial-transition grayscale) for the X4 Pro and its other targets, learning from Ink-boy and kuslota/InkBoy'
source: 'native run (web fan-out) plus two imports: the kuslota video transcript and the freeink-sdk SSD1677 driver baseline'
status: complete
preset: 'standard'
validation: 'normal'
created: '2026-10-16'
updated: '2026-10-16'
claims_verified: 6
claims_unverified: 7
claims_overturned: 0
---

# technical research: fast e-paper display driving for X4 Pro

**Decision this research serves:** Whether and how crosshatch-player should build a custom fast, per-pixel-driven e-paper display pipeline (short waveform, per-pixel drive budget, partial-transition grayscale) for the X4 Pro and its other targets, learning from Ink-boy and kuslota/InkBoy.

## Executive summary

**What the evidence says to do.** Build InkBoy's technique, not Ink-boy's: an opt-in, SSD1677-only experimental fast mode that loads a one-frame waveform through the volatile LUT command, uses the two RAM planes as a per-pixel hold/black/white selector, keeps a per-pixel drive budget in PSRAM, and re-uploads only the changed window each scan. Do it as a short measurement spike first, then an epic, and never as a replacement for the stock paths.

Three findings drive that answer:

1. **The X4 Pro's SSD1677 has the structure the technique needs.** The datasheet keys each pixel's waveform on the pair of RAM-plane bits into four independently writable LUT rows, accepts one-frame phases, and runs a loaded LUT when the update sequence skips the OTP load; Waveshare's own 4-gray mode on the same 4.26" panel is that exact mechanism [31][32]. The fork's SDK already uploads a 112-byte LUT, activates it with 0xCC, refreshes asynchronously, and writes windowed RAM [9]. The missing pieces are the per-pixel scheduler and the calibration, which InkBoy publishes under MIT [1].
2. **The floor is unknown and cheap to measure.** No source gives the SSD1677 frame period; the datasheet omits it and the family's 5 to 67 ms per frame range cannot be assumed [31][33]. One afternoon with a one-group LUT and a BUSY timer answers it. The SPI side is known: two 48 KB planes take 38 ms at the datasheet's 20 MHz or 19 ms at the 40 MHz the driver already runs [24][31][9], so a whole-screen 30 scans per second is out of reach at 800x480 and the realistic target is a game or UI window.
3. **Ink-boy teaches lineage, not technique.** It is a 2026-10 copy of CrossInk, its fast preset is gated to UC8279 variant 0x68, its self-reported 276 ms per refresh is slower than the fork's existing 77 to 85 ms DU path on SSD1677, and the driver code was never published [7][9][30].

**Biggest caveat.** Panel damage from short, DC-unbalanced waveforms is documented as a possibility by a peer-reviewed paper and E Ink's patents, but nobody quantifies it [15][16][17]. InkBoy's author has not validated lifetime, panels vary unit to unit, and the fast LUT ignores temperature [1][6][18]. Every safety pattern InkBoy uses (volatile LUT only, stock voltages, session caps, periodic clean, opt-in) is cheap and should be copied before the first scan runs.

## D1. Ink-boy: lineage, what it does, and where its speed comes from

**Answer:** Ink-boy is not an ancestor of CrossPoint. It is a copy, made on 2026-10-06, of uxjulia/CrossInk v1.6.1, itself a CrossPoint Reader fork (MIT, Dave Allie copyright), with 18 commits by one author on top through 2026-10-15; GitHub does not register it as a fork, it has 8 stars, no forks, and no issues [7] (high confidence). It bakes Peanut-GB and Walnut-CGB emulators into the CrossInk file browser for the X4 Pro only, and its code acknowledges the same three controller batches this fork handles: SSD1677, UC8179, UC8279 [7].

**Where the speed comes from, as far as it is public.** The "faster screen" commit (tag 2.5.0, 2026-10-13) adds a single refresh preset: a host-loaded DU waveform taken from "the stock X3 UC8279 bank", scaled to 40 percent of its frames, the PLL frame-rate register at 0x0F, the display SPI raised from 10 to 20 MHz, a changed-rectangle-only send and drive with unchanged frames skipped, and overlapped (asynchronous) refreshes [7]. The code comment reports roughly 276 ms per refresh against about 680 ms for the stock Fast waveform, "with the darkness of stock and a little ghosting" cleared by a periodic 1.6 s cleaning refresh, measured on one unit [7] (self-reported, unverified). The preset is enabled only when the detected controller is UC8279 variant 0x68; SSD1677 and UC8179 units keep stock [7] (verified by re-reading the source). So nothing in Ink-boy validates fast driving on the SSD1677 batch, which is the controller this fork's X4 Pro documentation treats as original.

**The driver itself is not published.** The commit points the SDK submodule at a commit that does not exist in upstream Free-Ink/freeink-sdk, in uxjulia's SDK fork, or in any public repository of the author or of "Thebitsworld", whom the README credits for the LUT; 26 minutes later the pointer was reverted to upstream, which lacks every method the activity calls (`setFastRefreshTuning`, `setFastRefreshDuScale`, `setFastRefreshDiffWindow`, `lastRefreshTimings`, `setDisplaySpiHz`, `displayBufferAsyncNoShadow`) [7][8][30] (high confidence). The HEAD tree cannot build as published. The comment's "1.8.1 betas" is not a CrossInk version (CrossInk tops out at 1.6.1) and the header self-references another project, so the driver was written in a private parent tree [7][29]. The LUT bytes, the frame-scaling rule, the diff-window code, and the timing log are therefore unrecoverable without asking the author.

**A contradiction worth noting.** The README says the custom LUT "drives the e-ink panel outside of its normal operating parameters" and may shorten lifespan; the code comment says drive levels and voltages stay stock and only the frame count and PLL change [7]. Both are the author's words; without the SDK commit neither can be checked.

**Controller-level versus panel-specific.** Portable to any SSD1677 or UC82xx unit: overlapped refresh with the BUSY wait deferred, a changed-rectangle window plus skip-unchanged, differential two-plane updates, SPI at the controller's rated clock, and forced temperature or frame rate [7][8]. Panel-specific: the DU LUT bank itself, the 40 percent frame scaling, PLL 0x0F, the cleaning interval, and VCOM tweaks. Upstream's own SDK is still tuning this last item: its 2026-10-12 commit moved the SSD1677 custom-LUT VCOM byte after ghosting on the X4 [8]. This fork's SDK already has the portable items for the SSD1677 (asynchronous refresh, two-plane differential update, windowed RAM writes, a 0x1C DU path at roughly 77 to 85 ms, custom LUT upload) and the same UC8179 TSSET lever [9].

## D2. kuslota/esp32-gameboy and the InkBoy driver: what the video's method actually is

**Answer:** the driver the video promises is already public. It is not in `esp32-gameboy`, which holds only the Peanut-GB core, storage, and USB gamepad host [2]; it is the sibling repository `kuslota/InkBoy`, released in one commit on 2026-10-12 with the controller gamepad as a third repo [1][3]. The video is "GameBoy on E-Paper ESP32 at 30FPS" on the channel Bo Made It, which reached Hacker News on 2026-10-11 [4][6]. The transcript's numbers (17 steps to 2, roughly 50 ms, a 6 to 8 drive budget, 33 ms scans) match the code: a 17-frame stock partial LUT replaced by a one-active-frame LUT, an 8-pulse default budget, and 33,334 µs image slots [1][5] (high confidence, primary source re-read).

**Hardware.** Waveshare ESP32-S3-ePaper-1.54 V2: a 200x200 panel on an SSD1681 controller, which the author cannot bypass [1]. This is the same controller family as the X4 Pro's SSD1677, not a bare panel.

**The method, as implemented** (all from the repository, high confidence) [1]:

- Stock partial update costs about 360 ms of controller time. InkBoy loads a one-active-frame waveform through the volatile LUT command 0x32, so one scan drives for roughly 20 ms in dithered mode and 14 ms in grayscale mode; it keeps the analog supply on between scans, saving about 90 ms per update; and it sends both 5,000-byte planes at 20 MHz.
- Per-pixel driving goes *through* the controller: the two image RAM planes (0x24 and 0x26) act as a per-pixel 2-bit selector into the LUT's voltage rows, with 00 = hold, 10 = drive black, 01 = drive white. Each scan uploads a black mask and a white mask; an unmasked pixel receives no drive at all. That is how stationary areas stay clean.
- Dithered mode keeps a 4-bit counter per pixel (four bit-planes, 25 KB of internal SRAM for 40,000 pixels). A pixel whose target changes reloads its budget (default 8 pulses each way); the counter decrements only after a delivered scan; when the last pixel finishes, a final all-hold scan is sent.
- Scheduling is slot-based: an image slot every 33.334 ms anchored to a fixed clock, missed slots skipped rather than queued, the latest frame taken from a three-buffer handoff; the emulator runs at 59.7 fps on the other core.
- Four grays without dithering come from stopping a transition partway. Each of the 12 transitions has its own dose (1 to 12 pulses); the default table from black/dark/light/white is K: -,2,4,7; D: 4,-,1,6; L: 4,1,-,5; W: 7,3,2,-. A per-pixel model tracks an estimated level 0..192, interpolates doses for interrupted transitions, and optionally corrects six round-trip routes. The state lives in PSRAM with working sets copied to internal RAM and rows split across both cores.
- The LUT is Waveshare's stock partial table patched in RAM (frame counts, frame-rate code, VSL from -15 V to -9 V, VSH2 for "weak" modes). The OTP command 0x30 is excluded by a build-time safety check, so nothing it does is permanent.

**The calibration process.** The firmware ships a "Transition test" screen that sweeps all 12 transitions as rectangles across four shade bands, a live tuning menu that changes one value at a time without a full refresh, JSON presets on the SD card, and an FPS counter; the README asks contributors for comparison photos and videos [1]. The transcript adds the photo-and-video rig and the many iterations, which the repository does not describe [5] (single source). The author states the defaults are his own tuning for his own panel and that panels vary [1]. Hacker News questions about per-panel calibration and temperature drift went unanswered; the fast LUT does not read the temperature sensor [1][6].

**Reported trade-offs** [1]: more pulses give deeper contrast and ghost clearing but softer motion; fewer give sharper motion and more ghosting; grayscale still leaves faint trails; a dark stain behind moving white objects needs a "weak" mode; heavy scenes drop below 30 fps; long-term panel life is not validated. The driver enforces a cap of 2,402 partial updates per session and a cleaning full refresh every 2,400 unless "continuous play" is enabled.

**What transfers.** The counter-per-pixel idea comes from ZephRay's PaperBoy, which drives raw panel signals; InkBoy's contribution is the adaptation to a controller that only accepts image RAM plus a LUT [1]. Whether the SSD1677 exposes the same plane-to-LUT-row selector is the gating question for the X4 Pro, taken up in D3.

## D3. Prior art and what the X4 Pro's controllers permit

**Answer:** every fast per-pixel e-paper precedent except InkBoy drives a bare panel; the X4 Pro cannot do that. But the SSD1677 datasheet shows the controller has the exact structure InkBoy exploits on the SSD1681, so the technique ports in principle. What no source gives is the frame period, which sets the floor.

**Bare-panel prior art, and why it does not transfer directly.** ZephRay's PaperBoy on the M5Stack PaperS3 reaches 30 to 60 fps by driving the 960x540 panel's raw row and column interface and skipping the waveform method entirely [14] (secondary reporting, medium confidence). His Glider README describes the fundamental per-pixel method: scan the TFT array at 50 to 120 Hz, push each changed pixel toward white or black, count frames and stop after a set period, and never drive pixels that did not change [12]. epdiy does the same with per-panel frame-time curves, a 4 to 5 frame binary mode, and temperature-dependent grayscale [13]. The same README rates integrated-controller panels at 100 to 300 ms per black and white partial update with total latency "from 100ms to several seconds" because of the host-to-controller data path [12]. The X4 Pro's panel has a controller on every batch, so this fork's only route is through the controller's LUT, as InkBoy did. The SSD1680 family that InkBoy's SSD1681 belongs to exposes the same 0x32 LUT command with 2-bit voltage-select phases, and Solomon lists the SSD1677 as having a programmable waveform with on-chip OTP [26]; the mechanical differences from the SSD1681 are 10 phase groups instead of 12, five-byte timing rows, and 105 LUT bytes instead of 153 [35].

**What the SSD1677 exposes** (Solomon Systech datasheet Rev 1.0, high confidence) [31]:

- A 112-byte waveform set: five LUTs of 10 groups with four 2-bit phases each (VSS, VSH1, VSL, VSH2), ten groups of frame counts TP A to D and a repeat count RP, five frame-rate bytes, then gate voltage, VSH1, VSH2, VSL and VCOM. Command 0x32 writes the first 105 bytes; voltages go through 0x03, 0x04 and 0x2C. This is byte-for-byte the layout the fork's SDK already uploads for its grayscale LUT, down to the same voltage tail Waveshare ships [9][32].
- Per-pixel LUT selection keyed on the pair of RAM bits (RED plane, BW plane): 00 to LUT0, 01 to LUT1, 10 to LUT2, 11 to LUT3, with LUT4 driving VCOM. The datasheet's "LUT2 = LUT0, LUT3 = LUT1" note for black and white panels is a waveform convention, not a hardware limit: Waveshare's 4-gray mode fills all four rows differently and activates them with 0x22 = 0xC7, which skips the OTP LUT load [31][32]. That is InkBoy's selector (00 hold, 10 black, 01 white) in the SSD1677's own terms.
- Frame counts of 0 skip a phase and 1 is one frame; no minimum is stated, so a one-frame LUT is inside the documented ranges [31] (medium confidence on legality, by absence of a stated minimum).
- Command 0x22 sequences decode as a stage mask: bit 4 loads the OTP LUT and overwrites a 0x32 LUT, so 0xF7, 0xFF, 0xFC, 0xD7 and the fork's 0x1C DU path all run the OTP waveform; 0xC7 and 0xCC run the loaded LUT, and 0xCC keeps clock and analog alive between scans, which InkBoy counts as worth about 90 ms per update [1][31]. The fork's driver already uses 0xCC to activate its custom grayscale LUT [9].
- Command 0x37 selects "display mode 2" per waveform set and offers a "RAM ping-pong" for mode 2; the datasheet does not say how mode 2 combines the planes beyond the LUT-index table [31].
- The datasheet rates the SPI write interface at 20 MHz maximum [31]; the fork's SSD1677 driver defaults to 40 MHz [9], twice the rating.
- What the datasheet does not give: any Hz or ms for the frame-rate bytes, no frame-rate table, and the gate-line-width commands 0x3A and 0x3B are reserved. The frame period at 800x480, and so the shortest one-frame update, is unevidenced [31]. The SSD1675 of the same family is adjustable from 15 to 200 Hz, 5 to 67 ms per frame, but that must not be assumed for the SSD1677 [33] (low confidence as a proxy).

**The UC8179 and UC8279 batches.** UltraChip parts keep the waveform in registers 0x20 to 0x24 as VCOM, WW, KW, WK and KK tables of 42 bytes, keyed on the old and new pixel values, with a PLL frame-rate register 0x30 and a panel-setting bit choosing register versus OTP LUTs [11][27]. GxEPD2's partial LUT for the 800x480 GDEY075T7 already sets WW and KK to zero and drives only KW and WK, which is the same "hold unchanged pixels, drive changed ones" selector expressed as old/new pairs [11]. The fork's UC8179 driver uploads 42-byte register LUTs and uses TSSET as the frame-rate lever, and the upstream UC82xx drivers run differential DU with "promote to full every N refreshes" against ghosting [8][9]. So the per-pixel-budget emulation exists on all three batches, in two register dialects, each needing its own panel-tuned tables.

**Timings on this exact panel class.** GxEPD2 measured 1,567 ms full and 500 ms partial on the GDEQ0426T82 [10]; Waveshare quotes 4 s full and 0.7 s partial and comments its temperature-forced fast init at 1.5 s [32][34]; the fork measures 559 ms fast with the stock sequences and about 77 to 85 ms with the opt-in 0x1C DU path [9]. Reseller figures of 0.42 to 1 s partial are inconsistent and low confidence [25]. No published ghosting-versus-frame-count measurement exists for any of these controllers.

## D4. Implementation reality on the ESP32-S3: floors, risk, and what teams report

**Throughput floor.** No measured SPI figure for a 48 KB e-paper frame on the ESP32-S3 was found; only arithmetic is available [24] (low confidence as a measurement). Wire time for one 800x480 1-bpp plane is 19.2 ms at 20 MHz and 9.6 ms at 40 MHz, doubled for both planes; the ESP32 family limits SPI to 40 MHz through the GPIO matrix. The X4 Pro driver already runs its SSD1677 bus at 40 MHz by default [9]. Scaled from InkBoy, one full two-plane upload on the X4 Pro is 19 to 38 ms of wire time before any waveform runs, which alone caps a whole-screen scan near 25 to 50 scans per second; the per-pixel counter store grows from 25 KB to about 240 KB (four bit-planes plus target over 48 KB) [1][9] (derived).

**Panel-side floor on controller parts.** No SSD16xx or UC81xx project reporting a sustained update rate with a custom shortened LUT was found other than InkBoy itself [1]. The fastest documented hobbyist result besides PaperBoy is 20 fps 4-gray video on a bare 960x540 panel driven over its parallel bus from a stock ESP32-S3, calibrated by "lots of trial and error" against a flatbed scanner, with observers reporting heavy ghosting and grayscale remnants and blacks rendered as grays [19] (high confidence, 2026-07). Vendor and reference figures for the X4 Pro's exact panel class agree on the stock floor: GxEPD2 measured 1,567 ms full and 500 ms partial on GDEQ0426T82 [10], the current firmware measures 559 ms fast and 1.5 to 1.7 s full and half [9], and reseller listings quote 0.42 to 1 s partial [25] (reseller figures low confidence, inconsistent among themselves).

**The cheap trick everyone uses first.** On SSD16xx parts the common "fast" mode is not a custom LUT: GxEPD2's GDEQ0426T82 driver and the idfxx SSD1680 driver both force the temperature register (0x1A) before the update so the OTP picks a shorter, hotter waveform [10][18]. The X4 Pro driver already does this for its half refresh (0x5A) and the UC8179 batch driver calls the same lever "REQUIRED" for its shorter partial [9]. The idfxx author documents the cost: under-driven blacks that settle to visible gray on the next partial, quality that "varies by panel batch and temperature", and a full refresh every ~10 partials to clear ghosting [18] (high confidence).

**Risk and durability.** Two independent publisher classes say DC-unbalanced short waveforms can damage panels. A 2021 peer-reviewed waveform paper keeps DC balance "to prevent the charge from getting trapped in the driving backplane" and says fast waveforms that ignore it "may cause damage to the EPD" [15]; E Ink patents state electrodes "may be irreversibly degraded" under extreme imbalance and that DC balance costs update times "two to three times longer" than imbalanced drive [16][17] (verified, two publishers). No quantified lifetime or refresh-count limit was found from E Ink or Solomon Systech; a Hackaday commenter's "~10M refreshes per pixel" is unverified [19]. The one long-run user retrospective found (an Inky Frame refreshed every 10 minutes for months) showed residue that grew over tens of thousands of refreshes and largely cleared with double refreshes, so it was not permanent, but that was a vendor OTP waveform, not a custom one [20] (medium). Vendor guidance converges on the same mitigation: a full clean every 5 to 10 partials (Inkplate) [21], conservative minimum intervals from Adafruit and Waveshare resellers [22], and InkBoy's hard caps [1].

**Calibration practice.** No open camera or photodiode rig was found; the documented methods are InkBoy's on-device transition test plus photos [1][5], Tony Weston's flatbed scanner [19], and epdiy's per-panel frame-time curves generated offline [13]. epdiy notes grayscale accuracy is temperature dependent (calibrated near 22 °C) and VCOM must be calibrated per module for partial updates [13]; the upstream SDK's own 2026-10-12 commit changed the SSD1677 custom-LUT VCOM byte after ghosting on the X4 [8], which is the same lesson on this panel.

**Team-level reality.** No open e-reader firmware with a 6 to 12 month retrospective on a shipped custom fast mode was found. The nearest evidence is GxEPD2's author offering only a differential black/white refresh as an "experimental" fast path, with a user reporting "tens of milliseconds" updates on a UC8151D and "a few pixels don't get turned on all the way" [23]; Espressif's own SSD1681 component notes that powering the display on reloads the OTP LUT over a custom one [28]; and the open ESP-IDF driver ecosystem is thin, with one driver offering no LUT upload at all [28].

## Cross-dimension insights

- **The fork is closer to InkBoy than InkBoy was to its own starting point.** InkBoy began from a stock Waveshare driver with no LUT path; the fork's SSD1677 driver already has custom-LUT upload, 0xCC activation, asynchronous refresh, windowed writes and a shadow plane [9][1]. The work is the scheduler, the counters, and the tuning, not the controller plumbing.
- **The same selector exists on all three X4 Pro batches, in two dialects.** SSD1677 keys LUT rows on (RED, BW) plane bits; UC8179 and UC8279 key WW/KW/WK/KK tables on (old, new) values, and GxEPD2's partial LUT already zeroes WW and KK [31][11]. A per-pixel budget layer can sit above both, feeding "hold" or "drive" per pixel into whichever plane encoding the batch uses, while the waveform tables stay per-batch and per-panel.
- **Everyone's "fast" mode is the temperature trick, and the fork already has it.** GxEPD2, idfxx and Waveshare force 0x1A to pick a hotter OTP waveform [10][18][32]; the fork writes 0x5A for its half refresh and the UC8179 driver calls TSSET its frame-rate lever [9]. That lever is exhausted; the next step is the loaded LUT.
- **Scale is the real difference from InkBoy, not the controller.** 384,000 pixels against 40,000 means roughly ten times the per-scan plane bytes and counter memory (about 240 KB for the dithered counters alone, which must live in PSRAM on the S3 and does not exist on the C3) [1][9]. The X4 Pro's 8 MB PSRAM covers it; the shared-code rule means the whole feature sits behind an S3-only capability guard.
- **The one number that decides the ceiling is the one nobody has.** With the frame period unknown, every fps projection for the X4 Pro is arithmetic on SPI time alone.

## Contrary evidence

The red-team pass did not run (`red_team` is off and validation is `normal`). The strongest counter-arguments surfaced by the research itself: the Glider author's own table rates controller panels at "100ms to several seconds" total latency because of the host-to-controller data path [12]; the idfxx author documents that shortened waveforms under-drive blacks and vary by batch and temperature [18]; and E Ink's patents say DC-balanced drive is two to three times slower than imbalanced drive precisely because imbalance degrades electrodes [16]. None of these says the X4 Pro cannot reach 10 to 20 window scans per second; all of them say the result will be grayer, panel-specific, and of unknown long-term cost.

## Recommendations

1. **Run a frame-period spike before planning the epic** (feeds: roadmap risk and estimates). Load a one-group LUT with TP[0A] = 1, 10 and 100 at the Waveshare frame-rate bytes and two other values, activate with 0xCC, and time BUSY on an SSD1677 X4 Pro. Also time a full two-plane upload at 20 and 40 MHz with and without the 0x44/0x45 window. Two numbers, one day. Confidence basis: datasheet structure verified [31]; floor unverified.
2. **Build the InkBoy selector on SSD1677 first, as an S3-only opt-in capability** (feeds: architecture spine). Scheduler with fixed slots and skip-not-queue, per-pixel 4-bit counters in PSRAM, black and white masks written to the BW and RED planes for the changed window only, a final all-hold scan, and a cleaning refresh plus session cap. Keep voltages at the panel's values and never touch the OTP command. Confidence basis: InkBoy's code and docs, primary, re-read [1]; SSD1677 mapping verified [31][32].
3. **Treat dithered black and white as the product target and partial-transition grayscale as research** (feeds: brief feasibility). InkBoy's 12-transition dose model took "a lot" of calibration on one panel and still leaves trails; the X4 Pro's three batches multiply that [1][5]. Ship 1-bit first.
4. **Port InkBoy's calibration tooling, not its numbers** (feeds: architecture spine, test strategy). A transition-test screen, a live one-value-at-a-time tuning menu, JSON presets on the SD card, and an FPS counter, so each batch and each tester can tune without a rebuild [1]. Numbers from a 200x200 SSD1681 do not transfer [1][13].
5. **Carry the safety patterns from day one** (feeds: PRD constraints). Volatile LUT only with a build check that forbids the OTP write command, stock voltage tail, a cleaning full refresh on a count and on exit, a per-session cap, and an off-by-default setting with a lifespan warning, because damage is plausible but unquantified [1][15][16][17] (risk verified, magnitude unknown).
6. **Do not chase Ink-boy's driver.** Its only public artefacts are wrapper signatures and a self-reported 276 ms on one UC8279 unit [7]. If the author answers an issue, the DU frame-scaling and diff-window code could save UC8279 work; otherwise the fork's own UC8179 register-LUT path is the better base [9][11].
7. **Keep the UC8179 and UC8279 batches on the roadmap as a second phase**, using the old/new register-LUT dialect with WW and KK held at zero [11][27]; expect separate tuning per batch.
8. **Decide the SPI clock question explicitly.** The driver runs 40 MHz against a 20 MHz datasheet rating [9][31]; it evidently works, but the fast mode doubles bus traffic, so measure error rates at both before relying on 40 MHz.

## Open questions

- **SSD1677 frame period at 800x480** and the encoding of the five frame-rate bytes. Answer: the spike in recommendation 1; or a newer Solomon datasheet revision.
- **Does 0x37 RAM ping-pong give a hardware old/new swap** that avoids re-uploading both planes? Answer: datasheet reading plus a bench test.
- **Which 0x22 value to use per scan.** The datasheet decoding points at 0xCC (clock and analog on, mode 2, no OTP load) or 0x0C once powered; a community SSD1677 note for the X4 uses 0x0C for its custom-LUT fast path but cites no measurements [31][36]. Answer: the spike.
- **Panel lifetime under one-frame, DC-unbalanced scans.** No vendor figure exists [15][16][19]. Answer: a long-run soak on a sacrificial unit, photographed daily.
- **Per-batch and per-unit variance across the three X4 Pro controllers** and across the Sticky's SSD1677 panel, where 0x1C already behaves differently [9]. Answer: the calibration tooling in recommendation 4 on several units.
- **Temperature.** InkBoy ignores the sensor; the OTP waveforms are temperature-indexed [1][17][18]. Answer: test at two temperatures during the spike.
- **Ink-boy's actual driver.** Only the author can supply it [7][30].
- **Measured SPI and DMA cost on the ESP32-S3** for a 48 KB plane; none published [24]. Answer: part of the spike.

## Source appendix

| n | claim or finding it supports | publisher | pub date | accessed | confidence |
|---|---|---|---|---|---|
| [1] | InkBoy driver mechanics, timings, calibration tooling, caps | [kuslota/InkBoy repository @2673c8a (code, docs/how-it-works.md, docs/tuning.md)](https://github.com/kuslota/InkBoy) | 2026-10-12 | 2026-10-16 | high |
| [2] | esp32-gameboy scope and metadata | [kuslota/esp32-gameboy README and GitHub API](https://github.com/kuslota/esp32-gameboy) | 2026-10-13 | 2026-10-16 | high |
| [3] | three sibling repos | [GitHub API, kuslota repositories](https://api.github.com/users/kuslota/repos) | 2026-10-13 | 2026-10-16 | high |
| [4] | video identity | [YouTube oEmbed, "GameBoy on E-Paper ESP32 at 30FPS", channel Bo Made It](https://www.youtube.com/watch?v=XiZmM6hwwAU) | unknown | 2026-10-16 | high |
| [5] | author's process account | kuslota video transcript (user-supplied import) (`imports/kuslota-video-transcript.md`) | unknown | 2026-10-16 | medium |
| [6] | community questions on calibration and wear | [Hacker News item 50039536](https://news.ycombinator.com/item?id=50039536) | 2026-10-11 | 2026-10-16 | medium |
| [7] | Ink-boy lineage, preset, timings, missing driver | [hellominecraft913-dev/Ink-boy repository @f8d9c30 and GitHub API](https://github.com/hellominecraft913-dev/Ink-boy) | 2026-10-15 | 2026-10-16 | high |
| [8] | upstream SDK SSD1677 and UC82xx driver facts, VCOM ghosting note | [Free-Ink/freeink-sdk @e91e34e](https://github.com/Free-Ink/freeink-sdk) | 2026-10-12 | 2026-10-16 | high |
| [9] | current X4 Pro driver capabilities and measured refresh times | freeink-sdk baseline import (local SSD1677/UC8179 drivers and docs, prior-run X4 Pro timings) (`imports/freeink-sdk-ssd1677-baseline.md`) | 2026-10-16 | 2026-10-16 | high |
| [10] | no 0x32 LUT, 0x1A temperature fast path, measured 1567 ms full / 500 ms partial | [GxEPD2 @de82887, GDEQ0426T82 (SSD1677) driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdeq/GxEPD2_426_GDEQ0426T82.cpp) | unknown | 2026-10-16 | high |
| [11] | UC8179 register LUT layout and partial LUT | [GxEPD2 @de82887, GDEY075T7 (UC8179) driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey/GxEPD2_750_GDEY075T7.cpp) | unknown | 2026-10-16 | high |
| [12] | per-pixel driving fundamentals; controller vs bare-panel latency table | [Modos-Labs/Glider README @ed94ef7 (ZephRay)](https://github.com/Modos-Labs/Glider) | unknown | 2026-10-16 | high |
| [13] | bare-panel waveform generation, DU frames, temperature dependence | [vroland/epdiy @42c1612](https://github.com/vroland/epdiy) | unknown | 2026-10-16 | high |
| [14] | PaperBoy drives a bare panel, 30-60 fps | [CNX Software, "PaperBoy Game Boy emulator works at 60 FPS on ESP32-S3 e-ink devkit"](https://www.cnx-software.com/2026/07/02/paperboy-game-boy-emulator-works-at-60-fps-on-esp32-s3-e-ink-devkit/) | 2026-07-02 | 2026-10-16 | medium |
| [15] | DC balance rule, 50 Hz frames, two-stage 4-gray waveform | [Frontiers in Physics, Wang et al., "driving waveform" (2021)](https://www.frontiersin.org/journals/physics/articles/10.3389/fphy.2021.723106/full) | 2021-08-09 | 2026-10-16 | high |
| [16] | DC imbalance degrades electrodes; DC balance costs 2-3x update time | [E Ink patent US11935495](https://patents.google.com/patent/US11935495) | unknown | 2026-10-16 | medium |
| [17] | DC-balanced waveforms needed to avoid damage; selection keys on temperature, prior state, dwell | [Amazon/E Ink patents US20210358424 and US9024862](https://patents.justia.com/patent/20210358424) | unknown | 2026-10-16 | medium |
| [18] | temperature-forcing fast mode; batch and temperature variance; full every ~10 partials | [Espressif Component Registry, cleishm/idfxx_epaper_ssd1680 readme](https://components.espressif.com/components/cleishm/idfxx_epaper_ssd1680/versions/1.0.0/readme) | unknown | 2026-10-16 | high |
| [19] | 20 fps 4-gray on a bare 960x540 panel; trial-and-error calibration; ghosting | [Tony Weston, EPD_Painter / Bad Apple page, and Hackaday 2026-07-21](https://hackaday.com/2026/07/21/20-fps-on-e-paper-display-without-help/) | 2026-07-21 | 2026-10-16 | high |
| [20] | residue after tens of thousands of refreshes, recoverable | [Pimoroni forum, "Inky Frame 7.3 burn-in"](https://forums.pimoroni.com/t/inky-frame-7-3-burn-in/24574) | 2024-08-05 | 2026-10-16 | medium |
| [21] | full refresh every 5-10 partials for lifespan | [Soldered, Inkplate partial update docs](https://docs.soldered.com/inkplate/6motion/basics/partial-update/) | unknown | 2026-10-16 | high |
| [22] | vendor minimum refresh interval guidance | [Adafruit, e-ink usage expectations; Core Electronics forum](https://learn.adafruit.com/adafruit-eink-display-breakouts/usage-expectations) | unknown | 2026-10-16 | medium |
| [23] | differential refresh "tens of milliseconds" on UC8151D, incomplete pixels | [Arduino forum, "GxEPD2 minimize full refresh time"](https://forum.arduino.cc/t/gxepd2-minimize-full-refresh-time/1166833) | 2023-09-09 | 2026-10-16 | medium |
| [24] | 40 MHz GPIO-matrix SPI limit | [Espressif ESP-IDF SPI master docs](https://docs.espressif.com/projects/esp-idf/en/v3.1.5/api-reference/peripherals/spi_master.html) | unknown | 2026-10-16 | medium |
| [25] | vendor-quoted full/fast/partial seconds (inconsistent) | [OpenELAB and Eckstein reseller listings for GDEQ0426T82](https://openelab.com/products/4-26-inch-e-ink) | unknown | 2026-10-16 | low |
| [26] | SSD168x LUT group structure, 0x32 command, SSD1677 programmable waveform | [Zephyr reel_board dts; NuttX ssd1680 driver; Solomon Systech SSD1677 page](https://www.solomon-systech.com/?p=5604) | unknown | 2026-10-16 | medium |
| [27] | UC81xx register LUT override of OTP | [Zephyr ultrachip,uc81xx-common binding](https://codex.ac6.fr/Zephyr/raw/Zephyr-latest/dts/bindings/display/ultrachip%2Cuc81xx-common.yaml) | unknown | 2026-10-16 | high |
| [28] | power-on reloads OTP LUT; thin open-driver ecosystem | [Espressif esp_lcd_ssd1681 readme; dzarda7/esp_epaper](https://components.espressif.com/components/espressif/esp_lcd_ssd1681/versions/0.1.0/readme) | unknown | 2026-10-16 | medium |
| [29] | CrossInk versions; no 1.8.x; no fast-refresh notes | [uxjulia/CrossInk releases and tags](https://github.com/uxjulia/CrossInk/releases) | 2026-10-04 | 2026-10-16 | high |
| [30] | no public SDK fork carrying the Ink-boy driver | [GitHub profiles hellominecraft913-dev and thebitsworld; Free-Ink/freeink-sdk forks page](https://github.com/Free-Ink/freeink-sdk/forks) | unknown | 2026-10-16 | high |
| [31] | 112-byte LUT layout, plane-keyed LUT0-3, TP/RP ranges, 0x22 sequences, 0x21, 0x37, no frame-rate table, 20 MHz SPI write max | [Solomon Systech, SSD1677 datasheet Rev 1.0 (2018-11), Waveshare mirror](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf) | 2018-11 | 2026-10-16 | high |
| [32] | 4-gray custom LUT via 0x32 activated with 0xC7; fast via 0x1A=0x5A + 0x91; partial via 0xFF | [Waveshare e-Paper repository, EPD_4in26.c (V1.0 2023-12-19)](https://raw.githubusercontent.com/waveshareteam/e-Paper/master/RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_4in26.c) | 2023-12-19 | 2026-10-16 | high |
| [33] | same-family frame-rate range 15-200 Hz (not an SSD1677 figure) | [Solomon Systech, SSD1675 datasheet Rev 0.11 (Adafruit mirror)](https://cdn-learn.adafruit.com/assets/assets/000/092/748/original/SSD1675_0.pdf) | 2016-08 | 2026-10-16 | low |
| [34] | vendor full 4 s / partial 0.7 s | [Waveshare, 4.26inch e-Paper product page and wiki](https://www.waveshare.com/wiki/4.26inch_e-Paper_HAT_Manual) | unknown | 2026-10-16 | high |
| [35] | SSD1681 LUT shape for comparison | [Waveshare e-Paper repository, EPD_1in54_V2.c (SSD1681)](https://raw.githubusercontent.com/waveshareteam/e-Paper/master/RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_1in54_V2.c) | unknown | 2026-10-16 | high |
| [36] | community 0x22 values and timings (unsourced) | [bigbag/papyrix-reader, SSD1677 driver notes](https://github.com/bigbag/papyrix-reader/blob/main/docs/ssd1677-driver.md) | unknown | 2026-10-16 | low |

## Staleness map

Computed with `recon_kit.py staleness` over the claims ledger (21 claim lines, 13 distinct refs). Windows in months by claim class: code-fact 1, ecosystem 6, user-report 6, timing-number 12, design-pattern 24, risk 24, controller-fact 96 (datasheet facts change only by revision). Five claims with no publication date were floored to the access date.

| re-check by | stale now | claim |
|---|---|---|
| 2017-08-01 | yes | [33] SSD1675 family frame rate 15-200 Hz; not an SSD1677 figure |
| 2020-11-01 | yes | [31] SSD1677 Rev 1.0 datasheet gives no frame period or frame-rate byte encoding; 0x3A/0x3B reserved (lead grep of  |
| 2023-08-01 | yes | [15] DC-unbalanced fast waveforms may damage the EPD (backplane charge trapping); corroborated by E Ink patents [16 |
| 2025-12-01 | yes | [32] Waveshare 4.26 fast = 0x1A=0x5A + 0x22=0x91 then 0xC7; partial = 0xFF; no custom LUT in either (re-read; same  |
| 2026-11-01 | no | [1] InkBoy drives per-pixel through the SSD1681 by using the two RAM planes as a LUT row selector (00 hold, 10 bla |
| 2026-11-01 | no | [7] Ink-boy fast preset (pll 0x0F, DU 40% frames, 20 MHz, diff window) is gated to UC8279 variant 0x68 only; SSD16 |
| 2026-11-01 | no | [7] Ink-boy is a 2026-10-06 copy of CrossInk v1.6.1 (a CrossPoint fork), not an ancestor; its fast driver SDK comm |
| 2026-11-01 | no | [31] SSD1677 112-byte waveform set: 5 LUTs x 10 groups x 4 phases, TP/RP per group, 5 frame-rate bytes, voltages vi |
| 2026-11-01 | no | [31] Per-pixel LUT row selected by (RED bit, BW bit) into LUT0-3; Waveshare 4-gray on the same panel uses all four  |
| 2026-11-01 | no | [31] One-frame LUT (TP=1, rest 0) is within documented ranges; no stated minimum (legality by absence) |
| 2026-11-01 | no | [31] 0x22 bit 4 loads the OTP LUT over a 0x32 LUT; 0xC7/0xCC run the loaded LUT (datasheet sequences re-read; consi |
| 2026-11-01 | no | [31] SSD1677 SPI write maximum 20 MHz; the fork's driver defaults to 40 MHz [9] (datasheet re-read) |
| 2027-01-01 | no | [14] PaperBoy 30-60 fps on a bare 960x540 panel via raw row/column drive (secondary reporting) |
| 2027-07-01 | no | [19] 20 fps 4-gray on a bare 960x540 panel from an ESP32-S3 (author's own claim via Hackaday; parallel bus, not con |
| 2027-10-01 | no | [1] InkBoy stock SSD1681 partial ~360 ms; one-frame scan ~20 ms (dithered) / ~14 ms (gray); analog kept on saves ~ |
| 2027-10-01 | no | [7] Ink-boy preset refresh ~276 ms vs ~680 ms stock Fast on UC8279 0x68 (self-reported code comment, single unit) |
| 2027-10-16 | no | [24] Derived SPI wire time 9.6 ms per 48 KB plane at 40 MHz; no measurement found |
| 2027-10-16 | no | [12] Glider README: controller panels 100-300 ms BW partial, total latency 100 ms to several seconds (single publis |
| 2028-10-16 | no | [10] GxEPD2 GDEQ0426T82 (SSD1677) loads no 0x32 LUT; its fast path forces 0x1A temperature before 0x22; measured 15 |
| 2028-10-16 | no | [18] Temperature-forced fast mode under-drives blacks and varies by panel batch and temperature; full refresh every |
| 2034-10-16 | no | [11] UC8179 register LUTs 0x20-0x24 keyed on old/new; GxEPD2 partial LUT zeroes WW/KK (single publisher, code) |

Earliest re-check: **2017-08-01**. 4 claims are already past their window today (the 2016 SSD1675 proxy figure, the 2021 DC-balance paper, and Waveshare's 2023 driver): that means look for newer literature on panel lifetime and a newer driver or datasheet revision, not that the facts are wrong. Next in line are the repository facts on Ink-boy and InkBoy, which age in a month because both projects are days old and may publish new code. This is the Refresh work order.
