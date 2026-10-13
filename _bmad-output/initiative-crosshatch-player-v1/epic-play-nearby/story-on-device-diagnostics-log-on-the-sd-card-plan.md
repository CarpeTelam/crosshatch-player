---
title: 'On-device diagnostics log on the SD card'
type: 'feature'
ticket: '19'
created: '2026-10-13'
status: built
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/docs/crosshatch/fork-scripts.md'
  - '{project-root}/docs/crosshatch/upstream-touches.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
baseline_revision: '0b4ab0059a742dc435a8df7e62f3f650299c86f4'
warnings: [oversized]
deferred:
  - summary: >-
      No barrier before sleep: lines logged in the last ~25 ms before Storage.prepareForDeepSleep() and the "Entering deep sleep" line after it never reach the card.
    evidence: |-
      enterDeepSleep (src/main.cpp) calls Storage.prepareForDeepSleep(), which unmounts, and then logs; the intent asks for a flush before sleep, this entry's stays_out forbids a call site, so the writer drains by itself and DiagLog::flush(timeoutMs) exists, called by nothing. A call to DiagLog::flush before the unmount (and a batch that straddles it fails and is counted) needs one line in src/main.cpp under #if CROSSHATCH_SD_LOG and a ledger row: an owner Decision.
    location: >-
      src/main.cpp:enterDeepSleep, lib/DiagLog/DiagLogDevice.cpp:flush
    severity: medium
  - summary: >-
      The writer's open/append/close every ~25 ms under the shared storage lock may stall page or asset loads or a game frame, and wears the card; unmeasured.
    evidence: |-
      Same priority as the loop task, one SD operation set per batch. Only a device run can measure it: compare frame and page times with and without the define, and the writer's keep-up (the dropped-lines notice in the files reports overruns).
    location: >-
      lib/DiagLog/DiagLogDevice.cpp:writerMain
    severity: medium
  - summary: >-
      No CI job compiles lib/DiagLog/DiagLogDevice.cpp or the logPrintf hook with the define on, so a change to Storage, BoardConfig, or Logging can break the diagnostics build unseen.
    evidence: |-
      The plan forbids a CI build with the define (inception Q4 did the same for CROSSHATCH_TOUCH_LOG). Add one define-on `pio run -e x4pro` and `-e default` to the device-run packet's checks when it is built.
    location: >-
      lib/DiagLog/DiagLogDevice.cpp, lib/Logging/Logging.cpp
    severity: low
  - summary: >-
      The writer lists the folder through Storage.listFiles (a std::vector<String> built with throwing allocation) up to maxFiles + 8 times per new file, and a folder over 200 files or a matching name over 47 characters defeats the boot count and the deletion.
    evidence: |-
      The same HAL call upstream uses everywhere; the pass runs once per 1 MiB segment; only hand-placed files reach the 200 limit. A nothrow directory walk is a HalStorage addition outside this entry's files.
    location: >-
      lib/DiagLog/DiagLogDevice.cpp:StorageFileStore::listFiles, lib/DiagLog/DiagLogWriter.h:makeRoom
    severity: low
  - summary: >-
      No test runs a file the real Writer wrote through device_log.py merge; the merge tests use card files laid out by hand whose header is pinned to DiagLogWriter.h by key.
    evidence: |-
      An end-to-end test needs a gtest to write a sample file that a Python test reads; the first define-on device run is the first real card file.
    location: >-
      scripts/device_log_test.py:card_text, test/diag_log/DiagLogWriterTest.cpp
    severity: low
  - summary: >-
      The simulator cannot build the diagnostics log: its FreeRTOS shim lacks UBaseType_t, TickType_t, pdPASS, and xTaskNotifyGive, so no simulator run writes a card file for merge.
    evidence: |-
      `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1 sim.sh build x4pro` stops in lib/DiagLog/DiagLogDevice.cpp on those names; the simulator's storage double was not reached. A SIMULATOR branch of the writer task (a std::thread and a condition variable instead of the notification) would do it.
    location: >-
      lib/DiagLog/DiagLogDevice.cpp
    severity: low
---

<intent-contract>

## Intent

**Problem:** A two-device run (epic-play-nearby R12, R14) needs a computer with two USB ports to capture each device's log (`scripts/device_log.py capture`); the owner's Decision of 2026-10-13 wants the run to need no computer.

**Approach:** A build with `-DCROSSHATCH_SD_LOG=1` hands every `logPrintf` line, through one guarded hook call in `lib/Logging/Logging.cpp` (ledger row 13), to a new fork library `lib/DiagLog`: a bounded non-blocking buffer and one writer task that drains it to per-boot files on the card through `Storage`/`HalFile`. `device_log.py merge` reads two or more cards' files and writes the merged log and summary, lining the devices up by the `LINK` lines' `session` id and `STATE` `ver`.

## Boundaries & Constraints

**Always:** All SD access through `Storage`/`HalFile`; nothrow allocation with null checks; locals under 256 B; no `Serial.print*`, and no log call in `lib/DiagLog` (a writer line would feed the buffer it drains); the C3 (`default`) builds with the define on, without PSRAM; the define off means the hook, its include, and the library's code are absent and `firmware.bin` is byte-identical (x4pro, default, sticky). `lib/Logging/Logging.cpp` changes only by the include and the hook call, both inside `#if CROSSHATCH_SD_LOG`. Independent of games code. Python standard library only; `fork-scripts.md` conventions.

**Never:** Touch `platformio.ini`, `lib/Logging/Logging.h`, any other upstream file, `games/**`, `src/games/**`, `src/activities/**` (no call site changes: the hook sees every line), or `lib/GameCore/**`. Call SdFat directly. Hold an SD file open between batches. Add a CI build with the define.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Many tasks log at once | loop, VM, GameLink, Wi-Fi tasks append | no call waits for the writer or the card; each task's lines keep their order; accepted + dropped = appended | n/a |
| Buffer full | lines arrive faster than the card drains, or the card never mounts | the line is dropped whole and counted; the file says `dropped N lines (buffer full)` | n/a |
| Lines before the card mounts | setup logs before `Storage.begin()` | they wait in the buffer and reach the file in order once `ready()` | dropped past ~8 KB, counted |
| Size cap | a segment reaches 1 MiB | next batch starts the next segment with its own header; the oldest files are deleted so at most 16 remain | a failed delete stops the pass |
| Write fails | `append` returns false | `writeFailures` counted, the batch's bytes reported as `lost N bytes` in the next file, a new segment; nothing waits or retries the batch | a failing header retries the same segment |
| Card unmounted (sleep, USB drive) | `ready()` false | writer idles and retries; `flush()` answers false; lines stay buffered | n/a |
| Two cards merged | A's and B's files with a shared `session` | one `merged.log` of `<stamp> <NAME> <line>`, each STATE's send before its receive, devices named by the header, `<NAME>.raw.log` the files unchanged, a summary equal to a serial capture's | exit 2 for a file that is not a diagnostics log, a device on two names, a missing path |
| No shared STATE | cards with no `LINK` lines | merged unaligned, the note says so | n/a |
| Define off | normal builds | `firmware.bin` identical to the base commit's | n/a |

</intent-contract>

## Code Map

- `lib/Logging/Logging.cpp` -- `logPrintf` ends `addToLogRingBuffer(buf)`; add `DiagLog::append(buf)` there and `#include <DiagLog.h>`, both in `#if CROSSHATCH_SD_LOG`. Read-only otherwise (`Logging.h` stays).
- `lib/DiagLog/DiagLog.h` -- public hook `append(const char*)` and `flush(timeoutMs)`; no dependencies.
- `lib/DiagLog/DiagLogBuffer.h` -- header-only `LineBuffer<Capacity>`: ring of length-prefixed lines under a critical section (device) or a yielding spinlock (host).
- `lib/DiagLog/DiagLogWriter.h` -- header-only `Writer<Buffer>` over an abstract `FileStore`: names, header, cap, rotation, deletion, failure handling, notices.
- `lib/DiagLog/DiagLogDevice.cpp` -- wholly inside `#if CROSSHATCH_SD_LOG`: the 8 KiB buffer, the writer task, `append`, `flush`. `lib/DiagLog/DiagLogStore.h` declares `deviceFileStore()`.
- `src/diagnostics/DiagLogStorage.cpp` -- wholly inside the define: `StorageFileStore` over `Storage`/`HalFile`. A project source, not in the library, so that the library finder's graph is the same in every build (Design Notes).
- `lib/hal/HalStorage.h` -- `Storage.open(path, O_WRONLY | O_CREAT | O_APPEND)`, `listFiles`, `ensureDirectoryExists`, `remove`, `ready()`; `prepareForDeepSleep()` unmounts under the storage lock.
- `test/diag_log/` -- new GoogleTest suite (buffer, writer with a fake `FileStore`); wired by `add_subdirectory` from `test/game_core/CMakeLists.txt` (a fork file) so `test/CMakeLists.txt` stays upstream's.
- `scripts/device_log.py`, `scripts/device_log_test.py` -- `merge` command, `CardFile`, `align_clocks`, `merge_cards`; `summary` learns the card figures and the `[DIAG]` notices.
- `docs/crosshatch/upstream-touches.md` -- row 13; Game paths gain `lib/DiagLog` and `test/diag_log`.
- `docs/crosshatch/fork-scripts.md`, `docs/crosshatch/game-canvas.md` -- the cardless run, and the define beside `CROSSHATCH_TOUCH_LOG`.
- `scripts/check_layers.py` -- scans game folders only; `lib/DiagLog` needs no change unless the check says otherwise (it stays out of `touches` then).

## Tasks & Acceptance

**Execution:**
- [x] `lib/DiagLog/*` -- buffer, writer, device file, public header (built by the build agent before the plan was written; see Implementation Notes)
- [x] `lib/Logging/Logging.cpp` -- the guarded include and hook call
- [x] `test/diag_log/*`, `test/game_core/CMakeLists.txt` -- the buffer and writer suites and their wiring
- [x] `scripts/device_log.py`, `scripts/device_log_test.py` -- `merge`, alignment, card summary, tests
- [x] `docs/crosshatch/upstream-touches.md` -- row 13 (`lib/Logging/Logging.cpp`: the hook call and its include, inside `#if CROSSHATCH_SD_LOG`, no game header) and the two Game-paths entries
- [x] `docs/crosshatch/fork-scripts.md` -- "Capturing a device run" gains the cardless run: build, play, pull the files, `merge`; what the files hold, the alignment, what it cannot capture
- [x] `docs/crosshatch/game-canvas.md` -- the device-run recipe names `CROSSHATCH_SD_LOG` and `CROSSHATCH_BUILD_ID` beside `CROSSHATCH_TOUCH_LOG`
- [x] Run the host suites and fast checks (Verification); fix what they show

**Acceptance Criteria:**
- Given eight threads appending while one drains, when the buffer overruns, then no append waits, per-thread order holds, and accepted plus dropped equals appended.
- Given a fake card, when lines are logged before it mounts, fill segments, and a write fails, then the files hold the lines in order, never exceed the cap, and say what was dropped or lost.
- Given two recorded cards, when `merge` runs, then every STATE's send precedes its receive, each device is named, and the summary's figures equal the serial capture's of the same lines.
- Given the define off, when x4pro, default, and sticky are built, then each `firmware.bin` equals the base commit's.

## Implementation Notes

The build agent investigated and wrote the code, the suites, and `merge` before writing this plan (the first four tasks, ticked), so the plan's intent and Design Notes record what that work settled. The implementation subagent finishes the unticked tasks, reads the finished ones against the plan, and fixes what the host checks show. Baseline firmware (`firmware.bin` of x4pro, default, sticky at the baseline commit, clean tree) was built before any change, for the byte comparison.

Beyond the plan's first list: `scripts/check_upstream_touches_test.py` (a fork file; its ledger-row count went 12 to 13), `lib/DiagLog/DiagLogStore.h` and `src/diagnostics/DiagLogStorage.cpp` (see Design Notes, "Why the file store is a project source"), and three Game-paths entries (`lib/DiagLog`, `src/diagnostics`, `test/diag_log`). The review's patches (Review Triage Log) were made by the re-engaged subagent; the build agent then made the file-store move after the first define-on build failed (`common/FsApiConstants.h` not found) and fixed what `pio check` found (two shadowing locals, one dead assignment in `DiagLogWriter.h`).

## Plan Change Log

## Review Triage Log

### 2026-10-13 — Review pass
- verdicts: 37 rows (a finding two lenses made, or one with two parts, has a row for each) — high 0, medium 5, low 28, false 3, maybe-false 1 (plus the intent-alignment lens's descriptive report, whose divergences have rows)
- findings:
  - `[medium]` `patch` Blind Hunter 1: `startWriter` never retries after a failed `xTaskCreate` (`started` stays true, the log silently drops forever) — reset `started` on failure so a later append retries.
  - `[medium]` `patch` Blind Hunter 2 and Edge Case 8: a writer task that cannot allocate clears `writerTask` while the creator stores the handle afterwards, leaving a handle to a deleted task for `xTaskNotifyGive` — the task sets a `writerDead` atomic and never touches the handle; append and flush check it.
  - `[low]` `patch` Blind Hunter 3: the critical section copies byte by byte with a modulo (about 16 us for 255 B on a 160 MHz core, not "a microsecond") — two memcpy calls per copy, comment corrected, unmeasured on the device.
  - `[low]` `patch` Blind Hunter 4: `notice()` returning 0 would still erase the pending counts (latent: the chunk minimum makes it always fit) — subtract only what was written.
  - `[medium]` `patch` Blind Hunter 5 and Edge Case 2: a card that takes headers and fails batches rotates a header-only file and deletes the oldest on every retry, replacing the good history — a batch failure starts a new segment only when the previous batch did not fail.
  - `[low]` `defer` Blind Hunter 6 and Edge Case 6: `makeRoom` and the boot scan build `std::vector<String>` through `Storage.listFiles` (throwing allocation under `-fno-exceptions`) up to 24 times per new file — the same HAL call upstream makes everywhere and the pass runs once per 1 MiB; a nothrow directory walk is a `HalStorage` addition outside this entry's files; deferred.
  - `[medium]` `defer` Blind Hunter 7: the writer's open, append, close every ~25 ms at the loop's priority under the shared storage lock may stall page or asset loads and wears the card — real and unmeasured; the device run is where it is measured (compare frame times with and without the define); deferred.
  - `[low]` `defer` Blind Hunter 8 and Verification Gap 1: no CI job compiles `DiagLogDevice.cpp` or the hook with the define on, so Storage, BoardConfig, or logPrintf changes can break it unseen — the plan forbids a CI build with the define (as inception Q4 did for `CROSSHATCH_TOUCH_LOG`); the CMake comment that claimed the firmware builds cover it is corrected; add a define-on `pio run` to the device-run packet's checks.
  - `[low]` `patch` Blind Hunter 9: two devices both logging `state sent` for one (session, ver) would pair a receive with the wrong send and skew the offset — such a key is dropped from the anchors, with a test.
  - `[low]` `patch` Blind Hunter 10: dead `assertTrue(both)`, a late `import random` in the alignment test — removed and moved.
  - `[low]` `patch` Blind Hunter 11 and Verification Gap 2: no test fails a batch that carried a dropped or lost notice — tests added for the repeat, the non-summing, a failed delete, the failure streak, a header too long, and boot/segment widths past 9999 and 99.
  - `[low]` `defer` Blind Hunter 11 (rest) and Edge Case 5: a folder holding more than 200 files (the `listFiles` cap) undercounts the boot number and the cap, and a matching name over 47 characters cannot be deleted — only files put there by hand can do it, the writer keeps the folder at 16; noted, not guarded.
  - `[low]` `patch` Blind Hunter 12 and Edge Case 12: `FakeStoreMatchesTheDevice` fails without `freeink-sdk` and slices a fixed 1200-character window, and a failed assert leaks a changed clock into later tests — skip when the SDK file is missing, find the function's end, reset the clock in `Rig`.
  - `[false]` `reject` Blind Hunter 12 (the host suite "fails in any checkout without the submodule"): `test/game_core/CMakeLists.txt` already compiles `freeink-sdk/libs/network/JsonSax/...`, so the suite needs the submodule with or without this test.
  - `[low]` `reject` Blind Hunter 13: the plan's bookkeeping lags (status in-progress, empty lens list) and "entry 6.19" / "entry 19" / "ticket 19" differ — the plan is updated by this review; the spellings are the ticket's ref and its id.
  - `[low]` `patch` Blind Hunter 14: the `game-canvas.md` line is 700 characters and repeats the command, the `fork-scripts.md` upload example lacks the build lock, the RAM cost is not recorded, a second boot's old files get merged — docs fixed.
  - `[low]` `reject` Blind Hunter 14 (rest): `lib/DiagLog` under the ledger's "Game paths" mislabels it — the section is the ledger's list of fork-only paths and the check reads it as that; a separate list would be a script change outside `touches`.
  - `[low]` `reject` Blind Hunter 14 (sticky not mentioned): the define and the recipe are env-independent (`-e x4pro` is the example).
  - `[low]` `patch` Edge Case 1: `makeDirectory` runs only for the first file, so a folder deleted mid-run fails every later write — it runs on every `startFile`.
  - `[low]` `patch` Edge Case 3: a header append that fails after writing part of the header is retried onto the same file, gluing a torn header to a whole one — the file is removed best effort before the retry.
  - `[low]` `patch` Edge Case 4: a header longer than the chunk fails forever — a minimal header is written instead.
  - `[low]` `patch` Edge Case 7: same as Blind Hunter 1.
  - `[low]` `reject` Edge Case 9: `flush` may return true while a drop or loss notice is pending — `flush` has no caller, notices are pending only after a failed write, and a fix needs writer state shared across tasks; the doc says the writer drains by itself.
  - `[medium]` `patch` Edge Case 10: the send-before-receive tie-break reorders one device's own lines inside a millisecond — sort by (ms, device, sequence) and nudge a receive that ties its send to send plus 1 ms, with a test.
  - `[low]` `reject` Edge Case 11: the monotone clamp can flatten a boot's lines onto one stamp when alignment is poor, silently — the per-boot note names the offset and its basis; a warning needs a threshold nobody has measured.
  - `[low]` `patch` Edge Case 13: "never exceed the cap" — a file passes the cap by one batch (under 512 B); the docs say so (the tests already allowed it).
  - `[low]` `patch` Edge Case 14: "time since the earliest device's boot" — stamp 0 is the earliest logged line or file-open of any device; docstring and docs reworded.
  - `[low]` `patch` Verification Gap 3: no test makes `remove` fail — `failRemove` switch and a test that the pass stops and the segment still starts.
  - `[low]` `patch` Verification Gap, other 1: the SDK-source pin (same as Blind Hunter 12; patched together).
  - `[low]` `patch` Verification Gap, other 2: a newline-terminated line of exactly 255 bytes takes the cut branch — no text is lost (254 bytes plus the newline equals the input), a boundary test pins it.
  - `[low]` `defer` Intent Alignment divergence, flush: "a flush before sleep" lives at the sleep path (`enterDeepSleep` calls `Storage.prepareForDeepSleep()` and then logs); this entry's `stays_out` forbids a call site, so the writer drains within about 25 ms and `DiagLog::flush` exists unused — a barrier needs one call in `src/main.cpp` and a ledger row, an owner Decision; deferred with `severity: medium`.
  - `[low]` `patch` Intent Alignment divergence, firmware: the header says `unknown` without `CROSSHATCH_BUILD_ID` while the docs say the version string — falls back to the version string.
  - `[low]` `defer` Intent Alignment divergence, fixtures: the merge tests use the recorded simulator lines in hand-built card files whose header is pinned by key to `DiagLogWriter.h`, not a file the real `Writer` wrote — an end-to-end test needs a gtest to write a sample that a Python test reads; deferred.
  - `[false]` `reject` Intent Alignment divergence, summary: "equal to a serial capture's" is read as `summary` of the raw serial logs (the figures), as the entry-3 plan did; a `capture` merged log carries `connected`/`lost` lines the card summary replaces by design — not a divergence in the figures.
  - `[maybe-false]` `reject` Edge Case 5 (a matching name over 47 characters): see Blind Hunter 11 (rest); our names are about 20 characters, so only a foreign file with a name like ours reaches it.
  - `[false]` `reject` Intent Alignment (verify items outside the diff): the firmware builds, the byte comparison, the flash compare, and the simulator run are the plan's post-review Verification, run before the commit.
  - `[low]` `reject` Blind Hunter 14 (writer keep-up and wear are only "unmeasured" in the docs): covered by the deferred device-run item above.

Patches were applied by the step-3 implementation subagent, re-engaged; host suites rerun after (2556 ctest, every `scripts/*_test.py`, `check_layers.py`, `./bin/clang-format-fix` twice).

## Design Notes

- **Settled by the sources.** Hook version, define name, new ledger row 13, fork-side buffer and writer, merge by `session` and `ver`, no crash capture (Notes, Decision owner 2026-10-13); no call site changes (this entry's `stays_out`: "the hook sees every line"); `scripts/device_log.py` is entry 3's, its `LINK` formats entry 8's (`src/games/GameLinkLog.h`).
- **Unknown 1, contexts.** `logPrintf` formats with `snprintf` and prints to the CDC (neither interrupt-safe), and no interrupt handler in `src`, `lib`, or `freeink-sdk/libs` logs (the ISRs found: `epdBusyIsr`, `cstTouchInterrupt`, `onUsbSerialJtagReset`, the panic wrappers). So the hook runs in tasks on either core, the Wi-Fi tasks included. Its shared state is one critical section over a copy of at most 256 B, and two atomics. A mutex would let a low-priority task stall a game frame; a spinlock deadlocks the single-core C3 under preemption.
- **Unknown 2, boot.** Setup logs before `Storage.begin()`; those lines wait in the 8 KiB buffer and are written once `ready()`. More than that is dropped and counted; the count reaches the file. The size of boot's logging is unmeasured.
- **Unknown 3, names.** The card has no clock, so `b<boot>-s<segment>-<device>.log` in `/crosshatch-log`, boot = highest in the folder + 1. The device is the last two STA MAC bytes in upper-case hex, the nearby label's suffix. The firmware is `CROSSHATCH_BUILD_ID` (the recipe passes the short commit) else the version string, which names the commit for `default` and `sticky` only.
- **Flush before sleep.** The sleep path unmounts in `enterDeepSleep` (`Storage.prepareForDeepSleep`), then logs "Entering deep sleep". A barrier there is a call site, which `stays_out` forbids. So the writer drains within about 25 ms of a line and closes the file after every batch (a closed file is complete, and no handle is open when the card unmounts), and `DiagLog::flush(timeoutMs)` exists for a caller. Lines in the last tens of milliseconds, and the line after the unmount, are lost; a batch that straddles the unmount fails and is counted. Recorded as deferred.
- **Why the file store is a project source.** `HalStorage.h` includes SdFat's `common/FsApiConstants.h`. PlatformIO's library finder does not hand lib/hal's SdFat path to `lib/DiagLog` (hal -> Logging -> DiagLog -> hal is a cycle), so the first define-on build failed. Naming `<SdFat.h>` in the library fixed that build and moved the define-off `firmware.bin` (112 bytes shorter on x4pro, 208 on default): the finder's graph is textual and ignores `#if`, so the new edge changed the link order in every build. The store therefore lives in `src/diagnostics/DiagLogStorage.cpp`, which sees every library, and the library reaches it through `deviceFileStore()`. The define-off image is then the base image but for the app descriptor's build stamps and digests (Verification).
- **Merge alignment.** With `d_uv` the fastest (receive minus send) over the STATEs u sent and v received, v's offset over u is `(d_vu - d_uv) / 2` and the latency `(d_uv + d_vu) / 2`; one direction gives `-d_uv` with latency 0; a boot with no pair is placed after its device's previous boot, or starts with the first device. At the same millisecond a send sorts before its receive. Stamps are the run clock on the 1970 epoch so `summary` reads `merged.log` unchanged.
- **Test doubles.** `FakeStore` stands in for `Storage`/`HalFile`; `FakeStoreMatchesTheDevice` pins the facts it copies (append creates and writes at the end, `listFiles` gives file names only) to the device sources. It is more permissive: no short or slow write, no unmount between one batch's operations. The card file stands in for the device's with the header pinned to `DiagLogWriter.h`.

## Verification

**Commands (implementation, host only):**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<main checkout's googletest-src> && cmake --build build/test && ctest --test-dir build/test -j 4`, under `flock /tmp/crosshatch-hosttest.lock`
- `python3 scripts/device_log_test.py`; the loop over every `scripts/*_test.py`; `python3 scripts/check_upstream_touches.py`; `python3 scripts/check_layers.py`; `./bin/clang-format-fix` twice

**Commands (once, after the review, under `/tmp/crosshatch-build.lock`, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`):**
- `pio run -e x4pro`, `-e default`, `-e sticky` without the define; compare each `firmware.bin` with the base commit's
- `PLATFORMIO_BUILD_FLAGS='-DCROSSHATCH_SD_LOG=1 -DCROSSHATCH_BUILD_ID=0b4ab005' pio run -e x4pro` and `-e default`; x4pro with `-DCROSSHATCH_SD_LOG=1 -DCROSSHATCH_TOUCH_LOG=1`
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for `default` and `-e x4pro`, and both again with the define on
- `scripts/check_flash_budget.py` build on, build off, compare (300 KiB, 1,024 B), objects, touch-line
- `sim.sh build x4pro` with the define

**Results (2026-10-13, base commit `0b4ab0059a742dc435a8df7e62f3f650299c86f4`; the builds ran on the base commit's HEAD with the change as an uncommitted working tree, so both sides carry the same version string; the commit adds this plan and no other change after the builds):**
- Host: ctest 2556 of 2556 pass (33 `DiagLogBuffer.*` and `DiagLogWriter.*`); every `scripts/*_test.py` passes (`device_log_test.py` 65 tests); `check_layers.py` passes; `./bin/clang-format-fix` twice changed nothing. `check_upstream_touches.py` is run on the commit (below).
- Define off, base vs this tree (`firmware.bin`, the baseline built first at the base commit on a clean tree): x4pro 6,255,680 B and 6,255,680 B; default 5,927,792 B and 5,927,792 B; sticky 6,138,128 B and 6,138,128 B. Not bit-identical files: the differing bytes are only the app descriptor's `elf_sha256` (32 B) plus the 33-byte trailer (image checksum and digest), 65 bytes on x4pro, and for default and sticky also the descriptor's build time and the `-dirty` version suffix (75 and 70 bytes). Every loaded byte is the same. The ELF digest differs because the ELF carries non-loaded sections (`Logging.cpp` moved by four lines) which `firmware.bin` does not hold.
- Define on: `-e x4pro` builds, `firmware.bin` 6,268,272 B (+12,592 B over the base), static RAM 112,560 B (+8,248 B: the 8 KiB buffer and the writer's small statics); `-e default` (the C3, no PSRAM) builds, 5,940,736 B (+12,944 B), RAM 67,424 B (+8,256 B); x4pro with `CROSSHATCH_TOUCH_LOG` too builds, 6,269,072 B. Method: the same `pio run` summary lines of the same envs at the base commit and on this tree. The task stack (6 KiB) is heap, taken when the first line is logged; neither it nor the writer's keep-up is measured at run time.
- `pio check` (cppcheck, high/medium/low) passes for `default` and `x4pro`, with and without the define (a first pass found three low style defects in `DiagLogWriter.h`, fixed).
- Flash budget (`check_flash_budget.py`, this tree, define off): games on 6,255,680 B, games off 5,957,328 B, +298,352 B (+291.4 KiB, limit 300 KiB, 8,848 B spare); static RAM +864 B (limit 1,024 B, 160 B spare). The epic's recorded base was +258,048 B and +784 B, so the epic's growth is +40,304 B and +80 B, the figures before this entry: unchanged like for like. The games-off build was not rebuilt at the base commit; the games-on image is the base's (above). `objects` and `touch-line` pass.
- Simulator: not run. `sim.sh build x4pro` with the define stops in `DiagLogDevice.cpp`: the simulator's FreeRTOS shim has no `UBaseType_t`, `TickType_t`, `pdPASS`, or `xTaskNotifyGive`. The storage double is not the obstacle. A `SIMULATOR` branch of the writer task (a thread and a condition variable) would allow it; deferred. No screenshots (no screen changed).
- A card file the real `Writer` wrote has not been through `merge` (deferred): the merge tests use files laid out like the writer's, with the header pinned to `DiagLogWriter.h`.

## Auto Run Result

**Summary.** A build with `-DCROSSHATCH_SD_LOG=1` writes every `logPrintf` line to per-boot files on the device's SD card (`/crosshatch-log/b<boot>-s<segment>-<device>.log`, 1 MiB segments, at most 16 files) through one guarded call in `lib/Logging/Logging.cpp` (ledger row 13) into a new fork library `lib/DiagLog`: a bounded non-blocking buffer (a critical section over a copy of at most 256 B) and a writer task that opens, appends, and closes the file per batch through `Storage`/`HalFile`, says what it dropped or lost, and numbers a boot's segments. `scripts/device_log.py merge` reads two or more cards' files, aligns the devices by the `LINK` `STATE`s' `session` and `ver`, and writes the merged log, raw logs, and a summary equal in figures to a serial capture's. With the define off the loaded firmware image is unchanged.

**Files.**
- `lib/Logging/Logging.cpp` -- the guarded include and hook call (upstream, ledger row 13).
- `lib/DiagLog/{DiagLog.h,DiagLogBuffer.h,DiagLogWriter.h,DiagLogStore.h,DiagLogDevice.cpp}` -- public hook, buffer, writer, store declaration, task and hook.
- `src/diagnostics/DiagLogStorage.cpp` -- the file store over `Storage`/`HalFile`.
- `test/diag_log/*`, `test/game_core/CMakeLists.txt` -- 33 tests; wired from the fork CMake file.
- `scripts/device_log.py`, `scripts/device_log_test.py` -- `merge`, the card summary, and their tests.
- `scripts/check_upstream_touches_test.py` -- the ledger row count 12 to 13 and a row-13 assertion (outside this entry's `touches`; a fork file the ledger change required).
- `docs/crosshatch/upstream-touches.md` -- row 13 and three Game paths. `docs/crosshatch/fork-scripts.md` -- the run without a computer. `docs/crosshatch/game-canvas.md` -- the define in the recipe.
- This plan.

**Review.** Four lenses as context-free subagents: 37 rows (medium 5, low 28, false 3, maybe-false 1). Patched: 4 mediums (writer start never retried, dangling task handle, history erased by a flaky card, own-line reordering at a tie) and the lows listed in the log. Deferred (6): no sleep barrier (medium), the writer's effect on loads unmeasured (medium), no CI compile of the define-on code, directory-listing heap and the 200-file cap, no writer-to-merge end-to-end test, the simulator build. Every rejected finding has its reason in the log. Follow-up review recommended: true (two or more mediums patched); the unverified risk is the writer task on a real card, which no host or build check reaches.

**Verification.** See Verification, Results.

**Formatting.** `./bin/clang-format-fix` changed nothing outside this story's paths.

**Residual risks.**
- Nothing has run on a device: the writer's keep-up, the 6 KiB stack, the unmount race with `prepareForDeepSleep`, and the effect on frame times are unmeasured.
- A flush before sleep is not a barrier (deferred; a call in `enterDeepSleep` needs an owner Decision and a ledger row).
- `firmware.bin` with the define off is not bit-identical to the base's (ELF digest, build stamps, image digest); the loaded image is.
