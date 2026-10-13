---
title: 'Heap lines reach the diagnostics log without a USB host'
type: 'feature'
ticket: '21'
created: '2026-10-13'
status: 'built'
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/docs/crosshatch/upstream-touches.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
warnings: [oversized]
baseline_revision: 'daa3594b58899c916c87941fd6ad1a739b992185'
deferred:
  - summary: >-
      No automated check compiles src/main.cpp with CROSSHATCH_SD_LOG=1, so an edit that keeps the pinned text but stops a define-on loop() from reaching or compiling the [MEM] block would pass CI.
    evidence: |-
      Every CI firmware env builds with the define off, and DiagLogMain.TheHeapLinesNeedNoUsbHostOnlyWhenTheDefineIsOn reads src/main.cpp as text only. This extends entry 19's deferred item "No CI job compiles lib/DiagLog/DiagLogDevice.cpp or the logPrintf hook with the define on" to the [MEM] branch. The close-out it names belongs in entry 11's rebuilt packet: a define-on `pio run -e x4pro` and `-e default`, and the device check that a cardless card log holds the [MEM] lines every 10 s.
    location: >-
      src/main.cpp:loop (the #if CROSSHATCH_SD_LOG branch of the [MEM] if)
    severity: low
---

<intent-contract>

## Intent

**Problem:** Upstream `src/main.cpp`'s `loop()` logs the `[MEM]` heap lines (internal `Free`, `Total`, `Min Free`, `MaxAlloc`, and PSRAM) every 10 s only while `Serial` reports a USB host or the board logs through the ROM console. An X4 Pro running a `CROSSHATCH_SD_LOG` build with no computer attached therefore writes no `[MEM]` line to its card, so the smoke run's ten-cycle heap check (entry 11's packet, step 6) and entry 14's heap after teardown (R12) have nothing to read.

**Approach:** With `#if CROSSHATCH_SD_LOG` on, the `[MEM]` block runs on the same 10 s period with no host condition; with it off, upstream's comment and condition compile unchanged, so the loaded image of every define-off build is the base commit's (row 14's rule: only the app descriptor's stamps and digests may differ). Ledger row 14 names the change; `docs/crosshatch/fork-scripts.md`'s run without a computer says the card gets the `[MEM]` lines. A host test pins the `main.cpp` text, as row 14's flush calls are pinned, since the host suite does not build `main.cpp`.

</intent-contract>

## Implementation Notes

Oneshot: about 40 lines in four files (`src/main.cpp`, `docs/crosshatch/upstream-touches.md`, `docs/crosshatch/fork-scripts.md`, `test/diag_log/DiagLogWriterTest.cpp`), all small and local. The baseline firmware (x4pro, default, sticky at `daa3594b`, clean tree) was built before any change, for the loaded-image comparison.

- `src/main.cpp`: the `#if CROSSHATCH_SD_LOG` / `#else` / `#endif` around upstream's `[MEM]` comment and `if`, as Design Notes show; five inserted lines, no upstream line changed. clang-format keeps the split `if` header as written.
- `test/diag_log/DiagLogWriterTest.cpp`: `DiagLogMain.TheHeapLinesNeedNoUsbHostOnlyWhenTheDefineIsOn`, a text pin beside row 14's flush pin. It finds upstream's comment and condition, byte for byte, between `#else` and `#endif` and just before the block's body, and requires the define-on branch to hold exactly one comment line and `if (` plus upstream's own period clause. Mutation check (each made by hand, then reverted): adding `Serial &&` to the define-on `if`, changing its period to 5000, and writing `#ifdef` for `#if` each fail it.
- `docs/crosshatch/upstream-touches.md`: row 14 names the `[MEM]` condition, and the paragraph under the table says why row 14 holds it. `docs/crosshatch/fork-scripts.md`: a **Heap lines** bullet in the run without a computer.
- Surprise: none. `scripts/check_upstream_touches_test.py` needs no change, since the row count is still 14.

## Plan Change Log

## Review Triage Log

### 2026-10-13 — Review pass
- verdicts: 17 findings — high 0, medium 0, low 14, false 3, maybe-false 0
- lenses: Blind Hunter, Edge Case Hunter, Verification Gap, and Intent Alignment, each run as a context-free subagent over `git diff daa3594b` (the plan was given only to the Edge Case Hunter, as its claims file).
- findings:
  - `[low]` `reject` Blind Hunter 1: the test in `test/diag_log/DiagLogWriterTest.cpp` is outside entry 21's `touches`, and nothing records why. The only fix is a note in this plan, so the finding is rejected. The file is a fork test, not in `stays_out`, and it sits beside row 14's flush pin. Auto Run Result's Files list names it as outside `touches`.
  - `[low]` `patch` Blind Hunter 2: the `main.cpp` comment says the diagnostics log puts "every line" on the card, but the log drops lines when its buffer overruns and after the sleep flush. The comment now reads "With the diagnostics log on, the heap lines go to its card file, so they need no USB host (ledger row 14)."
  - `[low]` `patch` Blind Hunter 3: the pin failed with no message when upstream's condition changed, which is its likeliest failure. The `ASSERT_NE` now says that upstream's `[MEM]` comment or condition changed and that row 14 and its branch need updating, and the branch checks say what they expect.
  - `[low]` `patch` Blind Hunter 4: one line of the new `fork-scripts.md` bullet ran 142 characters. The bullet is rewrapped, and every added line is now at most 120.
  - `[low]` `patch` Blind Hunter 5: "every 10 s" overstated what the card gets. The first lines come about 10 s after boot, `loop()` returns before the block while the raw-storage (USB drive) handoff runs, and the period is a minimum. The bullet now says "at least 10 s apart, the first about 10 s after boot", that a busy loop stretches the gap, and that none come while the card is lent out as a USB drive.
  - `[low]` `patch` Blind Hunter 6: row 14 said "the second condition", but the define-on `if` replaces the condition rather than adding one. It now says "the `#if CROSSHATCH_SD_LOG` branch of the `[MEM]` `if`". Grouped with Intent Alignment 3 (same root cause).
  - `[low]` `reject` Blind Hunter 7: the suite name `DiagLogMain` sits between the `DiagLogFlush` and `DiagLogWriter` tests. gtest groups output by suite, not by position, and the test sits beside the other `main.cpp` pin on purpose. No caller or check is harmed, and moving or renaming it is churn.
  - `[low]` `patch` Blind Hunter 8: the bullet's `summary --mem-all` sentence repeated the `summary` paragraph further down. The sentence is deleted.
  - `[low]` `patch` Blind Hunter 9: a host-less diagnostics run now reads the heap figures every 10 s, which before only a host-attached run did. The bullet says so and that its cost during a match is unmeasured.
  - `[low]` `reject` Edge Case 1: with the define on and `ENABLE_SERIAL_LOG` undefined (the slim env's `-UENABLE_SERIAL_LOG`) or `LOG_LEVEL` 0, `LOG_INF` compiles out and no `[MEM]` line reaches the card. The claim holds, but it applies to every line of the diagnostics log, not only this block (`Logging.h:44`), so it predates this change. The recipe builds `x4pro` (`LOG_LEVEL=2`) or `default`, and both set `ENABLE_SERIAL_LOG`. The proposed `#error` guard adds a branch for a build nobody makes.
  - `[low]` `defer` Verification Gap 1: no automated check compiles `main.cpp` with `CROSSHATCH_SD_LOG=1`. An edit that keeps the pinned text but stops the define-on `loop()` reaching or compiling the block would pass CI. This predates the change: entry 19 deferred "no CI job compiles the define-on code" (low), and this entry adds the `[MEM]` branch to that code. It is recorded in `deferred`. This run's define-on builds and the disassembly read (Verification) cover today's tree.
  - `[low]` `reject` Verification Gap, other 1: the Verification's command list called the new pin "the new `DiagLogFlush` pin", but its suite is `DiagLogMain`. The fix is a plan edit. The Results below name the test by its full name.
  - `[low]` `reject` Verification Gap, other 2: Design Notes quote an older wording of the `main.cpp` comment. The fix is a plan edit, and the snippet illustrates the shape, not the wording. The comment has since changed again (Blind Hunter 2).
  - `[false]` `reject` Intent Alignment 1: the diff carries no ELF, image comparison, or disassembly (reading C). Those are firmware checks, which by the verification order run after the review. They ran, and their results are under Verification, Results.
  - `[false]` `reject` Intent Alignment 2: the test reads source text, not the built image. The built-image evidence the ticket asks for comes from the Verification builds: the loaded-image comparison and `loop()`'s disassembly. The text pin is an addition that keeps the branch's shape.
  - `[low]` `patch` Intent Alignment 3: the ticket's Unknown and row 14 say "second condition", but the code replaces the condition. Grouped with Blind Hunter 6; the same row 14 fix applies.
  - `[false]` `reject` Intent Alignment 4: the bullet lists `Total`, which the ticket's field list omits. The bullet matches the firmware's line format (`main.cpp`'s `LOG_INF`, `device_log.py`'s `MEM_LINE`), so it is not a divergence.

## Design Notes

- **Settled by the sources.** The define is `CROSSHATCH_SD_LOG`, tested with `#if` and off in every env (the orchestrator's note for this run; `lib/Logging/Logging.cpp`'s row-13 hook uses the same form). The change belongs to ledger row 14, which is already the diagnostics log's in `src/main.cpp` (epic Notes, Scope note for entry 21). Normal builds keep the base's loaded image (entry 21's description; row 14's rule). `fork-scripts.md`'s cardless run says the card gets the lines (entry 21's description). Entry 11's packet (step 6, and its Firmware line saying the lines come "while a port is open") is not edited: the Scope note says this entry runs before that packet is rebuilt, which is entry 11's work.
- **The Unknown: a second condition inside the define, not a constant.** The `[MEM]` `if` becomes:
  ```cpp
  #if CROSSHATCH_SD_LOG
    // The diagnostics log puts every line on the card, host or not (ledger row 14).
    if (millis() - lastMemPrint >= 10000) {
  #else
    // The ROM console does not depend on Arduino USB CDC's connection state.
    if ((Serial || FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF) && millis() - lastMemPrint >= 10000) {
  #endif
  ```
  Upstream's comment and condition stay byte-for-byte, so the diff is inserted lines only and an upstream merge conflicts only if upstream rewrites those lines. A constant (`|| kMemLogWithoutHost`) would rewrite the upstream line, and at 120 columns clang-format would wrap it into two or three changed lines with a fork name inside upstream's expression. Each compiled configuration reads as one plain `if` on one condition, and the body (the two `LOG_INF` calls and `lastMemPrint = millis()`) is shared, not copied. The cost is that the 10 s period is written twice. The host pin fails if the two branches' period clauses ever differ, so the "same 10 s period" holds by test, not by reading.
- **What the existing condition guards, and what is kept.** `Serial` (HWCDC's host-connected state; the condition since `9a330306`) keeps the lines from being made when no host reads them. The ROM-console clause (`3f70f5df`, upstream #3683, Sticky's missing RAM stats) covers a board whose console does not depend on CDC. With the define on, the reader is the card: `logPrintf` hands every line to `DiagLog::append` after the transport write, whatever the host state (`lib/Logging/Logging.cpp:79`), so both host clauses guard nothing there. The period clause (`millis() - lastMemPrint >= 10000`: unsigned subtraction, so safe across the `millis()` wrap) is kept unchanged in both branches. On a host-less X4 Pro, `logSerial.print` queues without blocking (`Logging.cpp`'s comment), as for every other line the diagnostics build logs.
- **Cost with the define on.** Two lines of about 110 B every 10 s, about 80 KB an hour, against 16 segments of 1 MiB. This is an estimate from the line format; nothing measured it on a device.

## Verification

**Commands (implementation, host and fast checks):**
- Under `flock /tmp/crosshatch-hosttest.lock`: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, the new `DiagLogFlush` pin included.
- `for t in scripts/*_test.py; do python3 $t; done`; `python3 scripts/check_upstream_touches.py` (row 14 amended); `./bin/clang-format-fix` twice -- expected: pass, and the second run changes nothing.

**Commands (once, after the review, under `/tmp/crosshatch-build.lock`, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`):**
- `pio run -e x4pro`, `-e default`, `-e sticky` without the define -- expected: each `firmware.bin` matches the base commit's, byte for byte, except the app descriptor's ELF digest, build time and date, and version string, and the image checksum and digest (classified by a byte diff of each pair against the image's segment map, as entry 19 compared).
- `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1 pio run -e x4pro`, and `-e default` (the C3) -- expected: they build. Read `loop`'s disassembly in the define-on and define-off x4pro ELFs (`xtensa-esp32s3-elf-objdump`): the define-off `loop` tests `HWCDC::operator bool` ahead of the `[MEM]` block, and the define-on `loop` reaches it with no such test.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for `default` and `-e x4pro`, and both again with `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1` -- expected: no defect.
- Not run, with reasons: `scripts/check_flash_budget.py` (the define-off games-on image is the base's, compared above, so its figures cannot move); `sim.sh build x4pro` (no screen or game code changes, and the simulator cannot build the define-on code; entry 19's deferral).

**Manual checks:**
- A device check waits for entry 11's smoke run: a cardless run's card log holds a `[MEM]` pair every 10 s.

**Results (2026-10-13).** The base is `daa3594b58899c916c87941fd6ad1a739b992185`, built first on a clean tree. Every build below ran on the base's HEAD with this change as an uncommitted tree, after the review patches. The commit then adds this plan and nothing else, so both sides carry `daa3594b` in their version strings, the change's side with `-dirty`. Builds used `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache` under `/tmp/crosshatch-build.lock`. The scripts and logs are under the session scratchpad's `6.21/` (`build_base.sh`, `build_new.sh`, `cmpimg.py`, `base/`, `new/`).
- **Host.** `ctest` 2579 of 2579 pass, including `DiagLogMain.TheHeapLinesNeedNoUsbHostOnlyWhenTheDefineIsOn`. The pin's mutation check is under Implementation Notes. Every `scripts/*_test.py` passes (14 files), as do `check_layers.py` and `check_upstream_touches.py` (PASS, with a clean trial merge of `upstream/develop`; run again on the commit). `./bin/clang-format-fix` twice after the last edit changed nothing.
- **Define off: the loaded image equals the base's.** `cmpimg.py` walks each `firmware.bin`'s segment table and names every differing byte: an `esp_app_desc_t` field, the trailer, or a segment offset. Like entry 19's comparison, it is a byte diff of each pair, made exact by the segment map.
  - x4pro: 6,255,680 B against 6,255,680 B. Only the descriptor's `app_elf_sha256` (32 B) and the trailer's checksum and digest (33 B) differ.
  - default: 5,927,792 B against 5,927,792 B. Only the descriptor's `app_elf_sha256`, `version`, `project_name`, and `time`, and the trailer, differ (79 B). The base's descriptor stamps name entry 19's tree (`1.6.5-ch.4-102-ga1ffc5b5-dirty`, project `...-lane-c`), because that build took its descriptor object from the shared build cache. This side's are `...-110-gdaa3594b-dirty` and `...-lane-a`.
  - sticky: 6,138,128 B against 6,138,128 B. Only the descriptor's `app_elf_sha256`, `version` (`-dirty`), and `time`, and the trailer, differ (74 B).
  - Every byte of code and data outside the descriptor's stamps is the same in all three. RAM is identical too: x4pro 104,312 B, default 59,168 B, sticky 69,896 B, against the base's same figures.
- **Define on: the `[MEM]` condition no longer needs `Serial`, read from the disassembly.**
  - `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1` builds `x4pro` (6,268,608 B, RAM 112,560 B) and `default` (the C3, 5,941,152 B, RAM 67,432 B).
  - `xtensa-esp-elf-objdump -d -C --disassemble='loop()'`: in the define-off x4pro ELF, `loop()` calls `HWCDC::operator bool() const` once, just before `millis()` and the `loop()::lastMemPrint` load (the base ELF's `loop()` is the same). In the define-on ELF, `loop()` has no call to `HWCDC::operator bool`. The block is `call8 millis`, the `lastMemPrint` load, `sub`, a compare with 9,999 (`15 + 0x2700`), and `bgeu` past the block when the difference is smaller, then `HalMemory::getInternalHeap()` and `logPrintf`.
- **`pio check`** (cppcheck, low, medium, and high): PASSED for `default` and `x4pro`, each with and without `-DCROSSHATCH_SD_LOG=1`.
- **Not run, as planned:** `check_flash_budget.py`, since the define-off games-on x4pro image is the base's, so its figures cannot move. `sim.sh build x4pro`, since no screen or game code changed. No screenshots.
- **Device:** waits for entry 11's smoke run.

## Auto Run Result

**Summary.** With `CROSSHATCH_SD_LOG` on, `loop()`'s `[MEM]` block now tests only its 10 s period, so a diagnostics build with no USB host writes the heap lines to the card. With the define off, upstream's comment and condition compile unchanged in an `#else` branch: five inserted lines, and no upstream line changed. The define-off x4pro, default, and sticky images differ from the base's only in the app descriptor's stamps and digests. In the define-on x4pro ELF, `loop()` no longer calls `HWCDC::operator bool` (read from the disassembly). The Unknown is answered with a second condition inside the define rather than a constant (Design Notes). A constant would have rewritten upstream's line.

**Files.**
- `src/main.cpp`: the `#if CROSSHATCH_SD_LOG` / `#else` / `#endif` around the `[MEM]` `if` (upstream file, ledger row 14).
- `docs/crosshatch/upstream-touches.md`: row 14 names the `[MEM]` branch, and the paragraph under the table says why row 14 holds it.
- `docs/crosshatch/fork-scripts.md`: the **Heap lines** bullet in the run without a computer.
- `test/diag_log/DiagLogWriterTest.cpp`: `DiagLogMain.TheHeapLinesNeedNoUsbHostOnlyWhenTheDefineIsOn`, a text pin beside row 14's flush pin. This file is outside entry 21's `touches` (a fork test, not in `stays_out`).
- This plan.

**Review.** The four lenses ran as context-free subagents and reported 17 findings: high 0, medium 0, low 14, false 3, maybe-false 0.
- Patched (7 entries, all low): the `main.cpp` comment's "every line"; the pin's failure messages; the bullet's long line; "every 10 s" now says at least 10 s apart, the first after about 10 s, none while the card is a USB drive; row 14's "second condition"; the duplicated `--mem-all` sentence; the unmeasured cost of reading the heaps every 10 s.
- Deferred (1, low): no automated define-on compile of `main.cpp`, which extends entry 19's deferral.
- Rejected (8), each with its reason in the log: the `touches` note (a plan edit; Files says it), the suite name, the slim-env logging (predates the change; the recipe's envs log), the two plan-wording findings (plan edits), and the intent lens's three descriptive notes (evidence lives in Verification, `Total` matches the format).

Follow-up review recommended: false. This pass patched no high and no medium (0 high, 0 medium, 7 low).

**Verification.** See Verification, Results: host 2579 of 2579; script, ledger, layers, and format checks pass; the define-off images match on all three boards; the define-on x4pro and default build; the disassembly shows no `Serial` test; `pio check` passes four ways.

**Formatting.** `./bin/clang-format-fix` changed nothing outside this story's paths.

**Residual risks.**
- No device has run it. The `[MEM]` lines on a cardless card, and the cost of reading the heaps every 10 s during a match, wait for entry 11's smoke run.
- No CI build compiles the define-on branch (deferred). The host pin guards the branch's text, not that a define-on build reaches it.
- Entry 11's packet still says the `[MEM]` lines come "while a port is open" (its Firmware line and step 6). The packet is rebuilt by entry 11 (Scope note), so this entry leaves it alone.
