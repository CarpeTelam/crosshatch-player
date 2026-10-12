---
title: 'Two-device serial capture and the default-build touch-line check'
type: 'feature'
ticket: '3'
created: '2026-10-12'
status: done
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/docs/crosshatch/fork-scripts.md'
  - '{project-root}/scripts/fork_common.py'
baseline_revision: 'c05455736ea2f3f9f0c1e15c66d18156dc2bc39d'
warnings: [oversized]
deferred:
  - summary: >-
      device_log.py opens a port without an exclusive lock (no TIOCEXCL), so ModemManager, `pio device monitor`, or a second capture can take bytes from the same port and leave holes in a raw log.
    evidence: |-
      Real on Linux, not observable on a pty, and the fix (TIOCEXCL, which makes another non-root open fail with EBUSY) changes what a user sees on a real X4 Pro CDC port, which no run has measured. The docs now name ModemManager and the dialout group. Settle it in entry 11's smoke run: capture with and without `pio device monitor` open.
    location: >-
      scripts/device_log.py:open_port
    severity: low
  - summary: >-
      The DTR/RTS clearing and the reconnect after a real sleep are unobserved on hardware.
    evidence: |-
      A pty refuses TIOCMBIC (the error is suppressed) and closes with end-of-file at once, with no sleep timing or USB renumbering; a mock of the ioctl would verify nothing. The unknown in the ticket stays open until the first two-device run (entry 11's smoke run): check that connecting does not reset the board and that each sleep produces a lost and a connected line.
    location: >-
      scripts/device_log.py:open_port, Capture.run
    severity: low
---

<intent-contract>

## Intent

**Problem:** A two-device run (epic-play-nearby R12, R14) needs two serial logs the owner now keeps by hand, with no shared clock, and nothing in CI fails when the games-on default build holds the per-tap touch log line (`## e8-touch-log`, first item).

**Approach:** Add `scripts/device_log.py` (R13), standard library only: `capture` reads two or more ports at once, reconnects by itself, writes one merged log (host timestamp and device name per line) beside each device's raw log unchanged; `summary` lists the packet's figures. Add a `touch-line` command to `scripts/check_flash_budget.py` that fails when the games-on ELF holds the touch line's format text, run by the flash-budget job. Document a two-device run in `docs/crosshatch/fork-scripts.md`.

## Boundaries & Constraints

**Always:** Standard library only (`termios`, `os`, `select`; no pyserial: a need for it is an owner exception, so stop and report). `fork_common`'s exit contract (0 passed, 1 rule broken, 2 could not run), single quotes, sidecar tests that need no serial device. Raw logs keep every byte the port gave, in order, across reconnects. Edit only `scripts/device_log.py`, `scripts/device_log_test.py`, `scripts/check_flash_budget.py`, `scripts/check_flash_budget_test.py`, the `flash-budget` job of `.github/workflows/crosshatch-ci.yml` (and its comment bullet), `docs/crosshatch/fork-scripts.md`.

**Never:** Touch `src/**`, `lib/**`, `test/**`, `games/**`, `ci.yml`, or any upstream file. Add the `LINK`, `PeerGone`, `ABORT` lines to the summary (entry 11). Add a CI build with `-DCROSSHATCH_TOUCH_LOG=1` (owner Decision, inception Q4). Add a non-standard-library module.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Two ports talk | A and B lines interleaved | merged lines `<UTC ms stamp> <NAME> <text>` in host-time order (stamps never decrease); each raw log byte-identical to what its port sent | n/a |
| Port vanishes and returns | path disappears (sleep), later reappears | merged log gets a `[device_log] lost` then `connected` line for that device; the other device keeps flowing; a half line at the loss is flushed to the merged log; raw log untouched | reopen every 0.5 s, no exit |
| Path never opens or is not a tty | missing path, `/dev/null` | waits and retries, one stderr note per distinct reason | no crash |
| Bad arguments | bad `NAME=PATH`, duplicate name or path, unknown baud | message on stderr | exit 2 |
| Summary of a log | raw device log or the merged log | per device: connects/losses, each `VM stopped` with its game, dropped touches per game with hand-off count, `[MEM]` figures | unreadable file exit 2 |
| Touch line absent | games-on ELF without `%s: touch %s` | PASS, summary says so | n/a |
| Touch line present | ELF built with `-DCROSSHATCH_TOUCH_LOG=1` | FAIL (exit 1), names the offset and the define | n/a |
| Cannot check | no ELF or empty ELF, off-state metadata, source no longer holds the format | n/a | exit 2 |

</intent-contract>

## Code Map

- `scripts/fork_common.py` -- import `PASS`, `FAIL`, `SetupError`, `exit_code`, `write_step_summary`; define nothing it has. Read-only.
- `scripts/check_flash_budget.py` -- model for structure; `load_build(metadata_dir, 'on')`, `check_flag`, `main`'s subparsers and `heading`; add `touch-line` beside `objects`.
- `scripts/check_flash_budget_test.py` -- fixture style: `ObjectsTest` (temp project, metadata via `cfb.metadata_path`, `run_cli` with `GITHUB_STEP_SUMMARY`), `ON_DEFINES`, `test_workflow_*` readers of `crosshatch-ci.yml`.
- `.github/workflows/crosshatch-ci.yml` -- `flash-budget` job: steps `build-on` (id), `Build ... off`, `Compare`, `Check game objects` (`if: ${{ !cancelled() && steps.build-on.outcome == 'success' }}`); its comment bullet at the top. `flash-budget` is already in `Crosshatch Test Status`'s `needs`.
- `src/activities/games/GameMatchActivity.cpp:536` (read-only) -- `LOG_DBG("GAME", "%s: touch %s", ...)` inside `#ifdef CROSSHATCH_TOUCH_LOG`; `lib/Logging/Logging.h` appends `"\n"` to the format, so the ELF holds `%s: touch %s\n`.
- Device log formats (read-only; the summary's parsers): line prefix `[<millis>] [<LVL>] [<origin>] ` (`lib/Logging/Logging.cpp`). `GameVM.cpp:239` `VM stopped; arena peak %u bytes, stack high-water %u bytes free, least at a hook %u bytes` (carries no game id). `GameMatchActivity.cpp:268` `%s: %s -> %s on %s` (lifecycle; game id first). `:557` `%s: dropped a touch that began %d ms before the hand-off passed`, `:561` `%s: dropped a touch before the frame was on the panel`, `:846` `%s: hand-off screen pushed in %u ms`. `main.cpp:715` `Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes` and `:719` the same after `PSRAM: ` (origin `MEM`, so `[MEM] Free: ...`). `CrossPointWebServer.cpp` also logs `[WEB] [MEM] Free heap ...` lines, which are "other" `[MEM]` lines.
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet.md` (read-only) -- "Serial lines that carry each figure" L1, L2, L3.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/device_log.py` -- new. Docstring in the repo's style (what it does, exit codes, local run). `capture --port NAME=PATH ... --out DIR [--baud 115200] [--duration S]` (`--out` required, so no stray files in the repo; `--port` at least once, normally twice; name `[A-Za-z0-9_-]+`, unique; paths unique; baud must name a `termios.B<baud>` constant, else exit 2). One `select` loop; per device: open with `O_RDONLY|O_NOCTTY|O_NONBLOCK`, raw 8N1 mode set by hand (`tty.cfmakeraw` is 3.12+; the floor is 3.11) with `HUPCL` cleared and VMIN/VTIME 0, then clear DTR and RTS best effort with `TIOCMBIC` (ignore `OSError`: the packet says keep them off so connecting does not reset the reader); `termios.error` (not a tty) is a failed open. A read that raises `OSError` or returns `b''` is a loss: flush the partial line, close, retry the open every 0.5 s. Raw log `DIR/<NAME>.raw.log`: every chunk written as read, opened `ab` unbuffered, nothing added. Merged log `DIR/merged.log` (`ab`): per complete line `<YYYY-MM-DDTHH:MM:SS.mmmZ> <NAME> <line bytes>\n` (one trailing `\r` dropped; bytes kept, never decoded); the stamp is taken when the line completes and clamped to never go below the previous stamp, so file order is host-time order; a partial line over 64 KiB is flushed as a line. Event lines in the merged log only, `<stamp> <NAME> [device_log] connected <path>` and `... lost <path>: <reason>; waiting for it to come back`, also on stderr. SIGINT and SIGTERM (when run in the main thread) and `--duration` end it cleanly, flushing partial lines, exit 0. The capture function takes a `threading.Event` to stop and the clock as parameters so tests drive it. `summary [--mem-all] LOG...`: a line with the merged prefix belongs to its NAME, any other line to the file's stem (minus `.raw`); report per device in first-seen order: connected and lost counts (merged logs); each `VM stopped` as `<game>: arena peak N B, stack high-water N B free, least at a hook N B` with the game from the last lifecycle line before it (`(unknown)` when none) and its time (host stamp, else `[millis]`); per game the counts of both dropped-touch lines and of hand-off lines with the largest ms; `[MEM]` lines as counts (heap, PSRAM, other) plus first and last, smallest `Min Free` and smallest `MaxAlloc` (every line with `--mem-all`). Unreadable file or none given: exit 2.
- [x] `scripts/device_log_test.py` -- new, `unittest`, no serial device: `os.openpty()` pairs behind symlinks in a temp dir (the symlink is the port path; removing it and closing the master is the port vanishing; a new pair and a symlink swap by `os.replace` is it returning). Drive `capture` in a thread with a stop event; wait with a polling `wait_for(predicate, timeout)` on the merged log's content, never a fixed sleep, and write each next line only after the previous one is visible (and wait for the `connected` line before the first write). Cases: the matrix rows; raw logs byte-identical to the bytes sent (CRLF, `\xff`, NUL, a final line with no newline) including across a reconnect; the other device keeps flowing while one is away; a half line at a loss reaches the merged log; a port that appears only later; `/dev/null` as a port; arg errors exit 2 through `main`; one subprocess run of the CLI stopped by SIGINT exits 0 with both logs written; `summary` of a sample log (raw format, built from the firmware format strings above, with figures from packet rows U3 and U5 where it prints them) and of a two-device merged log, asserting every figure appears. Double (AGENTS.md rule): the sample log and the pty stand in for device output and the USB CDC port; a test pins that each parsed format string is still literally in its firmware source file (read from the working tree, as `platformio.ini` is read elsewhere); the pty is more permissive than the device, in that its loss is an immediate `EIO` and it has no sleep timing, so reconnect on a real X4 Pro is unmeasured.
- [x] `scripts/check_flash_budget.py` -- add `touch-line`. Constants `TOUCH_LINE_FORMAT = b'%s: touch %s'` and the source path above. `check_touch_line(metadata_dir, summary_path=None, project_dir=PROJECT_DIR)`: `load_build(.., 'on')` and `check_flag`; the ELF must exist and be non-empty (else `SetupError`); the source file must still hold the format as a quoted C string (else `SetupError` naming the constant, so a reworded log line cannot turn the gate into a pass that checks nothing); search the ELF with `mmap`; found: `FAIL`; else `PASS`. Report text under `## x4pro touch log line` for stdout and the job summary (found: the offset, `CROSSHATCH_TOUCH_LOG`, the page in `game-canvas.md`). `main` routes it with `heading = 'x4pro touch log line'`; update the module docstring (command list, exit 1 and 2 meanings, local run).
- [x] `scripts/check_flash_budget_test.py` -- `TouchLineTest`: absent passes, present fails (needle at the start, in the middle, at the end of the ELF), a near miss (`%s: touch %d`) passes, source without the format, missing or empty ELF, and off-state metadata are exit 2, CLI exit codes 0/1/2 with the summary file, the real `GameMatchActivity.cpp` holds the format inside a `LOG_DBG("GAME"` call, and the workflow's `flash-budget` job runs `touch-line` after `build on` with the `build-on` success condition.
- [x] `.github/workflows/crosshatch-ci.yml` -- in the `flash-budget` job only: a step after `Check game objects`, `Check the touch log line is compiled out`, same `if`, `run: python3 scripts/check_flash_budget.py touch-line`, with a short comment; and one sentence in the job's header comment bullet. Nothing else.
- [x] `docs/crosshatch/fork-scripts.md` -- a section "Capturing a device run" (after `## CI`, or before): the two-device command with `/dev/serial/by-id/...` paths (stable across the sleep renumbering), the output files, what a merged line looks like, `summary`, that it needs a computer with two USB ports, that DTR and RTS are cleared, Linux and macOS port names, and that the reconnect against a real X4 Pro's CDC port is unmeasured until a device run.

**Acceptance Criteria:**
- Given two pseudo-terminals feeding interleaved lines, when one vanishes and returns, then the merged log orders by host time, names each device, shows the loss and return, and each raw log equals the bytes its port sent.
- Given the sample log, when `summary` runs, then every `VM stopped` figure, dropped-touch and hand-off count, and `[MEM]` figure appears under its device.
- Given an ELF built with `-DCROSSHATCH_TOUCH_LOG=1`, when `touch-line` runs, then it exits 1; given the default build's ELF, it exits 0 (one x4pro build each, recorded).
- Given the flash-budget job, when its commands run from an archive tree of the committed work with every submodule, then `build on`, `build off`, `compare`, `objects`, and `touch-line` pass.

## Implementation Notes

Implemented by the step-3 subagent (full route). Files: the six in the Tasks, plus two Game-paths bullets in `docs/crosshatch/upstream-touches.md` (outside the entry's `touches`; required by `fork-scripts.md` and the brief, see Design Notes, Ledger). Additions beyond the plan: duplicate ports are detected by `os.path.realpath` (a `by-id` link and its `ttyACM` target are one port); the game attributed to a `VM stopped` line is cleared on a `connected` or `lost` event; `--duration` must be above 0; one workflow test that nothing in the workflow builds with `CROSSHATCH_TOUCH_LOG`. A pty loss reads as end-of-file here, not `EIO`; the code handles both. Host results at this step: `device_log_test.py` 28 tests, `check_flash_budget_test.py` 90 tests, every `scripts/*_test.py` clean, `check_upstream_touches.py` PASS, `./bin/clang-format-fix` twice with no change.

## Plan Change Log

## Review Triage Log

### 2026-10-12 — Review pass
- verdicts: 22 findings — high 0, medium 0, low 21, false 1, maybe-false 0 (plus the intent-alignment lens's descriptive report)
- findings:
  - `[low]` `patch` Blind Hunter 1: the touch-line comment says the define cannot reach "a shipped build", but the gate reads the x4pro games-on build only (LOG_LEVEL 2; release envs use 1 and compile LOG_DBG out) — workflow comment, docstring and report reworded to that scope.
  - `[low]` `patch` Blind Hunter 2: the gate passes vacuously if x4pro stops compiling LOG_DBG — a platformio.ini test pins ENABLE_SERIAL_LOG and LOG_LEVEL=2 for x4pro (a metadata branch in the script was rejected as more than the fix needs).
  - `[false]` `reject` Blind Hunter 3: the gate was never seen failing on a real ELF, so "one build each, recorded" is unmet — it is a listed post-review step (Verification), recorded below; the unit tests need no real build.
  - `[low]` `reject` Blind Hunter 4: plan checkboxes unticked, `lenses_ran` empty, "seven in the Tasks", a missing Auto Run Result, a truncated file name — a fix would edit this build's plan; the file name is the one `tickets.py find` gives; the Auto Run Result and the ticks are written at Finalize.
  - `[low]` `patch` Blind Hunter 5: `summary A.raw.log merged.log` merges one device name and double-counts — read_logs raises SetupError (exit 2) on a device name in two files; docs say one log per device.
  - `[low]` `patch` Blind Hunter 6: unguarded raw/merged writes (short write, ENOSPC, a removed directory) give a traceback and exit 1 — a write-all loop and an OSError-to-SetupError (exit 2) path, with tests.
  - `[low]` `defer` Blind Hunter 7: no exclusive open, so ModemManager or a second reader can take bytes — TIOCEXCL changes behaviour on a real CDC port nobody has measured; docs now name ModemManager and the dialout group; deferred above.
  - `[low]` `defer` Blind Hunter 8: DTR/RTS clearing and the termios flags are untested — the termios flags get a test (Verification Gap); the ioctl cannot be observed on a pty and a mock would verify nothing; deferred above.
  - `[low]` `patch` Blind Hunter 9: a second Ctrl-C does nothing — the first signal restores the previous handlers; test added.
  - `[low]` `reject` Blind Hunter 10: the game attributed to a `VM stopped` line can be stale after a reboot in a raw log, or when a lifecycle line does not match — a `VM stopped` always follows its match's `-> Leaving` line in the firmware, and a connect or loss clears it in merged logs; the cost of a rare wrong game name in a summary does not justify a millis-regression branch.
  - `[low]` `reject` Blind Hunter 11: brittle workflow slicing, a literal MEM source check that a reflow could break, and a test name that overclaims — the same style the file's existing workflow and platformio.ini tests use; a failure names the string.
  - `[low]` `reject` Blind Hunter 12 (Windows, lazy import): the docs name Linux and macOS and the ticket asks for a termios reader. The exit codes and the stamp hold after a clock step are documented (patch to the docs).
  - `[low]` `patch` Edge Case 1: the raw-log write ignores a short count or an OSError — same fix as Blind Hunter 6.
  - `[low]` `patch` Edge Case 2: `--duration nan` never ends — guard is `not duration > 0`; test added.
  - `[low]` `patch` Edge Case 3: a broken stderr pipe kills the capture — notes go through one OSError-suppressing helper; test added.
  - `[low]` `patch` Edge Case 5: a raw log and the merged log of one device merge into one — same fix as Blind Hunter 5.
  - `[low]` `reject` Edge Case 6: the same stale game attribution as Blind Hunter 10 — same reason.
  - `[low]` `reject` Edge Case 4: a host clock step back freezes the stamps at the latest value — the clamp is what keeps file order equal to time order; a monotonic-offset stamper would change the injected-clock design for a rare NTP step; documented instead.
  - `[low]` `reject` Edge Case 7: a library string containing `%s: touch %s` could fail the gate — plan-e8-touch-log measured 0 hits in the default x4pro ELF; the first CI run would show a hit by name.
  - `[low]` `patch` Edge Case 9: the claim that raw logs keep every byte rests on an unchecked write — same fix as Blind Hunter 6 (the write-all loop makes the claim true).
  - `[low]` `reject` Edge Case 8: SIGHUP does not flush a half line — a closed terminal loses at most one half line, already in the raw log.
  - `[low]` `patch` Verification Gap 1: no test reads the port's termios settings (speed, HUPCL, VMIN/VTIME) — a pty-slave tcgetattr test added; the DTR/RTS part is deferred above.
  - `[descriptive]` Intent Alignment: readings R-A (tool level, implemented) and R-B (real artifacts: the two real builds, the archive-tree run, a device-captured log, a real CDC port); S-1 "today" as what the firmware prints now (implemented) and S-2 dated today; V-1 game inferred from the lifecycle line (the firmware line has no game id). Divergences: the sample log is built from the firmware's format strings (pinned by a test), not captured from a device; the real-build and archive-tree runs are recorded below at Verification; `deferred-work.md` is the orchestrator's. No change.

## Design Notes

- **Settled by the sources.** Standard library only, a `termios` and `os` reader, `--port A=... --port B=...`, reconnect, one merged log with host stamp and device name, raw logs unchanged, a summary of the packet's figures (this entry; R13; Notes, inception Q5 Decision 2026-10-11). The assertion is an ELF-strings check in the flash-budget job, riding this entry (Notes, Decision "inception Q4", 2026-10-11; `deferred-work.md` `## e8-touch-log`). No CI build with the flag (same Decision). `LINK` / `PeerGone` / `ABORT` lines wait for entry 11 (R13). The packet wants DTR and RTS off (`device-run-packet.md`, Serial log).
- **Why a subcommand, not part of `objects`.** `objects` reads object files for AD-2's static rules; this reads the linked ELF's bytes. A separate command names its own failure in the job log and keeps `objects`'s exit meanings.
- **Needle.** The source pins the format (`"%s: touch %s"`), and the check refuses to run when the source changed, so a reworded line is exit 2, not a silent pass. The ELF search covers the whole file, debug sections included: `plan-e8-touch-log.md` measured 0 hits in the default x4pro ELF's strings and 1 with the define.
- **Stamp at completion.** A line's stamp is taken when its newline arrives, so file order and stamp order agree; the cost is that a slow line is stamped late by up to its transfer time.
- **Guards in the one function changed.** `check_flash_budget.main` gains a subparser and one `heading` branch; its guards (`limit_bytes` selection, the `objects` and `build` returns, the SetupError summary heading) keep their order and meaning.
- **Ledger.** `fork-scripts.md` and the brief require a new fork script and its test to be listed in `docs/crosshatch/upstream-touches.md` Game paths, a file this entry's `touches` omits. The two bullets are added, as the earlier script entries did, and reported under Auto Run Result so the orchestrator can revert them.

## Verification

**Commands (implementation, host only):**
- `python3 scripts/device_log_test.py -v` and `python3 scripts/check_flash_budget_test.py` -- expected: pass
- `for t in scripts/*_test.py; do ...` (fork-scripts.md's loop) -- expected: no `FAILED`, no `RAN NO TESTS`
- `python3 scripts/check_upstream_touches.py`; `./bin/clang-format-fix` twice, `git status` unchanged the second time -- expected: PASS, nothing new

**Commands (once, after the review, under `/tmp/crosshatch-build.lock`, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`):**
- `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 270 --ram-limit-bytes 1024`, `objects`, `touch-line` on the default build -- expected: all pass; flash +258,048 B and static RAM +784 B as at the epic base (no firmware source changes, so growth over the base is 0 B)
- `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_TOUCH_LOG=1 python3 scripts/check_flash_budget.py build on`, then `touch-line` -- expected: exit 1
- The job's commands once from an archive tree of the committed work (`git archive` plus every submodule, nested included) -- expected: all pass. No C++ file changes, so no `default` build, `pio check`, or `sim.sh` run.

**Results (2026-10-12; host results after the review patches; firmware results on the tree whose script, workflow and doc files are the committed ones):**
- `python3 scripts/device_log_test.py`: 38 tests, run 5 times in a row, all OK. `python3 scripts/check_flash_budget_test.py`: 91 tests OK. fork-scripts.md's loop over every `scripts/*_test.py`: no `FAILED`, no `RAN NO TESTS`. `python3 scripts/check_upstream_touches.py`: PASS. `./bin/clang-format-fix` twice: `git status` unchanged both times and no file changed outside this story's paths.
- No C++ file changed, so no host GoogleTest run, `default` build, `pio check`, or `sim.sh` run.
- Default x4pro build (`PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, under the build lock, this worktree, incremental): `build on`, `build off` exit 0; `compare --limit-kib 270 --ram-limit-bytes 1024` exit 0: `firmware.bin` 6,213,360 B on, 5,955,312 B off, +258,048 B flash (18,432 B under), static RAM 190,248 B on, 189,464 B off, +784 B (240 B under). These equal the epic base Measurement (2026-10-11, `7bfd8991`) to the byte, so this story's growth over the base is 0 B flash and 0 B static RAM (two measurements the same way; the first is the base Note's, the second this tree's). `objects` exit 0 (49 game objects, no problems). `touch-line` exit 0: the 94,163,296 B ELF holds no `%s: touch %s`.
- The assertion, red and green, one x4pro build each: `PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_TOUCH_LOG=1 python3 scripts/check_flash_budget.py build on` gave `firmware.bin` 6,214,176 B (+816 B over the default build); `touch-line` on it exited 1: the 94,167,160 B ELF holds `%s: touch %s` at offset 0x1bda9 (114,089). On the default build above it exited 0.
- Archive tree (the gate's own commands, a `git archive` of the staged tree `9ded67dda746c2f259b174bd3f7ebb3087907cb7` plus every submodule's archive, `freeink-sdk` and its nested `lucide` included, with `.pio` empty; the shared build cache supplied part of the objects; not a `git clone`): `pio pkg install -e x4pro`, `build on`, `build off`, `compare` (same +258,048 B and +784 B), `objects` (no problems), `touch-line` (no match in the 94,163,360 B ELF) all exit 0. The committed tree differs from that one only in this plan file (checked with `git diff --stat` against it).
- Unmeasured: reconnect and DTR/RTS against a real X4 Pro CDC port across a sleep (the pty loses its port at once and has no sleep timing; deferred above).

## Auto Run Result

**Summary.** `scripts/device_log.py` (standard library; `termios`, `select`) has `capture`, which reads two or more ports at once, reconnects by itself, keeps each device's raw log byte for byte, and writes one merged log of `<UTC ms stamp> <NAME> <line>` lines in host-time order, and `summary`, which lists each device's connects and losses, each `VM stopped` with its game, dropped touches and hand-off screens per game, and the `[MEM]` figures. `check_flash_budget.py touch-line` fails when the x4pro games-on ELF holds the per-tap touch line's format text; the flash-budget job runs it after `Check game objects`. `fork-scripts.md` documents a two-device run. This closes `## e8-touch-log`'s first item once the epic merges; the orchestrator updates that entry.

**Files.**
- `scripts/device_log.py` -- new: `capture` and `summary`.
- `scripts/device_log_test.py` -- new: 38 tests on pseudo-terminals and synthetic logs, no serial device.
- `scripts/check_flash_budget.py` -- `touch-line` command, docstring.
- `scripts/check_flash_budget_test.py` -- `TouchLineTest`, a workflow test, and an x4pro log-level pin.
- `.github/workflows/crosshatch-ci.yml` -- the `flash-budget` job: one step and one comment sentence.
- `docs/crosshatch/fork-scripts.md` -- "Capturing a device run".
- `docs/crosshatch/upstream-touches.md` -- two Game-paths bullets for the new script and its test. **Outside this entry's `touches`**: `fork-scripts.md` and the brief require a new fork script to be listed there; revert the two lines if the owner reads `touches` strictly.
- The plan.

**Review.** Four lenses as context-free subagents: 22 findings (low 21, false 1), plus the intent-alignment report. Patched (9 entries, all low): the touch-line scope wording, an x4pro log-level pin test, a device name in two logs, unchecked log writes, `--duration nan`, a broken stderr pipe, a second Ctrl-C, a port-settings test, and doc additions. Deferred (2, low): no exclusive port open, and DTR/RTS clearing and real-port reconnect unobserved. Rejected with reasons: the plan-file edits, a stale game after a reboot, brittle workflow-test slicing, Windows, a clock step back (documented), a library string holding the needle, SIGHUP. Follow-up review recommended: false (no high, fewer than two medium patched).

**Verification.** See Verification, Results.

**Formatting.** `./bin/clang-format-fix` changed nothing outside this story's paths.

**Residual risks.** Reconnect, the DTR/RTS clear, and exclusive access are untried on a real X4 Pro port. The sample log is built from the firmware's format strings (pinned by a test), not captured from a device. The touch-line gate covers the x4pro games-on build only (the release envs log at LOG_LEVEL 1). `ttyACM` renumbering is handled by reconnecting to a path, so `/dev/serial/by-id` paths are the way to name devices.
