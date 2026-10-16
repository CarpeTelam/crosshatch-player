# Structure lens findings (bmad-review, 2026-10-16, on research.md before the D2 grayscale subsection and D5 were added)

Word counts: total 6,162; Exec 389, D1 552, D2 729, D3 919, D4 747, Insights 289, Contrary 112, Recs 432, Open 205, Appendix 928, Staleness 653. Structure model: pyramid; fixed Deep Recon order respected.

| # | Section | Action | Finding |
|---|---|---|---|
| 1 | D4 opening | ADD Answer line | D1-D3 open with a one-sentence Answer; D4 does not. |
| 2 | D1 Answer | REORDER | Lead with the decision-relevant verdict (preset gated to UC8279 0x68, driver unpublished, nothing validates SSD1677); move star/fork/commit counts after. |
| 3 | D3 "Timings on this exact panel class" | MERGE into D4 "Panel-side floor" | Same numbers and sources repeated; keep Waveshare figures and the no-ghosting-data sentence as additions in D4. |
| 4 | Insights bullets 2-4 | CONDENSE | Repeat D3/D4 evidence; keep only the cross-dimension "so what" with back-references. |
| 5 | Recs item 8 (SPI clock) | MERGE into item 1 | Same spike; add "and error rates at each". |
| 6 | Recs item 5 (safety) | MOVE to position 2 | Match the summary's "before the first scan runs" priority. |
| 7 | D3 SSD168x comparison sentences | MOVE | Into the "What the SSD1677 exposes" bullets. |
| 8 | Staleness map claim cells | CONDENSE | Cut at clause boundaries; no dangling parentheses. |
| 9 | D1 VCOM sentence | CONDENSE | Keep in D4; D1 gets a short back-reference. |
| 10 | D4 throughput "driver runs 40 MHz" sentence | CUT | Stated in D3. |
| 11 | Exec finding 3 (276 ms vs 77-85 ms) | ADD to D1 | Comparison must appear in the body. |
| 12 | D4 Risk paragraph | SPLIT | Evidence, then mitigation. |
| 13 | Recs schema | UNIFY | Feeds tag plus confidence note on every item. |
| 14 | Open question 7 (Ink-boy driver) | CUT | Rec 6 covers it. |
| 15-16 | Frame-period repetition; decision line; appendix column | PRESERVE | Each serves a different reader stop. |

Minor: D2 heading length; Contrary-evidence opener is process metadata; Tony Weston bare-panel result may belong in D3.
