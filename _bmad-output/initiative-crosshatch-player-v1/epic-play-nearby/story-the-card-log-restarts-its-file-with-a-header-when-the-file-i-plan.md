---
title: 'The card log restarts its file with a header when the file is deleted mid-boot'
type: 'bugfix'
ticket: '22'
created: '2026-10-13'
status: done
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/.agents/skills/hal-and-abstractions/SKILL.md'
  - '{project-root}/.agents/skills/control-flow-clarity/SKILL.md'
warnings: [oversized]
deferred: []
baseline_revision: '1f5c0b33f79bb2527ee8f27409aa75d2f7188e6c'
---

<intent-contract>

## Intent

**Problem:** In entry 11's smoke run (2026-10-13, device 2C8C) the owner deleted the card logs from the device's Wi-Fi file manager while it ran. The writer's next batch reopened the boot's segment with `O_CREAT | O_APPEND` (`StorageFileStore::append`) and recreated `b0002-s00-2C8C.log` with no header (its first line `[148701] [DBG] [WEB] Served jszip.min.js`), so `scripts/device_log.py merge` refuses the whole card (exit 2) until the file is removed by hand.

**Approach:** A batch never creates a file; only the header write does. A batch whose file is gone is a failed write on the existing failure path: its lines are lost, the next segment starts with a header, and that file says `[ERR] [DIAG] lost N bytes (card write failed)`. So a deletion mid-run costs one batch and every file on the card stays mergeable. `fork-scripts.md`'s cardless run says so.

## Boundaries & Constraints

**Always:** Every file the writer makes starts with `# crosshatch-diag-log 1`. The device store goes through `Storage` / `HalFile` only. Every existing `DiagLogWriter` / `DiagLogFlush` / `DiagLogMain` test keeps passing; one that must change says why in Implementation Notes. The fake store names the device behaviour it stands in for, and a test pins it to the device store's open flags. With `CROSSHATCH_SD_LOG` off the loaded images of `x4pro`, `default`, and `sticky` equal the base commit's (row 13's rule).

**Never:** Touch an upstream file, `scripts/device_log.py` (merge keeps refusing a headerless file), `platformio.ini`, `games/**`, `src/games/**`, or `src/activities/**`. Never retry a batch, and never write a header into a file that already holds lines.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Deleted between batches | s00 written; every file in the folder deleted; a batch | that batch `Failed`, no file recreated; next pump makes s01 with its header, the lost notice (the batch's bytes), then the new lines | existing failure path: `writeFailures`, `bytesLost` count it |
| Deleted during a refusal streak | card takes headers, refuses batches (s01 started, stays); s01 deleted; card recovers | the next batch finds s01 missing and starts s02 with a header; no wedge on a missing file | a missing file always starts a new segment, streak or not |
| Card refuses a batch, file present | existing tests | unchanged: new segment once per streak | unchanged |
| Header write | new segment | `create` makes the file and writes the header | torn header removed (unchanged) |

</intent-contract>

## Code Map

- `lib/DiagLog/DiagLogWriter.h` -- `FileStore` (the card as the writer sees it), `Writer::pump` (batch append and its failure path, `lastBatchFailed_` streak rule), `Writer::startFile` (header write, torn-header `remove`). Header-only; the host suite compiles it.
- `src/diagnostics/DiagLogStorage.cpp` -- `StorageFileStore`, the device store, entirely inside `#if CROSSHATCH_SD_LOG`; `append` opens `O_WRONLY | O_CREAT | O_APPEND` today.
- `test/diag_log/DiagLogWriterTest.cpp` -- `FakeStore` (its comment names what it copies from the device; `failAppends`, `failAppendsAlways`, `failBatches` knobs count every `append` call, the header's included), `FakeStoreMatchesTheDevice` (text pin of the device file, `Storage.open(path, O_WRONLY | O_CREAT | O_APPEND)`).
- `docs/crosshatch/fork-scripts.md` -- `### The run without a computer`, the "What it says it lost" bullet.
- Read-only: `lib/hal/HalStorage.h` (`open`, `exists`); SdFat 2.3.1 (`platformio.ini:166`) `FatFile::open` (`FatLib/FatFileLFN.cpp`, `create:` step) and `ExFatFile::open` (`ExFatLib/ExFatFile.cpp`, `create:`), which `goto fail` without creating when `O_CREAT` is absent; `HalFile::operator bool` is `isOpen()`.

## Tasks & Acceptance

**Execution:**
- [x] `lib/DiagLog/DiagLogWriter.h` -- Split the store's write in two: `create(path, data, length)` (bool; creates when missing and writes at the end, used only for a header) and `append(path, data, length)` returning a small enum `Written` / `Missing` / `Failed` that never creates. `startFile` writes the header with `create`; `pump` treats any non-`Written` result as today's failure, and a `Missing` result starts a new segment even inside a refusal streak. Update the file's header comment ("Writes.") and the `FileStore` comments -- the only header write creates a file; a deleted file costs one batch.
- [x] `src/diagnostics/DiagLogStorage.cpp` -- `create` keeps today's `O_WRONLY | O_CREAT | O_APPEND`; `append` opens `O_WRONLY | O_APPEND` and, when the open fails, answers `Missing` if the card is ready and `!Storage.exists(path)`, else `Failed`. Share the write-flush-close tail. Comment the SdFat fact (an open without `O_CREAT` fails and creates nothing).
- [x] `test/diag_log/DiagLogWriterTest.cpp` -- `FakeStore`: `create` as today's `append`; `append` answers `Missing` for a missing file (before the failure knobs, as the device's open fails first), otherwise the knobs as today; both count in `appends` and share the knobs, so existing tests run unchanged. Update its comment. New tests: the I/O matrix's first two rows (assert every file in the store starts with the magic line, the lost notice's byte count, and that no file is recreated by the failed batch); a fake-double test that `append` on a missing file returns `Missing` and creates nothing while `create` creates. `FakeStoreMatchesTheDevice`: pin both opens' flags (`O_WRONLY | O_CREAT | O_APPEND` in `create`, `O_WRONLY | O_APPEND` in `append`, `O_CREAT` appearing exactly once in the file) and `Storage.exists(path)`; say the reason for the change in Implementation Notes.
- [x] `docs/crosshatch/fork-scripts.md` -- In the cardless run, say deleting the logs (or the folder) from the device's file manager mid-run is safe: the next batch is lost and said as `lost N bytes`, the next file starts with its header, every file stays mergeable; it costs one batch (at most 512 bytes of lines).

**Acceptance Criteria:**
- Given a writer that has written s00, when every file in the folder is deleted and the next batch runs, then no file appears, the batch counts as one write failure, and the next pump's file is s01 whose first line is `# crosshatch-diag-log 1` and which carries `[ERR] [DIAG] lost N bytes (card write failed)` with N the batch's line bytes.
- Given the fake store, when `append` targets a missing file, then it returns `Missing` and creates nothing, and `FakeStoreMatchesTheDevice` fails if the device's `append` open gains `O_CREAT`.
- Given every env built with the define off, when its `firmware.bin` is compared with the base commit's, then only the app descriptor's stamps and digests and the image trailer differ.
- Given `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1`, when `x4pro` and `default` build, then both succeed.

## Implementation Notes

- `FileStore::Appended` (`Written` / `Missing` / `Failed`) is nested in `FileStore`. `pump` keeps one failure path for any non-`Written` result; `fileGone` (`Missing`) only widens the streak rule's `if (!lastBatchFailed_ || fileGone)`, so a refused batch on a present file keeps today's one-segment-per-streak behaviour.
- `StorageFileStore`: `create` keeps `O_WRONLY | O_CREAT | O_APPEND`; `append` opens `O_WRONLY | O_APPEND` and, on a failed open, answers `Missing` only when `Storage.ready() && !Storage.exists(path)`, else `Failed`. The shared tail is `writeAndClose` (text `written == length && closed` unchanged, so the existing pin holds). The SdFat comment says "the create flag" rather than the token, so `O_CREAT` appears once in the file, as the pin requires.
- `FakeStore`: `create` and `append` share `write` (today's append body: knobs, `appends++`, `failBatches` by the magic prefix); `append` answers `Missing` for a missing path before the knobs and counts it in `appends`. No existing test changed: every existing batch targets a file its header made, so none reaches `Missing`.
- `FakeStoreMatchesTheDevice` changed because the device's one open became two: it now pins `create`'s `O_WRONLY | O_CREAT | O_APPEND`, `append`'s `O_WRONLY | O_APPEND`, `O_CREAT` appearing exactly once (`find == rfind`), and `!Storage.exists(path)`. Adding `O_CREAT` to `append`'s open fails it (checked by hand).
- New tests: `FilesDeletedBetweenBatchesCostOneBatchAndTheNextFileStartsWithItsHeader` (matrix row 1), `AFileDeletedDuringARefusalStreakStartsANewSegmentWhenTheCardRecovers` (row 2), `TheFakeAppendAnswersMissingAndMakesNothingWhileCreateMakesTheFile`.
- Mutation checks, reverted: the fake `append` creating a missing file turns all three new tests red; the writer ignoring `Missing` in a streak turns the row 2 test red.
- Review fixes (2026-10-13, review pass 1, applied by the same implementation subagent): `Writer` gains `streakFileGone_`, so `Missing` starts a new segment only when `!lastBatchFailed_ || (fileGone && !streakFileGone_)`: a streak costs at most one extra segment, plus one more if its file is gone. SdFat's `exists` is an `O_RDONLY` open, so a card that refuses opens also answers `Missing`, and without the bound it would start a file, and delete the oldest, on every batch. New `FakeStore::appendsMissing` knob (standing in for that card) and test `ACardThatRefusesOpensAndSaysMissingCostsAtMostTwoSegmentsNotAFilePerTry`; putting back the unbounded `|| fileGone` turns it red. `FakeStoreMatchesTheDevice` also pins the exact `Missing` / `Failed` return line, and its `O_CREAT`-once check now has a failure message. `FilesDeletedBetweenBatches...` asserts the folder is made again before the new file. The comments on `Step::Failed` and on the device `append` are corrected, and the doc names the `(card write failed)` notice. Design Notes' "bounded by `maxFiles`" sentence is superseded by this bound.

## Plan Change Log

## Review Triage Log

### 2026-10-13 — Review pass
- verdicts: 18 findings — high 0, medium 2, low 9, false 5, maybe-false 2
- lenses: Blind Hunter, Edge Case Hunter, Verification Gap, and Intent Alignment, each run as a context-free subagent over `git diff 1f5c0b33 -- . ':(exclude)_bmad-output'` (the plan went only to the Edge Case Hunter, as its claims file). Grouped entries: G1 = Blind Hunter 2 and Edge Case 1 to 3 (`Missing` decided by `exists`); G2 = Blind Hunter 4 and Intent Alignment 1 (folder deletion untested).
- findings:
  - `[low]` `patch` Blind Hunter 1: a deliberate deletion is reported as `lost N bytes (card write failed)`, and the doc did not say so. The notice text is the intent's ("the next file says `[ERR] [DIAG] lost N bytes (card write failed)`"), so no new notice was added. The doc now says the deletion is reported with the same `(card write failed)` notice as a refused write.
  - `[medium]` `patch` Blind Hunter 2 (G1): `Missing` bypassed the streak rule without bound. SdFat's `exists` is `tmp.open(path, O_RDONLY)` (`FatVolume.h:107`, `ExFatVolume.h:105`), so a mounted card that refuses opens answers `Missing` on every batch. Each one started a segment, and `makeRoom` deleted the oldest file every time, which is the harm the streak rule prevents. Fixed: a streak takes at most one segment for a missing file (`streakFileGone_`), with a test that fails on the unbounded condition.
  - `[maybe-false]` `reject` Blind Hunter 3: a delete landing between a batch's open and its write. `Storage` locks per call, so the window exists, but it predates this change (the `O_CREAT` open had the same window). SdFat's sync refuses a deleted entry (`FatFile.cpp:1240`), so the batch is `Failed` and takes the existing failure path. If true, the worst outcome is a few orphaned clusters, which is low, so it is rejected. A device run that deletes during heavy logging, followed by a `chkdsk`, would settle it.
  - `[low]` `patch` Blind Hunter 4 (G2): the doc says deleting the folder is safe, but no test shows the folder made again. For the writer, a deleted folder is the same as deleted files (`append` gives `Missing`). Its answer, `makeDirectory` before every file, is now asserted in `FilesDeletedBetweenBatches...` (`directories.size() == 2`). Modelling folders in the fake would add a branch the writer does not have.
  - `[low]` `reject` Blind Hunter 5: `create` appends rather than truncating, so a path that already exists keeps its bytes. This predates the change: the header write used the same flags. It is reachable only when the torn-header `remove` fails, or when a boot number is reused past the 200-file listing cap with `maxFiles` 16. Truncating would destroy a colliding file's data instead. Design Notes record the choice.
  - `[false]` `reject` Blind Hunter 6: the claim is that the fake's knobs no longer fit. The class comment says `Missing` comes "before any refusal", as on the device, where the open fails first. The "share the failure knobs" comment sits on `write`, which both calls use. `failBatches` still tells a header by its magic, as before. The old `append` never checked `mounted` either. No test outcome differs.
  - `[low]` `patch` Blind Hunter 7: the `Step::Failed` comment did not mention a gone file. It now says "or a batch's file is gone". A separate stat was not added: the intent makes a deletion the existing failure path.
  - `[false]` `reject` Blind Hunter 8: "at most 512 bytes of lines" holds. A batch is at most `CHUNK_BYTES` (512) including notices, and the same section already gives the 512-byte batch.
  - `[low]` `patch` Blind Hunter 9: the `O_CREAT`-once check would fail without explanation if a comment ever named the flag. It now says "the create flag belongs only in create's open".
  - `[medium]` `patch` Edge Case 1 (G1): a card I/O error that fails both the open and `exists` reads as `Missing`, and a segment started per batch. The same root cause and the same fix as Blind Hunter 2.
  - `[low]` `patch` Edge Case 2 (G1): an unmount between `ready()` and `exists()` reads as `Missing`. With the bound it costs at most one segment per streak. An unmount is a sleep (the MCU resets) or the USB drive (no writes while lent).
  - `[low]` `patch` Edge Case 3 (G1): the device comment claimed "a card that ... refused the open is Failed", which is false because `exists` is itself an open. The comment now says a refusing card reads as `Missing` too, and that the writer bounds it.
  - `[low]` `patch` Verification Gap 1: `StorageFileStore::append` is checked only by text pins, and swapping `Missing` and `Failed` in its return passed every pin. The exact return line is now pinned, so that swap fails `FakeStoreMatchesTheDevice`. A compiled host test of the device store, against a `HalStorage` stub, stays with entry 19's deferral ("no CI job compiles the define-on code"). The intent asks for a pin to the device's open flags. This run's define-on builds compile it (Verification).
  - `[low]` `patch` Intent Alignment 1 (G2): the folder deletion is documented but not exercised. The same fix as Blind Hunter 4.
  - `[false]` `reject` Intent Alignment 2: the streak test loses three batches, not one. Two of them are the card's refused writes, from the streak the test sets up. The deletion's own cost is one batch, which `FilesDeletedBetweenBatches...` pins exactly.
  - `[false]` `reject` Intent Alignment 3: the `FakeStore` and `FakeStoreMatchesTheDevice` changes are stated in Implementation Notes, as the Verify allows ("or with a stated reason"). Every other existing test is unchanged and passes.
  - `[false]` `reject` Intent Alignment 4: the diff carries no builds or image comparison. By the verification order those run after the review, and their results are under Verification.
  - `[maybe-false]` `reject` Intent Alignment 5: the timing windows inside one batch are untested. This is the same window as Blind Hunter 3, rejected on the same note. Neither path remakes a file without its header, which is the intent's invariant.

## Design Notes

- **Settled by the sources.** The fix shape (a batch never creates; a missing file is the existing failure path; only the header write creates; the doc sentence) is entry 22's description. The define is `CROSSHATCH_SD_LOG`, tested with `#if`, off by default (orchestrator's note). The define-off image rule and its method are row 13's and entries 19 and 21's (entry 21's Verification: build the base first on a clean tree, then compare each `firmware.bin` by its segment map with `cmpimg.py`). `touches` and `stays_out` are entry 22's.
- **Unknown 1, the interface: a second operation, not a flag.** `create` for the header and `append` for a batch. Each call site then names what it means, the device's two opens are two fixed flag sets the test pins by text, and the fake's two methods mirror them. A flag on `append` would put the create choice back at one call with a bool argument.
- **`append` says `Missing` as well as failure (a small enum, not a bool).** Without it, a deletion during a refusal streak wedges the boot: the streak rule (`lastBatchFailed_`, which protects a card that takes headers and refuses batches from a new file, and a deletion of the oldest, on every retry) keeps the writer on the file it started, and with no `O_CREAT` every later batch would fail on the missing file for the rest of the boot. `Missing` bypasses only that rule; a refused batch on a present file keeps it. A card whose `exists` wrongly says missing while `create` works would start a segment per batch (superseded at review: SdFat's `exists` is itself an open, so the writer bounds `Missing` to one segment per streak; Implementation Notes).
- **Unknown 2, SdFat.** `Storage.open` is `SDCard.open` -> `FsVolume::open` -> `FsBaseFile::open`, which calls `FatFile::open` or `ExFatFile::open`; a name not found reaches their `create:` step, which `goto fail`s (returns false, nothing written) when `O_CREAT` is absent (`FatFileLFN.cpp`'s "don't create unless O_CREAT and write mode", `ExFatFile.cpp:432`). `USE_DBG_MACROS` is 0, so it prints nothing. The `HalFile` is then not open (`operator bool` is `isOpen()`). A missing folder fails the path walk the same way.
- **Guards kept.** `startFile`'s best-effort `remove` after a failed header (a torn header must not be glued to the retry's), `makeDirectory` per file (the folder can vanish), and the streak rule for present files all stay. `create` keeps `O_APPEND` rather than truncating: no batch reaches a file before its header now, so the only earlier content is a torn header, which the `remove` handles.

## Verification

**Commands (implementation, host and fast checks):**
- Under `flock /tmp/crosshatch-hosttest.lock`: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, the new tests included. Mutation check by hand, then reverted: the fake `append` creating a missing file, and the writer ignoring `Missing` in a streak, each turn a new test red.
- `for t in scripts/*_test.py; do python3 $t; done`; `python3 scripts/check_upstream_touches.py`; `python3 scripts/check_layers.py`; `./bin/clang-format-fix` twice -- expected: pass, the second run changes nothing.

**Commands (once, after the review, under `/tmp/crosshatch-build.lock`, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`):**
- `pio run -e x4pro`, `-e default`, `-e sticky` without the define -- expected: each `firmware.bin` equals the base commit's (built before any change, scratch `6.22/base/`) except the app descriptor's stamps and digests and the trailer (`cmpimg.py`, exit 0).
- `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1 pio run -e x4pro` and `-e default` -- expected: both build.
- `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for `default` and `-e x4pro`, each with and without `-DCROSSHATCH_SD_LOG=1` -- expected: no defect.
- Not run: `check_flash_budget.py` (the define-off image is compared byte for byte instead), `sim.sh build x4pro` (no screen or game code).

**Manual checks:**
- Device check waits for entry 14's run: deleting the logs from the file manager mid-run leaves every card file starting with the header and one `lost N bytes` notice.

**Results (2026-10-13).** The base is `1f5c0b33f79bb2527ee8f27409aa75d2f7188e6c`. Its define-off `x4pro`, `default`, and `sticky` firmware were built first, on the clean tree, before any change, under the build lock with the shared cache (scratch `6.22/base/`: `x4pro` 6,255,680 B, `default` 5,927,792 B, `sticky` 6,138,128 B, all exit 0; `build_base.sh`).
- **Host.** After the review patches, `ctest` passes 2583 of 2583 (the two `Dumps...ForReviewWhenAsked` tests skip, as on every run). The new tests pass: `FilesDeletedBetweenBatches...`, `AFileDeletedDuringARefusalStreak...`, `ACardThatRefusesOpensAndSaysMissing...`, and `TheFakeAppendAnswersMissing...`. The changed `FakeStoreMatchesTheDevice` passes too. Mutation checks are in Implementation Notes.
- **Fast checks.** Every `scripts/*_test.py` passes, as do `check_upstream_touches.py` (PASS) and `check_layers.py`. `./bin/clang-format-fix` ran twice after the last edit and changed nothing.
- **Firmware: not run (blocked).** `6.22/build_new.sh` ran under the build lock after the review. Its first build (`x4pro`, define off) began with pioarduino's "Reinstall Arduino framework". That step was not in any base log, so something between the two runs had changed the shared framework. The reinstall failed with `[Errno 28] No space left on device` while unpacking `esp32-core-3.3.11-libs.tar.xz`. It left `~/.platformio/packages/framework-arduinoespressif32-libs` half-installed: `package.json` 0 bytes, only `esp32`, `esp32h2` (partial), `esp32s2`, `esp32s3`, and `hosted` present. All nine steps then failed, the five builds and the four `pio check` runs (`MissingPackageManifestError`). `df` shows 2.8 GB free. The largest users are the main checkout's shared build cache (13 GB) and `~/.platformio` (10 GB). The define-off image comparison, the define-on builds, and `pio check` are all still owed.
- **Firmware, second attempt (after the orchestrator freed disk and reinstalled the framework package).** `6.22/build_new.sh` now has a watchdog that kills a step whose log shows "Reinstall Arduino framework".
  - **Define off: the loaded image equals the base's.** Each `firmware.bin` was compared with `cmpimg.py` against the base build at `1f5c0b33`.
    - `x4pro` (6,255,680 B): 0 bytes differ.
    - `default` (5,927,792 B): 79 bytes differ, all in the descriptor's `app_elf_sha256`, `time`, and `version` and in the trailer.
    - `sticky` (6,138,128 B): 76 bytes differ, in the same fields.
  - **Stopped.** The next step, `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1 pio run -e x4pro`, printed `*** Reinstall Arduino framework ***` right after its PACKAGES list, and the watchdog killed it before anything was installed. The package was left intact. `sticky`'s build (`*** Add "custom_sdkconfig" settings to IDF sdkconfig.defaults ***`) leaves an `sdkconfig` in `framework-arduinoespressif32-libs`, and the next non-custom env reinstalls the framework. The first attempt's failed reinstall followed the base run's `sticky` build the same way.
  - Still owed: the define-on `x4pro` and `default` builds and `pio check` four ways.
- **Firmware, third attempt (2026-10-13, after the orchestrator let one framework reinstall complete; 12 GB free; `6.22/build_rest.sh`, which stops a step below 5 GB free; no reinstall step in any log).** The define-on builds, with `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_SD_LOG=1`, both succeed:
  - `x4pro`: 6,268,784 B, RAM 112,560 B. `nm` shows `StorageFileStore::create` and `StorageFileStore::append` in the ELF.
  - `default` (the C3): 5,941,344 B, RAM 67,432 B.

  `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (cppcheck) PASSED for `default` and `x4pro`, each with and without the define. Every check in the plan's Verification has now run and passed. `sticky` was not rebuilt: its define-off comparison was already done.

## Auto Run Result

**Status: built.** The first post-review firmware run was blocked: a full disk half-installed the shared framework package, which the orchestrator repaired (Verification, Results). The ticket's `blocked` mark is the orchestrator's to clear.

**Summary.** A batch no longer makes a file. `FileStore` is split in two. `create` is used only for a file's header and opens `O_WRONLY | O_CREAT | O_APPEND`. `append` returns `Written` / `Missing` / `Failed` and opens `O_WRONLY | O_APPEND`. A batch whose file was deleted mid-run takes the existing failure path: its lines are lost, the next segment starts with its header, and that file says `[ERR] [DIAG] lost N bytes (card write failed)`. So a deletion costs one batch, and every file on the card stays mergeable. A missing file starts a new segment even inside a refusal streak, at most once per streak. Without that, the boot would wedge on a missing file; without the "once", a card that refuses opens would start a file per batch, because SdFat's `exists` is itself an open. Unknown 1: a second operation, not a flag. Unknown 2: SdFat's `FatFile::open` and `ExFatFile::open` fail and create nothing without `O_CREAT` (Design Notes).

**Files.**
- `lib/DiagLog/DiagLogWriter.h`: `FileStore::create` and `FileStore::Appended append`, the header written with `create`, the missing-file branch and its once-per-streak bound (`streakFileGone_`), and the comments.
- `src/diagnostics/DiagLogStorage.cpp`: the device `create` (with `O_CREAT`) and `append` (without it; `Missing` when the open fails and `exists` says no), sharing `writeAndClose`.
- `test/diag_log/DiagLogWriterTest.cpp`: `FakeStore` with `create`, `append`, and the `appendsMissing` knob; four new tests; `FakeStoreMatchesTheDevice` pinning both opens, the single `O_CREAT`, and the exact `Missing` / `Failed` line (reason in Implementation Notes).
- `docs/crosshatch/fork-scripts.md`: the cardless run says that deleting the logs or their folder mid-run is safe, and that it costs one batch, reported with the `(card write failed)` notice.
- This plan.

**Review.** The four lenses ran as context-free subagents and reported 18 findings: high 0, medium 2, low 9, false 5, maybe-false 2.
- Patched, 6 entries:
  - G1 (medium): `Missing` decided by `exists`, which is an open, is now bounded to one segment per streak, with a test.
  - G2 (low): the folder made again before the new file is asserted.
  - The doc names the `(card write failed)` notice (low).
  - The `Step::Failed` comment (low).
  - The `O_CREAT`-once failure message (low).
  - The exact `Missing` / `Failed` line pinned (low).
- Deferred: none.
- Rejected, each with its reason in the Review Triage Log:
  - the delete-inside-a-batch window (maybe-false; it predates this change, and at worst it orphans clusters);
  - `create` not truncating (predates this change; the Design Notes give the choice);
  - the fake's knobs (false);
  - the 512-byte figure (false);
  - the intent lens's three descriptive notes (false).
- Follow-up review recommended: false. One medium entry and five low entries were patched, and no high.

**Verification.** Host tests: 2583 of 2583 pass. The script, ledger, layers, and format checks pass. With the define off, the `x4pro`, `default`, and `sticky` images match the base's: `x4pro` byte for byte, and the other two differ only in descriptor stamps and digests. With the define on, `x4pro` and `default` build. `pio check` passes four ways. Details are under Verification, Results.

**Formatting.** `./bin/clang-format-fix` changed nothing outside this story's paths.

**Residual risks.**
- No CI job compiles the define-on store code (entry 19's deferral). This run's define-on `x4pro` and `default` builds compile it, and the text pins guard its flags and its return line.
- No device has run it. Deleting from the file manager mid-run waits for entry 14's run.
- A delete that lands between a batch's open and its write was not covered. It predates this change and is rejected in the log.
