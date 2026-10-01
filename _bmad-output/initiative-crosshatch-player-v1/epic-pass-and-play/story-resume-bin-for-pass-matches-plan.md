---
title: 'resume.bin for pass matches'
type: 'feature'
ticket: '5'
created: '2026-10-01'
status: 'built'
route: 'full'
route_source: 'auto'
baseline_revision: 'dbc964f7f5283988f64947afb25851c95d137645'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/heap-discipline/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `resume.bin` already carries `mode` and `n`, but `GameSaveStore` writes only solo (0, 1) and refuses anything else, so a pass match (R8, AD-17) cannot save or be offered; and `GameSaveStoreTest` runs on its own second fake SD card (`save_store_stubs/`), the drift retrospective AI-4 / agg-3 asks to fold away.

**Approach:** add overloads beside today's signatures (which stay solo-only, so `GamesLauncherActivity` and `GameMatchActivity` are unchanged until entries 7 and 9): a roster setter for writing, and `peek` / `loadResume` forms that take the game's manifest and the host's caps, accept a solo or pass save whose mode and `n` that game can start on that host, and give the saved roster. Move the test onto `harness/stubs/`, adding to that fake only the partial write the old fake had, pin the fake's rename and partial write against SdFat, and document the format in `formats.md` with its AI-10 item 7 fixes.

## Boundaries & Constraints

**Always:** file version stays 1 (a pre-epic solo save is the same bytes); mode byte 0 = solo, 1 = pass, set by explicit constants, never the enum's value; a refused save is kept (never deleted or rewritten by a read); `Unreadable` stays what it is today, whatever the mode; `peek` == `loadResume` acceptance for the same startable set; the harness fake only gains members (existing members and their behaviour unchanged); allocation rules as today (one nothrow buffer in `peek`, none added); every new double behaviour names the device code it stands in for.

**Never:** touch `src/activities/games/**`, `src/games/GameVM.*`, `lib/GameCore/**`, the harness match/resume suites, or `MatchStore.*` (not needed: the setter is reached through `MatchStore::saves()`); add an i18n string; change what the two-argument `peek` or `loadResume` accepts.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Solo, default | no `setRoster`; `saveResume` | bytes as today (mode 0, n 1); both `peek`s Valid; new `loadResume` gives `Roster::solo()` | — |
| Pass write | `setRoster(Roster::pass(2))` | mode 1, n 2; new `peek` Valid; new `loadResume` gives pass, 2, the ver | — |
| Pass, old API | pass save; 2-arg `peek` / `loadResume` | None / empty, `unreadable` false; file kept | `INF` line in `peek` ("cannot start"), `ERR` "discarded" in `loadResume` |
| Not startable | pass save with n outside `max(2, seats.min)`..`min(seats.max, host maxSeats, 16)`, or `host.pass` false, or the manifest lacks the mode; solo save of a game without solo, or n ≠ 1 | None / empty, kept | as above, reason names mode or seats |
| Unknown mode | mode byte 2..255 | None, kept | `ERR` "discarded …: unknown mode" |
| Pre-epic solo save | `formats.md`'s example bytes | Valid in both forms; solo roster, ver 7 | — |
| Fault | pass save, open or read fails / OOM | Unreadable, kept | as today |
| Nearby roster | `setRoster` with mode nearby | nothing read, written, or deleted (AD-17), as without the hash | — |

</frozen-after-approval>

## Code Map

- `src/games/GameSaveStore.h` -- add `#include <Roster.h>` (GameCore; layer-allowed edge, src/games → lib/GameCore). New public: `static SaveState peek(const GameCore::Manifest& game, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES], const GameCore::HostCaps& host)` (uses `game.id`); `void setRoster(const GameCore::Roster& roster)` (before the first save/flush; solo until called; nearby turns resume.bin off); `std::span<const uint8_t> loadResume(uint16_t& ver, bool& unreadable, const GameCore::Manifest& game, const GameCore::HostCaps& host, GameCore::Roster& saved)` (`saved` = `Roster::solo()` or `Roster::pass(n)`, solo on failure). Private: forward-declared `struct Startable;` (defined in the .cpp), `GameCore::Roster roster`, `bool resumeOn() const` (hash set and roster not nearby). Update the class comment (pass saves) and both old signatures' comments (solo-only).
- `src/games/GameSaveStore.cpp` -- `struct GameSaveStore::Startable { bool solo; bool pass; uint8_t passMin; uint8_t passMax; }`, a solo-only constant and `startableFor(manifest, host)` (from `manifest.check(host)`: not ok → nothing; solo if `modes & MODE_SOLO`; pass if `modes & MODE_PASS`, range `max(2, seatsMin)`..`min(seatsMax, host.maxSeats, Roster::MAX_SEATS)`). `readResume` gains `const Startable&` and the saved mode/n out: replace the `not a solo save` line (same place: after the package-hash check, before `ver`) with byte → `GameCore::Mode` (`RESUME_MODE_SOLO` 0, new `RESUME_MODE_PASS` 1, else `"unknown mode"`), then an exhaustive `switch` on the mode: not startable → `"mode not startable"`, n outside → `"seats not startable"`. These two join `OTHER_PACKAGE` as kept-quietly reasons in `peek` (`LOG_INF "%s: %s is a save this game or host cannot start: %s; the file is kept"`); the other-package line stays as is. Old `peek(id, hash)` and `loadResume(ver, unreadable)` call shared code with the solo-only set. `saveResume` writes the mode byte and `roster.seats` through a switch on `roster.mode`. Replace `hasPackageHash` gates in `saveResume`, `loadResume`, `deleteResume`, `flushResume` with `resumeOn()`.
- `src/games/MatchStore.*` -- unchanged (`saves()` already exposes the setter; the unknown is settled: construction needs no roster).
- `test/game_script/harness/stubs/HalStorage.h` -- add `std::map<std::string, size_t> failWriteAt` to `Card`: a write that would put a byte at offset n or later stores nothing and returns 0; earlier bytes stay. Comment: FAT32 `FatFile::write` returns 0 and keeps `m_fileSize` from the last whole write (SdFat 2.3.1 FatFile.cpp ~1500); exFAT can keep the failing call's whole sectors (`ExFatFile::write` raises `m_validLength` per sector), which the fake does not model; small writes may only fail at `close()` on the device (sector cache), modelled separately by `failClose`. Rewrite the header's first lines: this is the one fake card (the save store's fake folded in, AI-4).
- `test/game_script/harness/stubs/Logging.h` -- hold `fakelog` itself (the content of `save_store_stubs/Logging.h`), no include of the removed folder.
- `test/game_script/save_store_stubs/` -- delete (`git rm`).
- `test/game_script/CMakeLists.txt` -- `GameSaveStoreTest`: include dir `harness/stubs`; sources add `lib/GameCore/Manifest.cpp`, `lib/GameCore/Roster.cpp`, `lib/JsonParser/StreamingJsonParser.cpp` (and `lib/Memory` include if needed); comment names the harness fake.
- `test/game_script/GameSaveStoreTest.cpp` -- port to the harness API: `fakesd::addFile`, `bytesOf`, `has`, `removeEntry`, `sim().ops` (a helper that drops `exists ` and `write ` ops, so the old exact sequences still read `open TMP`, `close`, `remove`, `rename`), per-path failure sets (helpers to set and clear them; the old flags were one-shot), `failReadAt[path] = 0` for the read fault, `failOpen` for the open fault, `failWriteAt[tmp] = header bytes` for the old second-write failure; `isDir` for the folder check.

## Tasks & Acceptance

**Execution:**
- [x] `test/game_script/harness/stubs/HalStorage.h`, `Logging.h` -- the two additions above -- one fake card and one log capture
- [x] `test/game_script/CMakeLists.txt`, `GameSaveStoreTest.cpp`; `git rm -r test/game_script/save_store_stubs` -- port every existing test unchanged in meaning; run the suite green before the store changes
- [x] `src/games/GameSaveStore.{h,cpp}` -- the overloads, setter, and checks above
- [x] `test/game_script/GameSaveStoreTest.cpp` -- new tests for each I/O matrix row (a test `Manifest` with seats 1..3, solo and pass, and hosts with `maxSeats` 2 and 4, `pass` on and off) and `TheFakeCardRenamesAndTearsWritesAsSdFatDoes` (rename refuses an existing target and leaves both files; a write past `failWriteAt` returns 0 and adds nothing while earlier bytes stay; close still succeeds, as FatFile::sync does after a write error), each with its SdFat citation
- [x] `docs/crosshatch/formats.md` -- resume.bin: mode 0 solo / 1 pass, n (1, or the pass range), the overloads and which callers use which until entries 7 and 9, the new reasons and their log levels, file version 1 kept (a pre-epic firmware refuses a pass save and keeps it); limits table rows for `.png` size (`IMAGE_MAX_WIDTH` / `IMAGE_MAX_HEIGHT`, 2,048 x 3,072) and manifest nesting (`StreamingJsonParser::MAX_NESTING`, 32); ~:250 "(the `.lua` total is added there in a later entry)" → "including the `.lua` total"

**Acceptance Criteria:**
- Given the fold, when every host suite runs, then all pass and nothing under `test/` names `save_store_stubs`.
- Given `GamesLauncherActivity` and `GameMatchActivity` unchanged, when their suites run, then a solo save behaves as before.

## Implementation Notes

- `readResume` became a private static member (`GameSaveStore::readResume`, same body and check order) instead of staying in the anonymous namespace: a namespace-scope function cannot name the private `GameSaveStore::Startable`. The shared code is two more private members, `peekStartable` and `loadStartable`, plus `startableFor` and a `static const Startable SOLO_ONLY` (declared with the incomplete type, defined in the .cpp). `modeOfByte` (byte → `GameCore::Mode`, `default` for the open 2..255 set) and `keptQuietly` stay file-local.
- A pass match no seats fit on the host (`passSeats` 0) counts as pass not startable (`mode not startable`), so an empty range never accepts n 0.
- A solo roster writes n as `RESUME_SEATS_SOLO` (1), not `roster.seats`; the bytes are the same for `Roster::solo()`.
- The existing table test's three `not a solo save` cases moved: mode 1 / n 1, and solo n 2 / n 0, are now `mode not startable` / `seats not startable` (INF in peek), covered by the new matrix tests; the table gained `unknown mode` cases (mode 2, 255).
- After review pass 1 (supersedes the line above for malformed seats): a seat byte no mode can have (solo n ≠ 1, pass n < 2 or > 16) is `bad seat count`, logged at `ERR` by both forms (as `not a solo save` was), checked before the startable checks; `seats not startable` is only a well-formed pass n outside the game/host range. A successful load through the new `loadResume` adopts the saved roster. 58 `GameSaveStoreTest` cases.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message (each told the worktree is read-only), and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA). Verdicts: medium 1, low 10, false 4, maybe-false 0, plus IA's descriptive report. No intent_gap or bad_plan; the patches went back to the step-03 implementer (the same agent, re-engaged), and verification re-ran on the patched tree.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | EC1, BH1 | A pass save resumed through the new `loadResume` left the store's roster solo; a caller that skips `setRoster` writes mode 0, n 1, and the next Continue resumes the pass game as solo | medium | patch | Real: `loadStartable` never touched `roster`, and entry 9 removes `GameMatchActivity`'s pass skip. Fixed: a successful load through the new form adopts `saved` (a later `setRoster` wins); `AResumedPassSaveIsWrittenBackAsAPassSave`. BH1's other half (a new pass match that never calls `setRoster` writes solo) cannot be closed without changing the unchanged solo callers; deferred to entry 9 under `## 5.5` |
| 2 | EC4, BH3, VG-other, EC5, IA §3.2 | A seat byte no mode can have (solo n ≠ 1, pass n 0, 1, > 16) is malformed but was logged quietly at `INF`; before, the two-argument forms logged solo n ≠ 1 at `ERR`, and those cases lost their old-form tests | low | patch | New reason `bad seat count` (ERR, not kept quietly) before the startable checks; old-form solo n 0 / 2 cases restored |
| 3 | BH11 | The solo-only `peek` refusing a pass save logged "a save this game or host cannot start", untrue for that form | low | patch | Now "is a save that cannot be resumed here" |
| 4 | VG1, BH6 | No test reached the `max(2, seats.min)` lower bound or the empty range (`passSeats` 0) | low | patch | Rows added: a seats 3..4 game refuses n 2 and takes n 3; the empty range on `hostOf(1, true)` is `mode not startable` |
| 5 | BH5 | Nothing pinned the package-hash check before the mode check | low | patch | `AnotherPackagesSaveIsThatWhateverItsMode` (mode 1 and mode 7) |
| 6 | BH7 | The pass write was tested only through `saveResume` | low | patch | `FlushResumeWritesAPassRostersSnapshotAndDeletesItWhenOver` |
| 7 | BH9 | `clearFailures()` missed some `Card` failure members | low | patch | Clears every per-path failure member |
| 8 | BH10 | The ported exact op sequences dropped the `write` lines | low | patch | Sequences keep `write <tmp>`; "never written" counts `write ` ops |
| 9 | BH8, BH12 | `formats.md`: lines past the 120-column wrap; the empty-range rule unstated; a redundant `HostCaps::pass` aside; planning entry numbers that go stale | low | patch | Rewrapped (table rows cannot wrap) and reworded; callers named instead of entry numbers |
| 10 | EC2, BH2 | `saveResume` writes an out-of-range pass `roster.seats` (0, 1, 17+) unchecked; a mode outside the enum would write garbage | low | reject | Only a caller bug reaches it (`Roster::pass` gets its n from `passSeats`, 2..16), and the fix is a new guard. The garbage half is false: the switch covers every `Mode` enumerator, and `-Wswitch` flags a new one |
| 11 | EC3 | `setRoster` to nearby after a save was written turns `deleteResume` off, leaving a stale save | false | reject | AD-11: the roster is fixed for the match, so no match changes mode after it starts; a nearby match never wrote a save |
| 12 | BH4 | The pass upper bound is computed beside `passSeats` rather than taken from GameCore | low | reject | `lib/GameCore` is `stays_out`; row 4's tests pin the lower bound through `passSeats`, and the upper bound is the same three-way minimum `passSeats` documents |
| 13 | IA §3.3 | The new forms accept any n in the pass range, while today's start paths start only `passSeats()`'s fewest | false | reject | AD-11 and R8 refuse only an n "the game or host cannot start"; the range is what they can start, and the seat-choice rows are deferred, not forbidden |
| 14 | IA §3.4 | `setRoster` also gates reads and deletes; peek ⇔ loadResume holds only for a store with the hash and a non-nearby roster | false | reject | The frozen I/O matrix's nearby row asks for exactly this; `peek` is static, and the equivalence is stated for a store that reads at all |
| 15 | IA §3.1, §3.6 | No product caller uses the new forms yet; the SdFat pin is a fake-side test citing the source | false | reject | The Approach keeps callers unchanged until entries 7 and 9; a host test cannot run SdFat, so R13's pin asserts the fake's behaviour against cited device source (SdFat 2.3.1 FatFile.cpp :128, ~:974, ~:1503) |

## Design Notes

**Why manifest + host, not a mode mask:** "a save the game or host cannot start" is `Manifest::check(host)` plus the pass seat range `passSeats` uses; taking both keeps one rule for the title screen (entry 7) and the match (entry 9), and `peek` stays exactly what `loadResume` accepts. `lib/GameCore` is `stays_out`, so the range is computed in the .cpp from the same three bounds.

**Guards kept in `readResume`** (`git log -L`: 68ec417b, b964078a): open failure → unreadable; size checked before any read (bounds the stack prefix and the buffer); read failure → unreadable; blob header; whole prefix; package hash before mode (another package's save stays an `INF` line whatever its mode); `ver` set only once the save is accepted that far; empty snapshot; canonical codec. `deleteResume`'s no-hash early return keeps its reason (a match that cannot tell whose save it is never deletes); the nearby gate joins it for AD-17's reason.

**Fake fidelity (R13):** the fold adds only `failWriteAt`; rename and the no-partial-call write are pinned by one test citing SdFat; the exFAT difference and the cache-at-close case are written in the fake's comment as places it is more permissive.

## Verification

**Commands:**
- Host suites under `/tmp/crosshatch-hosttest.lock` (AGENTS.md's CMake/Ninja/ctest) -- expected: all pass; `GameSaveStoreTest` and every harness suite included.
- `grep -rn save_store_stubs test/` -- expected: nothing.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new in `git status`.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`, and the same `pio check` for `default`, then `sim.sh build x4pro` (src/games changed; no screenshot named) -- expected: success, no defects.
- No flash measurement (orchestrator: entry 5 records none). No CI gate changes, so no fresh-tree run.

**Results (2026-10-01, after the review patches, on the tree this entry commits):**
- Host suites: CMake/Ninja configure, build, and `ctest -j4` under `/tmp/crosshatch-hosttest.lock`: 1,467 of 1,467 pass (`GameSaveStoreTest` 58, on `harness/stubs/`; every harness suite included). No compiler warning from the changed files.
- `grep -rn save_store_stubs test/`: nothing. `check_layers.py`: 484 include edges pass. `check_upstream_touches.py`: PASS (no upstream file touched). Every `scripts/*_test.py` passes. `./bin/clang-format-fix` twice: nothing new, nothing outside these paths.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` SUCCESS, `pio run -e default` SUCCESS, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` PASSED, the same for `default` PASSED, `sim.sh build x4pro` SUCCESS.
- Flash and static RAM: unmeasured (the orchestrator records no measurement for entry 5).
- No CI gate or workflow changed, so no fresh-tree run. No screenshots (the verify names none).
