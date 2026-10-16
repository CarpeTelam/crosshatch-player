# Import: kuslota video transcript (user-supplied, 2026-10-16)

Source: YouTube video by the author of https://github.com/kuslota/esp32-gameboy describing the custom e-paper driver; transcript pasted by the user. Treat as the author's own account (primary for his method, single-source for numbers).

Key claims from the transcript:
- Board: ESP32 with a small 200x200 e-paper (Waveshare-class) with a dedicated display controller that cannot be bypassed; Waveshare partial refresh ~300 ms; stock driver ~2 fps.
- Shortened waveform from ~17 drive steps to 2; full update ~50 ms (~20 updates/s) but heavy ghosting and gradual graying of the whole panel.
- Per-pixel driving (from ZephRay PaperBoy): keep state per pixel, give a changed pixel a drive budget, deliver pulses until budget spent, stop driving stationary pixels; budget of 6-8 drives covers black-to-white; cleaner static areas, less graying; trade-off: more drive = clearer but moving objects blurrier.
- Reached ~30 fps: new scans start every ~33 ms.
- 4 shades without dithering by deliberately stopping drive partway through a transition; 12 transitions among 4 shades each need a different budget; calibrated with move-a-patch tests, photos and video of the physical panel; verbatim: "After a lot, and I mean a lot of calibration, it started to look reasonably good. It's still not perfect".
- Driver not yet published at the time of the video; promised on GitHub with the project.
- UI/menus on the same board became fast as a side effect.
