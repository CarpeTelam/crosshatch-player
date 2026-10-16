# Semantic citation check (fresh-context verifier, 2026-10-16)

86 marked sentences checked: 71 supported, 10 stretched, 5 unsupported, 2 unsourced. Disposition by the lead:

| finding | disposition |
|---|---|
| [9] cited for windowed writes, 40 MHz default, shadow plane, SSD1677 0x5A half temperature, voltage tail, "REQUIRED" | Facts were read from the SDK by the lead but not filed; now added to imports/freeink-sdk-ssd1677-baseline.md with line references. Citations stand. |
| [10] cited for 1,567 ms / 500 ms | Read from the GxEPD2 header during verification but not filed; added as a lead addendum to digests/D3-r1-1.md. Citation stands. |
| "same voltage tail Waveshare ships" | True at this checkout; upstream later changed VCOM [8]. Report wording qualified. |
| "stock Waveshare driver with no LUT path" | Wrong; Waveshare's driver writes vendor LUTs. Reworded. |
| `displayBufferAsyncNoShadow` absent | Present at e91e34e, absent at a3a920d; report names the commits. |
| "any public repository of the author or Thebitsworld" | Softened to what was checked (profiles, forks page 1 of 5). |
| UC8179 PLL 0x30 attributed to [11][27] | Re-attributed to [8][9]. |
| ESP32 40 MHz GPIO-matrix limit | Qualified as an older ESP32 document, not stated for the S3. |
| "only arithmetic is available [24]" | Marker moved; arithmetic marked derived. |
| VSL "-15 V to -9 V" | Reworded as selectable values. |
| "a lot" of calibration [5] | Transcript says it verbatim; import file now quotes it. Citation stands. |
| papyrix 0x0C "cites no measurements" | Reworded: quotes unsourced timings. |
| 0x22 decode of unlisted values presented as high confidence | Bullet now marks the decode of 0x1C, 0xCC and 0x0C as derived, medium confidence. |
| Two unsourced sentences | Markers added ([9]; [26][35]). |
