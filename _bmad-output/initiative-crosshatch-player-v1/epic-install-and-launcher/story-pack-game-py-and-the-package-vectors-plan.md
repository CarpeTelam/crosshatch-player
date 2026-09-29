---
title: 'pack_game.py and the package vectors'
type: 'feature'
ticket: '2'
created: '2026-09-28'
status: done
baseline_revision: '8f389a52e74a00fb26591e4c94067f6a7c2d6b47'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - 'AGENTS.md'
  - 'docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The release workflow's `pack-games` step calls `scripts/pack_game.py`, which does not exist, and the installer (entry 3) has no golden package or limit vectors to test against.

**Approach:** Add the packer as the only Python reader of `manifest.json`, with a sidecar test, and commit `test/game_core/package_vectors.json` plus one packed vector package; list the sidecar in the ledger's Game paths.

## Boundaries & Constraints

**Always:** Standard library only, Python 3.11+, single quotes; `fork_common`'s exit contract (0 packed, 1 invalid package, 2 could not run) and `parse_api_level`; the last stdout line is the 16-hex package hash; `<dir>`'s name equals `manifest.id`; package 262,144 B, member 131,072 B, 32 members, images 62 + ceil(w/32)*4*h each (`icon.png` excluded, at most 32, total at most 131,072 B), members sorted by name, fixed timestamps; hash = first 8 bytes of SHA-256 over members sorted by name, each `name \0 u32le(len) bytes`.

**Never:** Touch `scripts/fork_release.py`, `src/**`, `lib/**`, or import `game_codec.py`; write a `.bmp` or any non-whitelisted file into a package; edit files outside `touches`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pack | valid `<dir>` | `<out>/<id>.cpgame`, hash last line, exit 0 | none |
| Same twice | same folder, any mtime | byte-identical packages | none |
| Bad package | `.bmp`, other file, subfolder, bad name, missing `main.lua`/`manifest.json`, binary chunk, interlaced or malformed PNG, over any limit | nothing written, each problem on stderr | exit 1 |
| Bad manifest | a `Manifest::parse` rule broken, `solo` with `seats.min` != 1, `nearby` with `seats.max` < 2, `id` != dir name, unknown `icon`, bad `icon_weight` | as above | exit 1 |
| API range | `api` outside `API_MIN_LEVEL..API_LEVEL` | refused | exit 1 |
| Cannot run | missing `<dir>`, unreadable `ApiLevel.h`, unreadable `names.txt` when `icon` is set, `<out_dir>` not creatable | nothing written | exit 2 |

</frozen-after-approval>

## Code Map

- `scripts/fork_common.py` -- `SetupError`, `exit_code`, `parse_api_level`, `API_LEVEL_HEADER`; reuse, do not change.
- `scripts/fork_release.py` `pack_one` (line 609) -- calls `python packer games/<id> <out>` with cwd = project dir, needs exit 0, a 16-hex last line, `<out>/<id>.cpgame`. Read only.
- `lib/GameCore/Manifest.cpp` -- the parse rules to mirror in Python: duplicate known keys (top level, `seats.min/max`) fail; `api` a plain integer 1..999,999,999; `name` 1..64 UTF-8 bytes; `version` at most 32 bytes; `seats` both keys, `min` >= 1, `max` >= `min`; `modes` a non-empty array of `solo|pass|nearby`; `hidden` bool; unknown keys ignored. Its `icon` grammar is the old one; R9's replaces it here.
- `lib/GameCore/GameImages.h` -- `IMAGES_BYTES` 128 KiB, `MAX_IMAGES` 32, header 62, converter layout; `lib/PngToBmpConverter/PngToBmpConverter.cpp` line 446-455 -- refuses interlaced, width > 2048, height > 3072, zero size.
- `assets/game-icons/names.txt` -- library names (first column of non-`#` lines); manifest `icon` must be one, and match `[a-z][a-z0-9]*(-[a-z0-9]+)*`, at most 32 bytes.
- `docs/crosshatch/upstream-touches.md` -- Game paths already lists `scripts/pack_game.py`; add `scripts/pack_game_test.py`.
- `scripts/fork_release_test.py` -- style reference; imports by putting `scripts/` on `sys.path`.

## Tasks & Acceptance

**Execution:**
- [ ] `scripts/pack_game.py` -- new. Module docstring with usage and exit codes. `build_package(members)` returns zip bytes (zipfile, `ZipInfo` date 1980-01-01, `create_system` 3, `external_attr` 0o100644 << 16, deflate level 9, stored when deflate does not shrink); `package_hash(members)`; `read_manifest`; `png_size(data)` from IHDR (signature, length 13, CRC, interlace 0, size limits); `check_images(sizes)`; `pack(dir, out_dir)`. The repo root is the script's `parent.parent` (so `ApiLevel.h`, `names.txt` resolve in a temp project). Collect every problem, print each as `error: ...` on stderr, return FAIL; success prints `packed <path> (<n> bytes)` then the hash. Refuse a member starting `\x1bLua`. Write via temp file and `os.replace`; write nothing on failure. -- the packer
- [ ] `scripts/pack_game_test.py` -- new `unittest` sidecar: a temp project (packer, `fork_common.py`, `ApiLevel.h`, `names.txt` copies); every exit code; each limit at the limit and one over (package via incompressible `.lua` sized from the measured overhead, member, members 32/33, images total 131,072/131,074 from PNG dimensions, images count 32/33 at function level because the member cap binds first); `.bmp` refused; two runs byte-identical after `os.utime` and a different write order; `parse` cases for every I/O row; `fork_release.pack_one` against the real packer; the committed hash vector and package; `package_vectors.json`'s limits equal the packer's constants; the real fixtures `counter` packs and `images` (has `.bmp`) is refused. -- verify
- [ ] `test/game_core/package_vectors.json` and `test/game_core/package_vector.cpgame` -- new. JSON: `limits` (each with `limit`, `at`, `over`; images cases as `[w, h]` lists with their `bytes`), the image formula constants, and `hash_vector` (`id` `package-vector`, `members` as text: `manifest.json`, `main.lua`, `util.lua`, `sha256` of the preimage, `package_hash`, `package` file name and its byte size). `util.lua` is short enough to be stored, `main.lua` long enough to be deflated. The `.cpgame` is the packer's output for those members. -- entry 3 and 6 read them
- [ ] `docs/crosshatch/upstream-touches.md` -- add `scripts/pack_game_test.py` after `scripts/pack_game.py` in Game paths -- ledger

**Acceptance Criteria:**
- Given a valid folder, when `pack_game.py` runs from the repo root, then the package holds exactly the sorted whitelisted members and the last stdout line matches `[0-9a-f]{16}`.
- Given the committed vector package, when its members are hashed the R4 way, then the result equals `package_hash`.
- Given any folder in the I/O matrix's error rows, when packed, then the exit code is as listed and no `.cpgame` exists.

## Implementation Notes

Implemented by a subagent from this plan. Beyond the plan: `png_size` also refuses a non-zero PNG compression or filter method (the converter does); subfolders and symlinks are refused as non-regular files; `.bmp` is refused in any letter case with its reason; `icon_weight` other than `regular` or `fill` is invalid (AD-15's amendment, R9); `pass` is a valid mode; the vectors add `image_width` and `image_height` limits (2048, 3072). The images-total vectors are `[[2048,511],[32,33]]` = 131,072 B and `[[2048,511],[32,9],[32,9]]` = 131,074 B. The package's file name and the id check use `directory.resolve().name`. The vector package was 589 B, hash `1ac5ece47021dedf`, until the follow-up (575 B, `0530a15766e91bf1`); `main.lua` is deflated and `util.lua` stored. Tests found and fixed one bug (an uncreatable `<out_dir>` crashed the cleanup instead of exiting 2).

## Plan Change Log

## Review Triage Log

The four lenses (blind hunter, edge-case hunter, verification gap, intent alignment) ran as context-free subagents over the staged diff and all returned. Counts: 1 medium, 12 low, 5 false or out of scope, 0 high.

| Finding (lens) | Verdict | Route | Evidence / action |
|---|---|---|---|
| Golden-package test pins deflated bytes and length, zlib-dependent (blind, edge, gap) | medium | patch | `test_the_packer_packs_the_vector_members` now compares content, methods, and hash; byte identity stays in the two-run test. The plan's Design Notes said content and hash only. |
| `icon_weight` and the icon grammar differ from `Manifest.cpp` (blind, edge, gap) | false | none | R9 and AD-15's amendment require both; entry 6 aligns `Manifest.cpp`. The docstring claim was loose: patched to say so. |
| Vectors have no C++ consumer; constants not tied to `GameCore` (blind, gap, intent) | false | none | The ticket says "entry 6 checks the C++ constants against it" and `stays_out` lists `lib/**`, `src/**`; the packer's constants are tied to the JSON by `test_vector_limits_equal_the_packer_constants`. |
| Docstring "a game that packs installs" overstated: IHDR-only PNG, no bit depth or color type, IDAT, IEND, depth-32 manifest (blind, edge) | low | patch (docstring) | Real, but a corrupt image or 32-deep manifest in a game folder is unlikely and the fix adds guards; docstring narrowed to say what is checked. Deferred entry records the rest. |
| Lua binary check bypassed by a leading `#` line or BOM (blind) | low | patch (docstring) | The installer enforces it; the packer check is best effort, now stated. |
| `test_member_bytes` asserts a loose substring (blind) | low | patch | Now asserts `bytes; at most 131,072 per member`. |
| Read whole file before size check; `is_file` OSError; symlinked `<dir>` name; empty `<out_dir>` left after a failed write; double compress; junk files fail the pack; folder name vs `GAME_ID`; nesting-depth and non-UTF-8 filename tests; Python version and `os.symlink` on Windows (blind, edge) | low | rejected | Each needs a guard or branch for a case a release folder does not reach (`games/<id>` are checked-in folders); the folder-name case is already reported through the `id` message. |
| Tests depend on the live `counter` and `images` fixtures and `ApiLevel.h` (edge) | low | rejected | The plan's task asks for the real fixtures; both stay valid across an API bump. |
| No `docs/file-formats.md` or `formats.md` entry for the package hash (blind) | false | none | Entry 3 writes the package format into `docs/crosshatch/formats.md` (epic Notes); not in `touches`. |
| Intent alignment: readings A to D; the diff is reading D with the packer-side equality check | none | none | Descriptive; the installer-side consumer is entry 6, as the ticket says. |

### Orchestrator's independent review (adversarial, edge-case, verification-gap lenses)

Follow-up commit fixes. One row per distinct finding; findings two lenses shared are triaged once.

| Finding (lens) | Verdict | Route | Evidence / action |
|---|---|---|---|
| `json.loads` decodes `\uXXXX`, the device's `StreamingJsonParser` keeps it literal (lines 149-154), so escaped keys, values, and names counted in characters differ (adversarial 1, edge-case 1) | high | patch | Confirmed in `StreamingJsonParser.cpp`; a manifest written with `ensure_ascii=True` would pack and then fail `Manifest::parse`. `read_manifest` now refuses any `\u` escape (a regex that ignores an escaped backslash before `u`); tests cover an escaped key, id, mode, name, icon, extra key, and the still-valid escaped backslash and `\n`. `manifest_text` in the tests writes raw UTF-8. |
| Vector `main.lua` was 947 B, over the parser's 511 B token limit, so a C++ test reading the JSON would lose it (adversarial 2, edge-case 2) | medium | patch | Shortened to 403 B (still deflated); vector package regenerated (575 B, hash `0530a15766e91bf1`); a test asserts every member string is at most 511 B escaped and holds no `\u`. |
| 32-character member-name limit had no vector and no at-limit test (verification-gap 1) | medium | patch | `MAX_MEMBER_NAME_CHARS` builds both regexes; `member_name_chars` vector (32, 33); tests pack 32-character `.lua` and `.png` and refuse 33; a mutation to 31 now fails 3 tests. |
| Stale `<out_dir>/<id>.cpgame` stays after a failed pack (adversarial 3) | low | patch | Deleted on exit 1 (only that file, only a regular file); tested, other packages untouched. Docstring says so. |
| `png_size` ignored colour type and bit depth (adversarial 4; my own earlier low, rejected) | low | patch | One table check, `PNG_DEPTHS`; tests for types 1, 5, 7, depth 3, palette at 16 bits, and the legal pairs. Chunk walk stays deferred. |
| BOM refused though the device skips it (edge-case 3) | low | patch | Checked `handleScanning`: an unknown byte (0xEF...) outside a string falls into the ignored default, so the device accepts a BOM. Decoded with `utf-8-sig`; test now expects acceptance. |
| `out_dir` resolving to the game folder writes the package into it (edge-case 4) | low | patch | Exit 2, "is the game folder"; test for three spellings, folder unchanged. |
| Vector fields `row_pixels_per_word` and `word_bytes` never checked (verification-gap 2) | low | patch | Test recomputes `image_bytes` from the JSON's own fields over widths 1, 31, 32, 33, 64, 65, 2048. |
| Duplicate-key test covered 5 of 9 known keys (verification-gap 3) | low | patch | Loops over `KNOWN_KEYS` (and asserts the loop table equals it). |
| `deferred-work.md` entry appended under `## 4.2` (verification-gap 4) | false | none | The orchestrator rejected it: `tickets.toml`'s header allows every entry to append under `## <ref>`. |

## Design Notes

- No existing function is moved or rewritten: `pack_game.py` is new and `pack_one`'s contract is only read (git log -L is not applicable).
- The images-count cap (32) cannot be reached by a valid package: manifest and `main.lua` take two of the 32 members, so 30 images is the most; the count check stays (the installer's `MAX_IMAGES`) and is tested on `check_images`.
- The converted-image total moves in steps of 4 (62 mod 4 is 2): 131,073 is unreachable, so the over case is the smallest reachable total above the limit, 131,074.
- The committed `.cpgame` is checked by content and hash, not bytes, because deflate output can differ between zlib builds; byte identity is tested between two runs on one toolchain.
- The packer also refuses what the converter or loader would fail on later (interlaced PNG, over 2048 x 3072, `\x1bLua`), so a game that packs installs.

## Verification

**Commands** (run in this worktree after the follow-up fixes):
- `python3 scripts/pack_game_test.py` -- Ran 70 tests, OK.
- `for t in scripts/*_test.py; ...` (the loop from `docs/crosshatch/fork-scripts.md`) -- no FAILED and no RAN NO TESTS line.
- `python3 scripts/check_upstream_touches.py` -- Result: PASS.
- `./bin/clang-format-fix` twice -- nothing new in `git status`.
- Mutations (each failed the suite, then reverted): `MAX_MEMBER_NAME_CHARS` 32 to 31 (3 failures), the `\u` check disabled (1), the colour-type check disabled (13).

First commit (289a6abe): 65 tests OK; `pack_game.py` on the `counter` fixture printed `e8c3ac8dfb646d3b`.

No CI gate or workflow changed (`crosshatch-ci.yml` already runs every `scripts/*_test.py`), so no fresh-tree run applies. No memory, flash, or timing figure was taken; no C++ changed.
