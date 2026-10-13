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
followup_review_recommended: false
context:
  - '{project-root}/docs/crosshatch/fork-scripts.md'
  - '{project-root}/docs/crosshatch/upstream-touches.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
baseline_revision: '0b4ab0059a742dc435a8df7e62f3f650299c86f4'
warnings: [oversized]
deferred:
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
  - summary: >-
      An unmount (the USB drive, or sleep when a line lands after the flush or the card is slow) landing between the writer's Storage.open and its write may leave a HalFile on an ended volume, and whether SdFat then returns an error or faults is unread.
    evidence: |-
      HalStorage.h says open HalFiles become invalid after prepareForDeepSleep(); the plan's Design Notes assume a straddling batch fails and is counted. Reading SdFat's FatFile::write after FsVolume::end(), or one device run that sleeps with a line logged just before the unmount (with the define on), settles it. The sleep flush (row 14) closes the window for sleep unless the card is slow or a task logs after the flush; the USB drive has no barrier.
    location: >-
      src/diagnostics/DiagLogStorage.cpp:StorageFileStore::append
    severity: medium (unverified)
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

Follow-up pass (2026-10-13): beyond `touches`, accepted by the orchestrator: `scripts/check_upstream_touches_test.py` (its ledger row count follows the rows, 13 then 14). Widened by the owner's Decision (Plan Change Log): `src/main.cpp`, with `lib/DiagLog/DiagLogFlush.h` (new), `DiagLog.h`, `DiagLogDevice.cpp`, ledger row 14. The follow-up lenses' step-3 subagent could not be continued, so the build agent made the patches.

## Plan Change Log

- 2026-10-13, follow-up pass: the owner's Decision (owner, 2026-10-13, entry 19's flush before sleep, option a, recorded in the epic Notes on the epic branch and sent to this build by the orchestrator) widens this entry's `touches` by `src/main.cpp`: a `DiagLog` flush before each `Storage.prepareForDeepSleep()` on the sleep paths, inside `#if CROSSHATCH_SD_LOG`, with its include and nothing else; ledger row 14. It supersedes the intent contract's Never list (`src/activities/**` and "any other upstream file", the flush as no call site) for `src/main.cpp` only; the contract text is unchanged. Known-bad state avoided: a flush with no bound (a failing card holding sleep).

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

### 2026-10-13 — Review pass (follow-up, plan `built` at a1ffc5b5)
- verdicts: 41 findings — high 0, medium 3, low 35, false 2, maybe-false 1 (plus the intent-alignment lens's descriptive report, whose divergences have rows)
- findings:
  - `[medium]` `patch` Blind Hunter 1, Edge Case 1, Verification Gap 2: `StorageFileStore::listFiles` returns true for a folder `Storage.listFiles` could not open (the SDK answers an empty vector, the same as for an empty folder), so the scan numbers the boot 1 and the next header is appended to an existing `b0001` file, and `makeRoom` deletes nothing; the fake's `failList` returns an error the device never gives — verified in `SDCardManager::listFiles`; the store now asks `Storage.exists(directory)` first and fails on a missing folder, the one case the device can report, and `FakeStoreMatchesTheDevice` pins both that and the SDK's empty answer
  - `[low]` `defer` carried — Blind Hunter 2, Edge Case 3, Verification Gap other 1: the 200-file listing cap truncates the boot scan and the cap — carried from the first pass (folder over 200 files, only by hand)
  - `[low]` `defer` carried — Blind Hunter 3, Edge Case 3: `Scan::oldestName` is 48 bytes, so a long matching name defeats the deletion — carried from the first pass (names are about 20 characters)
  - `[low]` `reject` Blind Hunter 4 (recursion): nothing stops a future `LOG_*` in the storage calls from feeding the buffer the writer drains — the calls the writer makes (`open`, `ensureDirectoryExists`, `remove`, `listFiles`, `exists`) reach only `Serial.printf` in the SDK; `HalStorage.cpp`'s `LOG_ERR` calls are in the USB-drive and `readFileToString` paths the writer never calls; a guard against a call nobody makes is more complexity than the harm shown
  - `[low]` `patch` Blind Hunter 4 (docstring): `device_log_test.py` says a card file holds what the serial port gave, but the SDK's own `Serial.printf` lines ("[SD] not initialized", "Failed to open") bypass `logPrintf` and never reach the card — docstring says so now
  - `[low]` `patch` Blind Hunter 5: `drain` held the critical section across up to 512 bytes and several records while the comment bounded it to 256 bytes — verified in `DiagLogBuffer.h`; `drain` takes the lock once per record, so every hold is one memcpy of at most 255 bytes, and the comment says an append or one drain record
  - `[low]` `patch` Blind Hunter 6 (static), Edge Case 11: `fork-scripts.md` called the 6 KiB task stack static, but `xTaskCreate` takes it and the context from the heap at the first logged line — docs reworded
  - `[low]` `reject` Blind Hunter 6 (retry and blocking): a failed `xTaskCreate` is retried by every later line, and the first line allocates inside `logPrintf` — a failing allocation returns at once (no wait), the same heap lock `logPrintf`'s own callers take; the cost of a line that cannot start the task is one failed allocation; a back-off needs a tick state and an atomic for a case (heap exhausted) where the log has little left to say
  - `[low]` `reject` Blind Hunter 7: nothing records the stack high-water mark, the buffer's peak, or the `Stats` on the card — a new feature (a footer written at close) beyond this entry; the deferred device-run item names how the cost is measured (frame times with and without the define, the dropped-lines notice)
  - `[low]` `defer` carried — Blind Hunter 8: `flush()` is public, uncalled, and its device logic untested — the same fact as the first pass's flush row; it gains a caller and a test only with the owner's Decision on the sleep barrier (deferred, medium)
  - `[low]` `defer` carried — Blind Hunter 9, Verification Gap 1: no CI job compiles the define-on code (`DiagLogDevice.cpp`, `DiagLogStorage.cpp`, the hook), and the checks on them are source-text pins — carried from the first pass; the plan's Never forbids a CI build with the define
  - `[low]` `reject` Blind Hunter 10: row 13's "image unchanged" against "ELF digest, build stamp, image digest differ", and a doubt that the library finder pulls `lib/DiagLog` into every build — the sentence says both precisely (every loaded byte is the same), `DiagLogStore.h` records why the file store is a project source, and this pass's define-off images are compared with the base's (Verification)
  - `[low]` `patch` Blind Hunter 11 (build id): `git rev-parse --short HEAD` names the commit for a dirty tree, so two devices flashed from different dirty trees would not trigger the "different firmware" warning — the recipe uses `git describe --always --dirty` in `fork-scripts.md` and `game-canvas.md`
  - `[false]` `reject` Blind Hunter 11 (LOG_LEVEL): the recipe omits that the release envs use LOG_LEVEL 1 — `game-canvas.md` already says the lines show at LOG_LEVEL 2 (development envs and the simulator) and the release envs use 1
  - `[low]` `reject` carried — Blind Hunter 11 (sticky): the recipe shows only x4pro — carried (the define and the recipe are env-independent)
  - `[low]` `reject` Blind Hunter 12 (merge writes into an existing --out, stale `<NAME>.raw.log` beside the new files): `capture` also appends into files already there; the report names the files it wrote; deleting files in a directory the user named is worse than leaving them
  - `[low]` `reject` Blind Hunter 12 (a header-only boot has its clock note at 0 ms): a boot with no log line has nothing to place; the note's text names the basis
  - `[low]` `reject` Blind Hunter 12 (a second boot with no shared STATE is placed at the previous boot's last time, hiding the sleep gap): the card has no clock, the per-boot note says how the boot was placed, and the first pass logged the same trade (Edge Case 11, rejected)
  - `[low]` `reject` Blind Hunter 12 (a stray file of another device in a card folder aborts the merge): by design (I/O matrix: exit 2 for a device on two names, naming both paths); skipping it silently would merge a wrong run
  - `[low]` `reject` Blind Hunter 13: `FakeStoreMatchesTheDevice` and `DiagLogFormatsTest` grep source text, and the fake hides short writes and name truncation — the greps are the pin the Test-doubles rule asks for, the plan says where the fake is more permissive, and the one hidden behaviour that mattered (the silent listing failure) now has its pin (row 1)
  - `[medium]` `patch` Edge Case 1: same root as Blind Hunter 1 — patched there
  - `[maybe-false]` `defer` Edge Case 2: an unmount (`prepareForDeepSleep`, the USB drive) between `Storage.open` and the write leaves a `HalFile` whose volume has ended, and the header says open HalFiles become invalid — whether SdFat returns an error, as Design Notes assume, or faults, I did not read in the SDK's SdFat; settled by reading `FatFile::write` after `FsVolume::end()` or one device run that sleeps with a line logged just before; deferred, medium unverified, beside the sleep-barrier item
  - `[low]` `defer` carried — Edge Case 3: the 200-file cap and the 48-byte name — carried (see Blind Hunter 2, 3)
  - `[low]` `reject` Edge Case 4: `esp_read_mac` failing leaves the device id `0000`, and two devices can share their last two MAC bytes, which `merge` refuses — the efuse read does not fail on this hardware, a clash is 1 in 65,536, and the error names the devices and files
  - `[low]` `reject` Edge Case 5: a failed `xTaskCreate` makes every append retry — see Blind Hunter 6
  - `[low]` `reject` Edge Case 6: a second failed batch after a partial write leaves a torn line that the next good batch continues, and a notice cut off is said twice — on a card that is failing, costs one line, counted in the `lost N bytes` notice; the fix (a segment per failure) is what the first pass removed because a flaky card then deleted the history
  - `[low]` `reject` Edge Case 7: `boot_` is read once, so a card swapped while running can make a path collide with a file on the new card — a swap while running is not supported (the buffer holds the lines, and the volume is remounted by the same card in the USB-drive case, where the folder check in `startFile` and the rising segment number avoid a collision)
  - `[low]` `reject` carried — Edge Case 8: `flush` may return true with a notice pending — carried from the first pass (Edge Case 9)
  - `[low]` `reject` Edge Case 9: "byte-identical `firmware.bin`" against the docs' "differs in digests" — the docs state the exact fact; the plan's Residual risks says the file is not bit-identical and the loaded image is
  - `[low]` `patch` Edge Case 10: the overrun test uses 4 threads where the acceptance criterion says eight — `THREADS = 8` in that test, run 200 times and in 20 full ctest runs (Verification)
  - `[low]` `patch` Edge Case 11: the 6 KiB stack described as static — see Blind Hunter 6
  - `[low]` `defer` carried — Verification Gap 1: no device-side run and no define-on build in CI — carried (see Blind Hunter 9)
  - `[medium]` `patch` Verification Gap 2: same root as Blind Hunter 1 — patched there
  - `[low]` `defer` carried — Verification Gap other 1: the 200-file cap is never exercised — carried (see Blind Hunter 2)
  - `[low]` `reject` carried — Verification Gap other 2: `FakeStoreMatchesTheDevice` skips when `freeink-sdk` is missing — carried from the first pass (patched there on purpose: skip, not fail)
  - `[low]` `defer` carried — Intent Alignment A: "a flush before sleep" lives at the sleep path and the diff's flush is exercised against a fake — carried (deferred, medium: needs a call in `src/main.cpp` and a ledger row, an owner Decision)
  - `[low]` `defer` carried — Intent Alignment B and D: "captures every line" lives at the device and the simulator, and the tests run the header-only buffer and writer; the device files are pinned by source text, and no simulator run exists — carried (deferred: define-on CI, simulator branch)
  - `[low]` `defer` carried — Intent Alignment C: `merge` is tested with card files laid out by hand, not a file the real writer wrote — carried
  - `[low]` `reject` carried — Intent Alignment E: the define-off image is not literally byte-identical — carried; the Verification section of this pass compares the loaded bytes
  - `[low]` `reject` Intent Alignment F (hook placement after the serial print, lines after the unmount not captured; the store under `src/diagnostics`; the alignment's offset estimate): all are in Design Notes with their reasons (the sleep path, the library finder, the merge alignment) and the first two in the docs; no reading of the intent excludes them
  - `[false]` `reject` carried — Intent Alignment F (summary equality by construction): carried from the first pass (the figures equal a serial capture's; the card summary replaces the `connected`/`lost` lines by design)

A follow-up pass: the four lenses ran as context-free subagents on `git diff 0b4ab005..HEAD` (the plan excluded); the first two to report were the Verification Gap and Intent Alignment lenses, the Blind Hunter and Edge Case lenses were launched in the same message but the harness backgrounded them, and triage began only after all four had reported. The step-3 implementation subagent of the first pass could not be continued, so the build agent applied the patches itself (the one medium, five lows). Host suites rerun after the patches (see Verification).

### 2026-10-13 — Review pass on the sleep flush (added at the owner's Decision after the first follow-up lenses)
- verdicts: 19 findings (the two lenses' rows merged where they made the same claim) — high 0, medium 0, low 16, false 3, maybe-false 0
- findings:
  - `[false]` `reject` Flush pass, Blind Hunter 1 and 2: `writerFailing` is cleared by an idle pump, so `flush` says true for lines a refused batch lost, depending on who runs first — a refused batch adds its bytes to `pendingLost_`, so the next pump has a notice to write and is `Wrote` or `Failed`, never `Idle`; only `Idle` with nothing pending clears the flag, and then nothing was lost
  - `[low]` `reject` Flush pass, Blind Hunter 3: `flush` waits for an empty buffer, so a task that keeps logging can hold it to the bound — bounded at 250 ms and the result is not used; a high-water mark is more state for a case that costs the bound
  - `[low]` `patch` Flush pass, Blind Hunter 4, Edge Case 1: the early-sleep paths of `setup()` can flush just after the card mounted, and the writer sits in a 500 ms retry wait that the flush cannot end, so the boot's buffered lines are lost — the retry wait is taken in 25 ms slices and ends at the next slice while a flush waits (`flushWaiting`)
  - `[low]` `reject` Flush pass, Blind Hunter 5: `ESP.restart()` sites (OTA, SD update, silent restart) lose the buffered tail — the owner Decision is the sleep paths; the docs already list the tail before a reset as not captured
  - `[low]` `reject` Flush pass, Blind Hunter 6: the result of `flushBeforeSleep()` is dropped and a timeout leaves no record — sleep must go on either way, and a record is written by the writer through the same card that just refused
  - `[low]` `reject` Flush pass, Blind Hunter 7: `git describe --always --dirty` is unrelated to the flush — it is the first pass's Blind Hunter 11 patch of this follow-up, triaged above; `device_log.py` reads the header value as an opaque string
  - `[low]` `reject` Flush pass, Blind Hunter 8: the new SDK pin in `FakeStoreMatchesTheDevice` matches whitespace and is unrelated to the flush — it is this follow-up's first patch (row 1); the other pins in the test have the same form on purpose, so an SDK change fails them
  - `[low]` `reject` Flush pass, Blind Hunter 9, Edge Case 8: the `main.cpp` text pin hard-codes three sites, `\n`, and one spelling — a new site fails the count; a helper or other spelling is not in `main.cpp` (grep: `prepareForDeepSleep` is called in `main.cpp` only, apart from the HAL's own)
  - `[false]` `reject` Flush pass, Blind Hunter 10: the new tests share `clockMs` without isolation — `Rig`'s constructor resets it to 1000, every new test builds a `Rig` first, and the wrap test restores it
  - `[false]` `reject` Flush pass, Blind Hunter 11: the heap claim in `fork-scripts.md` is unbacked — `startWriter` calls `xTaskCreate`, which takes the stack from the heap; the `DiagLogDevice.cpp` comment about 8 KB is the buffer's size
  - `[low]` `reject` Flush pass, Blind Hunter 12: row 14 does not record the awake time, the Storage lock context, or a basis for 250 ms — the row names the bound and that sleep cannot be held longer; `flush` is called from the sleep paths, which do not hold the lock; 250 ms is an estimate (a 25 ms coalesce plus a few batches) until a device run
  - `[low]` `reject` Flush pass, Edge Case 2: `flush` can say true with a drop count or a lost-bytes notice not yet written — the window is one tick between two pumps and needs drops; the writer's `Writer` would have to expose its pending state
  - `[low]` `patch` Flush pass, Edge Case 3: a refused batch followed by `NotReady` (unmount) clears `writerFailing`, so the lost lines read as flushed — the flag stays set through `NotReady`
  - `[low]` `reject` Flush pass, Edge Case 4: `xTaskNotifyGive` on a task that deleted itself after a failed allocation, between `flush`'s `writerDead` check and the notify — the task deletes itself at its first instruction when it can not allocate, long before a sleep path flushes
  - `[low]` `reject` Flush pass, Edge Case 5: lines appended after `flush` returns, before the unmount, get no second flush — the window is the unmount's own few milliseconds; the straddle it can cause is the deferred, unverified item
  - `[low]` `patch` Flush pass, Edge Case 6: the test double pumps every 5 ms while the device waits 500 ms after a refused batch or an unmounted card — the device now retries every 25 ms while a flush waits, and the double's comment says it is more permissive
  - `[low]` `patch` Flush pass, Edge Case 7: `FlushProbe::nowMs` multiplies ticks by `portTICK_PERIOD_MS` — it uses `millis()` as the writer's clock does
  - `[low]` `reject` Flush pass, Edge Case 9: the plan's Never list forbids other upstream files and call sites, and the Plan Change Log does not record the Decision — a fix that edits the plan's intent contract is rejected; the Decision is recorded in the Plan Change Log and Design Notes
  - `[low]` `reject` Flush pass, Edge Case 10: the I/O matrix says an unmounted card answers false, and `flush` says true for an empty buffer — with nothing buffered nothing is lost, and the matrix row is about lines that stay buffered

The Blind Hunter and Edge Case Hunter ran as context-free subagents on the delta of the flush (`DiagLogFlush.h`, `flushBeforeSleep`, the three calls in `src/main.cpp`, row 14, and their tests); the Verification Gap and Intent Alignment lenses did not run again, since the Decision is the intent for this delta. Patched by the build agent (four lows).

## Design Notes

- **Settled by the sources.** Hook version, define name, new ledger row 13, fork-side buffer and writer, merge by `session` and `ver`, no crash capture (Notes, Decision owner 2026-10-13); no call site changes (this entry's `stays_out`: "the hook sees every line"); `scripts/device_log.py` is entry 3's, its `LINK` formats entry 8's (`src/games/GameLinkLog.h`).
- **Unknown 1, contexts.** `logPrintf` formats with `snprintf` and prints to the CDC (neither interrupt-safe), and no interrupt handler in `src`, `lib`, or `freeink-sdk/libs` logs (the ISRs found: `epdBusyIsr`, `cstTouchInterrupt`, `onUsbSerialJtagReset`, the panic wrappers). So the hook runs in tasks on either core, the Wi-Fi tasks included. Its shared state is one critical section over a copy of at most 256 B, and two atomics. A mutex would let a low-priority task stall a game frame; a spinlock deadlocks the single-core C3 under preemption.
- **Unknown 2, boot.** Setup logs before `Storage.begin()`; those lines wait in the 8 KiB buffer and are written once `ready()`. More than that is dropped and counted; the count reaches the file. The size of boot's logging is unmeasured.
- **Unknown 3, names.** The card has no clock, so `b<boot>-s<segment>-<device>.log` in `/crosshatch-log`, boot = highest in the folder + 1. The device is the last two STA MAC bytes in upper-case hex, the nearby label's suffix. The firmware is `CROSSHATCH_BUILD_ID` (the recipe passes the short commit) else the version string, which names the commit for `default` and `sticky` only.
- **Flush before sleep.** The sleep path unmounts (`Storage.prepareForDeepSleep`) and then logs "Entering deep sleep". The first version had no barrier (this entry's `stays_out` forbade a call site) and the writer drained by itself; the owner then decided, option (a), to add one (Decision owner 2026-10-13, entry 19's flush before sleep): `DiagLog::flushBeforeSleep()` before each of the three `Storage.prepareForDeepSleep()` calls in `src/main.cpp` (`enterDeepSleep`, after `display.deepSleep()`; the ghost-wake path and the USB-power-boot path in `setup()`), each in `#if CROSSHATCH_SD_LOG`, ledger row 14. The wait is `waitUntilDrained` (`DiagLogFlush.h`, header-only so the host tests run it): true only when the buffer is empty, the writer is between batches, and the last batch was not refused; false at the bound (`SLEEP_FLUSH_TIMEOUT_MS`, 250 ms: the 25 ms coalesce plus a few 512-byte batches, an estimate until a device run), at once for an unmounted card with lines buffered, and a refused batch's lines count as gone. While a flush waits the writer retries a not-ready or refusing card every 25 ms instead of 500 ms. The result is unused: sleep goes on, so a failing card delays each path by 250 ms at most. Lines logged after the call (the "Entering deep sleep" line) cannot reach the card. The writer still closes the file after every batch.
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

**Follow-up pass (2026-10-13; tree = `a1ffc5b5` plus this pass's changes, which the follow-up commit records; no source file changed after the builds below):**
- Host: ctest 2562 of 2562 pass (39 `DiagLog*`: six new `DiagLogFlush.*`), 20 full `ctest` runs all pass, `ctest -R DiagLog --repeat until-fail:200` passes (the flake proof for the changed overrun test, now 8 threads, and the new flush tests; none failed, so no flake is claimed); every `scripts/*_test.py` passes (`check_upstream_touches_test.py` counts 14 rows); `check_layers.py` passes; `check_upstream_touches.py` passes (run again on the commit, below); `./bin/clang-format-fix` twice changed nothing after the last edit.
- Define off, `firmware.bin` against the base commit's (`0b4ab005`, built first on a clean tree): x4pro 6,255,680 B and 6,255,680 B; default 5,927,792 B and 5,927,792 B; sticky 6,138,128 B and 6,138,128 B. Every differing byte is outside code and data that run: the app descriptor's ELF digest and build time, the version string (`...-g0b4ab005` against `...-ga1ffc5b5-dirty`, three copies in default and sticky, none in x4pro), and the image digest. Compared by a byte diff of each pair; the loaded contents are the same.
- Define on (`-DCROSSHATCH_SD_LOG=1 -DCROSSHATCH_BUILD_ID=0b4ab005`), the same builds at the same tree: x4pro 6,268,640 B (+12,960 B over the base's 6,255,680), RAM 112,560 B; default (C3, no PSRAM) 5,941,184 B (+13,392 B over 5,927,792), RAM 67,432 B (+8,264 B over the define-off 59,168); x4pro with `-DCROSSHATCH_TOUCH_LOG=1` too 6,269,440 B. All build. The flush added about 370 B on x4pro over the first pass's 12,592 B.
- `pio check` (low, medium, high) passes for `default` and `x4pro`, with and without the define.
- Flash budget (`check_flash_budget.py`, define off, this tree): games on 6,255,680 B, games off 5,957,328 B, +298,352 B (limit 307,200 B, 8,848 B spare); static RAM +864 B (limit 1,024 B, 160 B spare): identical to the first pass's figures, so the epic's growth is still +40,304 B and +80 B and normal builds did not move. `objects` and `touch-line` pass.
- Simulator and screenshots: not run (the simulator still cannot build the define-on code; no screen changed).

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

**Follow-up pass (2026-10-13), a fresh review of `0b4ab005..a1ffc5b5` by the four lenses as context-free subagents.** Summary: 41 findings (medium 3, low 35, false 2, maybe-false 1; the three mediums share one root). Patched: the folder listing that answered "empty" for a folder the store could not read (the store now asks `exists()` first; a pin of the SDK's behaviour), the buffer's lock held across a whole drain (now one record per hold), the 8-thread overrun test (was 4), the recipe's build id (`git describe --always --dirty`), and three doc and docstring corrections. Carried (not re-patched or re-deferred): 18 rows from the first pass. Deferred: one new item, the unmount between the writer's open and write (medium, unverified). Rejected: each with its reason in the log.

The owner's Decision (flush before sleep, option a) was applied in this pass: `DiagLog::flushBeforeSleep()` (bounded, 250 ms) before each of the three `Storage.prepareForDeepSleep()` calls in `src/main.cpp`, in `#if CROSSHATCH_SD_LOG`; ledger row 14; the wait in `lib/DiagLog/DiagLogFlush.h` with six host tests (drains to the store before returning, the bound, a refused card, an unmounted card, the clock wrap, a pin of the three call sites); the first deferred medium (no barrier) is closed. The two lenses that reviewed the delta found only lows (four patched). Files added or changed in this pass: `src/main.cpp` (upstream, row 14), `lib/DiagLog/{DiagLogFlush.h,DiagLog.h,DiagLogDevice.cpp,DiagLogBuffer.h}`, `src/diagnostics/DiagLogStorage.cpp`, `test/diag_log/*`, `scripts/check_upstream_touches_test.py` (outside `touches`, accepted by the orchestrator), `scripts/device_log_test.py` (docstring), `docs/crosshatch/{upstream-touches,fork-scripts,game-canvas}.md`, this plan.

Follow-up review recommended: false (this pass patched no high; the rule). Verification: see Verification, Follow-up pass. Formatting: `./bin/clang-format-fix` changed nothing outside this story's paths. Residual risks: the writer on a real card (keep-up, stack, effect on frame times) and the flush's 250 ms bound are unmeasured; the unmount between the writer's open and write (deferred, unverified); a line logged after the flush and before the unmount; `ESP.restart()` paths lose the buffered tail.
