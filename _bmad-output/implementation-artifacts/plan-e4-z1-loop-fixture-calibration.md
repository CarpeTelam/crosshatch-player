---
title: 'Recalibrate the loop fixture band "Slow C calls forever"'
type: 'bugfix'
ticket: ''
created: '2026-09-30'
status: 'built'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
baseline_revision: '07a95af23bc0e44d52796c4cb138444d13f77284'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** At the entry-14 device run (`epic-install-and-launcher/story-device-run-and-owner-sign-off-plan.md`, step 9) the `loop` fixture's band "Slow C calls forever" was abandoned (`VM did not stop within 500 ms of cancel; abandoning it`), not cancelled. Its `backtrack(6, 30)` call outlasts the 500 ms join (`GameMatchActivity::STOP_TIMEOUT_MS`) on the X4 Pro, so the hook, which sees the cancel only between calls, never runs. The band's hint says "The 3 s watchdog cancels it", and the clean cancel path (`VM stopped; ...`) is the thing it exists to show.

**Approach:** Shrink the per-call pattern to about 20 ms on the device (a tenth or less of the join), and keep the loop cheap in Lua instructions so the 3 s watchdog, not the 2 M instruction budget, ends it. "Stuck in one C call" (`backtrack(12, 40)`) stays as it is: it must still be abandoned.

</frozen-after-approval>

## Implementation Notes

Oneshot: fixture parameters, one README cell, and one host test; about 60 lines.

**Files changed**
- `test/game_script/fixtures/loop/main.lua`: `backtrackArgs(k, n)` builds the subject and pattern; `backtrack(k, n)` is `string.find(backtrackArgs(k, n))`, so `stuckInOneCall`'s `backtrack(12, 40)` is the same call as before. `slowCallsForever` builds `backtrackArgs(6, 10)` once and loops on `find(subject, pattern)`.
- `test/game_script/fixtures/README.md`: the "Slow C calls forever" row says the watchdog cancels the VM and the log reads `VM stopped; ...`, not `abandoning`. The error text in its first backticks is unchanged, so `EveryLoopFixtureBandEndsWithTheReadmesText` still pins it.
- `test/game_script/CallGuardTest.cpp`: two tests. `ACancelStopsTheLoopFixturesSlowCallsBand` runs the real band (the watchdog bands are skipped by name in `SessionGameTest`), cancels it from another thread at 100, 137, and 173 ms, and expects `Outcome::Cancelled` and `Fault::Cancelled` (not `Budget`); its canceller stops waiting when the tap has returned, so a band that errors at once fails instead of hanging. `ACallOfTheLoopFixturesSlowCallsBandIsFarUnderTheJoin` edits the fixture source's `while true do find(subject, pattern) end` into 20 calls (and fails if that text is gone), taps the band, and expects the least of three per-call means to be under 2 ms on the host.
- `_bmad-output/implementation-artifacts/deferred-work.md`: one entry under `## e4-z1`.

**Why `(6, 10)`.** The hook runs on every call event (`HOOK_MASK` has `LUA_MASKCALL`), so the cancel lands at the next `string.find` call; the wait is one call. The other bound is the budget: `CallGuard::INSTRUCTION_BUDGET` is 2 M instructions and the loop spends 4 to 6 Lua instructions a call (counted with a count hook of 1 on the same loop, 6,010 for 1,000 iterations including the test's own counter), so the budget ends the loop after about 400 K calls. It must not come before 3 s. At about 2.2 to 3 M instructions a second (the spike: 2.17 M in a search, 3.03 M in a plain loop) the instructions alone take 0.7 to 0.9 s, so a call must average more than about 6 microseconds on the device: a call of 1 ms or more clears that more than 150 times. The old loop rebuilt both strings each iteration (about 25 instructions), which would have made the window between "cheap enough for the join" and "dear enough for the budget" only about 28 to 50 ms wide.

**Measurements (host).** Lua from `lib/lua/src` built with the firmware's defines (`-DLUAI_MAXCCALLS=30 -DMAXCCALLS=16 -Dl_randomizePivot=sizeof -include luai_throw.h`, `-O2`, x86-64, Xeon at 2.1 GHz; a scratch copy of `lstrlib.c` counts entries to `match`). `string.find(rep("a", n), rep(".-", k) .. "b")`, mean of a 0.4 s loop:

| `(k, n)` | splits C(n+k+1, k+1) | matcher steps (`match` entries) | host time a call |
| --- | --- | --- | --- |
| (6, 30), old | 10,295,472 | 12,243,263 | 91 to 104 ms |
| (6, 14) | 116,280 | 155,039 | 1.11 ms |
| (6, 12) | 50,388 | 68,951 | 0.60 ms |
| **(6, 10), new** | **19,448** | **27,455** | **0.21 ms** |
| (5, 12) | 18,564 | 24,751 | 0.18 ms |
| (6, 8) | 6,435 | 9,437 | 0.074 ms |

The splits column sums C(n+k, k) over the n+1 start positions of an unanchored `string.find`, which is C(n+k+1, k+1); it tracks the measured matcher steps within 1.2 to 1.5 times. The old comment's "about 2 M steps" was C(36, 6) = 1.95 M for the first start position only, so it understated the call about 5 times; the review found this, and the fixture's header comment now uses the summed count. The "about 2 x 10^11 steps" beside `stuckInOneCall` is the same first-position count and is left alone with the call.

**Device estimate (unmeasured on the device; the owner confirms it).** Method: the ratio of the device to the host for one interpreter workload, applied to the matcher's host time. The spike (`_bmad-output/planning-artifacts/research/spike-script-engine-2026-09-26.md`, ESP32-S3 at 240 MHz, `cpu_mhz_after_run` 240) ran `while true do end` to a 10 M-instruction budget in 3.30 s. The same loop on this host, built as above with a count hook of 1000 and a budget of 10 M, takes 0.034 to 0.036 s: a ratio of 92 to 97. The ratio of the matcher is uncertain in both directions: the one-instruction loop favours the host's branch prediction (a lower ratio for the matcher), while the matcher's deep recursion costs the LX7 register-window spills the loop never takes (a higher one). The only hard lower bound is the owner's observation that the old call took over 500 ms on the device (a ratio over 4.8 from the 104 ms host call). At the ratio of 95, `(6, 10)` takes 0.21 ms x 95 = **about 20 ms** on the device, the old `(6, 30)` about 9 s. Range: about 1 ms (ratio 5) to 40 ms (ratio 190). The fixture rests on its margin, not on the estimate: a call stays inside the 500 ms join up to a ratio of about 2,600, and the budget cannot come before the watchdog unless the device is about 30 times faster than the host. The 50 ms target holds up to a ratio of about 230.

**Design.** `(6, 10)` keeps `k = 6`, so the matcher still recurses to six `.-` levels (the same depth as before, within `MAXCCALLS` 16).

## Review Triage Log

The quick lens ran as a context-free subagent, in the foreground, and returned. Verdicts: 0 high, 0 medium, 1 low patched, 2 low rejected.

- Low, patched: the comment's "about 8 K splits" (and the header's C(n + k, k)) counted the first start position only; an unanchored `string.find` sums over all n+1 start positions, C(n+k+1, k+1) = 19,448 for `(6, 10)`, and the matcher-step counts fit it. Fixed in the fixture's two comments and in the Measurements table above; the parameters come from measured times, so they do not change.
- Low, rejected: `device-run-packet.md` and `plan-e4-y-chgame-rename.md` still name the old hash `e7ebc00d62486a0f`. They record what was packed for the entry-14 run and are outside this build's paths; the deferred-work entry under `## e4-z1` says so and gives the new hash.
- Low, rejected: the 50 ms cancel bound could flake on a loaded runner. Only the time from the canceller's timestamp to the busy loop's return counts (the canceller's oversleep does not), the host call is 0.2 ms, and the full `ctest -j 4` run and the reviewer's three `-j 8` runs passed; the old parameters gave 63 to 84 ms, so a wider bound would weaken the mutation check.

The independent review of 1b549252 (three lenses: adversarial, edge-case, verification-gap; table in the orchestrator's scratchpad, `e4-z1/review-all.md`) found nothing blocking in the fixture. Triage, all in the follow-up commit:

- M1, medium, patched: the 50 ms cancel-latency bound admitted sizes the device would abandon (`(6, 20)`, `(6, 22)`, `(6, 24)` passed 3 of 3; at the ratio of 95 they are 0.75 to 2 s a call), and its comment reasoned backwards. Replaced by `ACallOfTheLoopFixturesSlowCallsBandIsFarUnderTheJoin`: the least of three 20-call means of the real band's `find` must be under 2 ms on the host (about 190 ms on the device by the ratio of 95, under the join). It fails at `(6, 20)` (7.7 ms) and `(6, 30)` (85 ms) and passes at `(6, 10)` (11 ms for 60 calls). The earlier "rejected: 50 ms could flake" row above is superseded: the latency bound is gone, and the cancel test keeps only the outcome and fault checks.
- L1, low, patched: the canceller spun on `inLua()` forever if the band returned at once. It now also stops when the tap has returned. With the loop body changed to `find(nil, pattern)` the test fails in under a second instead of hanging. The older test at `ACancelFromAnotherTaskStopsARunningLoopEvenUnderPcall` has the same pattern; it is not this change's and its fixtures loop for certain, so it is left.
- L2, low, recorded: the old-size mutation check depended on host speed and the cancel points; it no longer applies, since the per-call mean does not alias with anything.
- L3, low, patched: the "upper end of the ratio" claim was unsupported; the Device estimate now says the ratio is uncertain in both directions and that the fixture rests on its margin.
- L4, low, patched: the plan said 2.2 M instructions a second; the spike says 2.17 M (search) and 3.03 M (plain loop); the plan states both.
- L5, low, patched: the `## e4-z1` deferred entry now names its trigger.
- L6, low, patched: device step 4 lists the `VM cancelled` line.
- L7, low, patched: the fixture's header comment is wrapped to the file's width.

## Verification

**Commands:**
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j 4'` -- expected: all pass, including the two `CallGuardTest` loop-fixture tests and `SessionGameTest.EveryLoopFixtureBandEndsWithTheReadmesText`
- mutation checks (each restored): `backtrackArgs(6, 20)` fails `ACallOfTheLoopFixturesSlowCallsBandIsFarUnderTheJoin` (7.7 ms against 2 ms), `(6, 30)` fails it too (85 ms), and `find(nil, pattern)` in the loop fails the cancel test without hanging; the two tests pass 20 runs of 20 at `(6, 10)`
- `python3 scripts/pack_device_run_test.py` and every `scripts/*_test.py`, `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` -- expected: pass
- `./bin/clang-format-fix`, twice -- expected: `git status` shows nothing new

**Device check for the owner (X4 Pro, tethered):**
1. `python3 scripts/pack_game.py test/game_script/fixtures/loop <out>` and check the printed hash is `c74273851f270beb` (the old package was `e7ebc00d62486a0f`, in `device-run-packet.md` and `plan-e4-y-chgame-rename.md`; `pack_device_run.py` writes the new one into `HASHES.txt`).
2. Install `loop.chgame` (upload it, or copy it to the card, then open Games).
3. Open "Runaway scripts", tap "Slow C calls forever", and let it run to the error view.
4. Expect the serial log to read `a call ran over 3000 ms; stopping the VM`, then `VM cancelled`, then `VM stopped; arena peak ..., stack high-water ... bytes free, least at a hook ... bytes`, and no `VM did not stop within 500 ms of cancel; abandoning it` and no `Abandoned the stuck VM`. The error view reads `It stopped responding: one step ran over 3 seconds` after about 3 s (not 3.5 s).
5. Tap "Stuck in one C call": it must still log `abandoning it` and `leaked 1032 bytes`, about 3.5 s to the error view.
6. If step 4 still abandons, the device call is over 500 ms: lower `n` (for example `backtrackArgs(6, 8)`, 0.074 ms on the host) and repeat.
