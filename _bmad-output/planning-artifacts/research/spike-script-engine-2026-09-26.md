---
title: 'Spike: Lua 5.4 vs Berry as the crosshatch-player script engine (X4 Pro)'
type: 'technical'
decision: 'Which engine runs sandboxed v1 games: PUC Lua 5.4 compiled as C, or Berry'
status: 'complete: recommend Lua 5.4'
branch: 'spike/script-engine (throwaway, never merged)'
firmware_commit: '1175389644c8'
created: '2026-09-26'
updated: '2026-09-26'
---

# Spike: Lua 5.4 vs Berry on the X4 Pro

**Decision this spike serves:** which engine runs sandboxed v1 games: PUC Lua 5.4 compiled as C, or Berry. The brief addendum's "Script runtime starting points" section asks for a footprint spike on the S3 to settle it.

**Status:** complete. The build-side numbers come from the build session; the device numbers come from one full run of the `x4pro-spike` firmware (`1175389`) on a real X4 Pro, via `/spike-results.txt`.

**Recommendation: PUC Lua 5.4, compiled as C, with the VM heap in PSRAM.** Reasons and caveats are in [Recommendation](#recommendation).

## What was built

The code described here is not on this branch. It lives on the throwaway branch `spike/script-engine`: firmware as run at `1175389`, with the vendored Berry sources last present at `22b60fb` (`lib/berry` was removed in `94cbba8`).

| Path | What it is |
|---|---|
| `lib/lua54/` | PUC Lua 5.4.7 from the lua.org tarball (SHA-256 `9fbf5e28…1e30`), byte-for-byte unmodified, built as C. `io`, `os`, `debug`, `package`, `linit`, and the two standalone binaries are left out of the build. |
| `lib/berry/` | Berry `v1.1.0`, the newest release tag (2022-08-01), with `src/` unmodified. Three spike files replace upstream's `default/` port: `berry_conf.h`, `spike_be_port.c` (no file system), and `spike_be_modtab.c` (`string` and `math` only). `coc_prebuild.py` runs Berry's constant-table generator as the library's `extraScript`. |
| `spike/SpikeScriptHost/` | Engine glue (`LuaEngine.cpp`, `BerryEngine.cpp`), the game scripts (`GameScripts.h`), and a native host runner (`host/run.sh`). Linked only by `x4pro-spike`. |
| `src/spike/` | Engine-neutral benchmark driver (`SpikeBench`) and device harness (`SpikeActivity`), compiled only with `SPIKE_SCRIPT_ENGINE`. |
| `src/main.cpp` | One `#ifdef SPIKE_SCRIPT_ENGINE` hook in `setup()`, after normal routing. Recovery mode and the crash report keep precedence. |
| `platformio.ini` | `[env:x4pro-spike]` extends `env:x4pro` and adds `-DSPIKE_SCRIPT_ENGINE`, the glue library, and a build-info script. No other env changed. |

### Workload

The workload is the same tic-tac-toe game in both languages, with the same algorithm, structure, and names; only the syntax differs.

**Host API.** Both engines bind the same functions:
- drawing: `clear`, `rect`, `line`, `text`, `present`, into the real framebuffer through `GfxRenderer`
- `millis`
- the callbacks `init(w,h)`, `draw()`, and `onTouch(x,y)`

**Game.** The computer's move is a full minimax search with no pruning. `bench()` runs that search from an empty board; it must visit 549,945 positions.

**Loop variant.** Idiomatic Berry `for i: 0..8` allocates a range object and an iterator on every loop. So both scripts also carry `benchWhile()`: the same search written with `while` loops, to separate allocation cost from interpreter cost.

**Driver.** A fixed script of 16 taps plays three full games:
- an optimal-play draw that includes one tap on an occupied cell
- a computer win
- a second draw

A tap after each finished game starts the next one. Each tap's `onTouch` return code is checked against the expected sequence `1011140113011114`.

**Order.** Each boot runs, in order:
1. Lua with the VM heap in internal RAM
2. Lua with the VM heap in PSRAM
3. Berry in internal RAM
4. Berry in PSRAM
5. error isolation for both engines

Each run gets a fresh FreeRTOS task (core 1, priority 1, 16 KB stack), so its stack high-water mark covers that run only.

**Internal-RAM runs only.** These also:
- show every `present()` as a fast refresh; e-ink time is excluded from `draw()`
- run the `while`-loop search
- run the GC-stopped search (Lua only)

**CPU clock.** The CPU is held at full clock with `HalPowerManager::Lock`. Each run logs `cpu_mhz_after_run` as proof.

### Sandbox as built

| | Lua | Berry |
|---|---|---|
| Error unwinding | `setjmp`/`longjmp` (compiled as C) | `setjmp`/`longjmp` (always C) |
| Protected entry | Every callback and the VM setup run through `lua_pcall` via a C trampoline, so nothing reaches `lua_atpanic`/`abort()` | Every callback and the VM setup run through `be_pcall` via a native trampoline. A raise with no handler calls `abort()`. |
| Runaway scripts | Count hook every 1000 instructions, via `lua_sethook(LUA_MASKCOUNT)`. `luaL_error` once the budget is spent. | **Yes, Berry can bound them.** With `BE_USE_PERF_COUNTERS` on, the VM calls the observability hook with `BE_OBS_VM_HEARTBEAT` every 2^(N-1) instructions (N = `BE_VM_OBSERVABILITY_SAMPLING`, set to 11, so every 1024). `be_raise(vm, "timeout_error", …)` from the hook unwinds to the host's `be_pcall`. |
| Budget sticky? | Yes: once spent, it re-raises on every tick, so a script's own `pcall` cannot swallow it | Yes: the same, since a script's `try/except` would otherwise catch it |
| Memory cap | Per-VM `lua_Alloc` (`ud` = engine) counts bytes, enforces the cap, and places the heap with `heap_caps_*` | Process-wide `BE_EXPLICIT_MALLOC/REALLOC/FREE` macros. The spike routes them to the active VM through a global and prefixes each block with its size, because Berry's `free` passes none. |
| Library surface | base, table, string, math; `load`, `loadfile`, `dofile` removed | string, math; no `os`, `sys`, `debug`, `gc`, `introspect`, `json`, `time`, or file system |

## Build-side results

All measurements come from pioarduino Core 6.1.19, platform 55.03.311 (Arduino-ESP32 3.3.11, ESP-IDF 5.5), xtensa GCC 14.2, and `-Os -fno-exceptions`. "RAM" is PlatformIO's static DRAM figure (`.data` + `.bss`). The app slot is 6,553,600 bytes.

### Firmware size

The single-engine rows are real link builds: `x4pro-spike` with `PLATFORMIO_BUILD_FLAGS=-DSPIKE_ONLY_LUA` or `-DSPIKE_ONLY_BERRY`, so the other engine is never referenced and never linked. They include the harness and the one engine's glue. All three spike rows are from the same harness revision, `b345fe0`.

| Build | Flash | Δ flash vs `x4pro` | Static RAM | Δ RAM | Slot used |
|---|---:|---:|---:|---:|---:|
| `x4pro` baseline (`develop` @ `05fafba`) | 5,657,610 | — | 101,816 | — | 86.3% |
| `x4pro-spike`, Lua only | 5,781,842 | +124,232 | 102,376 | +560 | 88.2% |
| `x4pro-spike`, Berry only | 5,770,614 | +113,004 | 102,400 | +584 | 88.1% |
| `x4pro-spike`, both engines | 5,870,630 | +213,020 | 102,864 | +1,048 | 89.6% |
| Firmware you flash (`1175389`, same engines, reworked summary screen) | 5,872,878 | +215,268 | 102,864 | +1,048 | 89.6% |

**Engine-exclusive cost.** This is the both-engine build minus the other engine's only build, so libc code both engines share is attributed to neither:

| | Flash | Static RAM |
|---|---:|---:|
| Lua (core + libs + glue + game script) | **100,016 B** | 464 B |
| Berry (core + port + glue + game script) | **88,788 B** | 488 B |
| Shared by both (harness ≈ 9 KB + common libc/libm ≈ 15 KB) | 24,216 B | — |

**Per-object breakdown.** From the both-engine map file (`esp_idf_size --files`):

| Group | Objects linked / built | Flash code | Flash data (rodata) | `.bss` + `.data` |
|---|---:|---:|---:|---:|
| Lua 5.4.7 core + base/table/string/math libs | 25 / 27 | 80,770 | 2,218 | 0 |
| Berry 1.1.0 core + port | 29 / 42 | 63,229 | 8,834 | 8 |
| `LuaEngine.cpp` (glue + Lua game script text) | 1 | 1,576 | 4,908 | 468 |
| `BerryEngine.cpp` (glue + Berry game script text) | 1 | 2,148 | 4,328 | 480 |
| Harness (`SpikeActivity` + `SpikeBench`) | 2 | 8,883 | 248 | 65 |

Neither engine adds IRAM (IRAM is already 100% used by the baseline). Neither core has any static RAM of its own: the glue's static RAM is its error and config text buffers.

### `-fno-exceptions`

- **Lua: compiles cleanly.** No warnings, and no `__cxa_throw`, `__gxx_personality*`, or `_Unwind_Resume` references in any object.
  - One toolchain trap: on xtensa g++ 14.2, `<limits.h>` alone does not define `LLONG_MAX` in C++ mode. So `luaconf.h`, included from C++, refuses 64-bit integers, while the library itself (compiled as C) uses them.
  - The glue includes `<climits>` first and `static_assert`s `sizeof(lua_Integer) == 8`. Any future C++ file that includes `lua.h` must do the same, or it fails to compile. This one fails loudly; a config that differed without an `#error` would be a silent ABI mismatch.
- **Berry: compiles cleanly.** No warnings and no exception symbols. It is C with `setjmp`/`longjmp` regardless of flags.
- Both cores are also warning-free under host GCC `-Wall -Wextra` (`spike/SpikeScriptHost/host/run.sh`).

### Binding code

Counts are non-blank, non-comment lines.

| | Lua | Berry |
|---|---:|---:|
| Glue file total (VM lifecycle, allocator, budget, trampolines, bindings) | 243 | 265 |
| of which: host-API bindings (`clear` `rect` `line` `text` `present` `millis`) | 37 | 40 |
| Port layer the engine requires from the embedder | 0 | 98 (`spike_be_port.c`) + 17 (`spike_be_modtab.c`) |
| Config | none (stock `luaconf.h`) | 35 changed lines in `berry_conf.h` |
| Build tooling | none | 21-line `coc_prebuild.py` (Python pre-build step) |

### Embedding friction found while building

- **Berry's only release tag is `v1.1.0` (2022).** Tasmota and upstream development track `master`. Pinning a release means pinning three-year-old code; tracking `master` means no releases at all.
- **Berry needs a pre-build code generator.** `coc` (Python) must regenerate `generate/*.h` whenever a source file or `berry_conf.h` changes, and it fails if `generate/` does not exist yet. Wired as a PlatformIO `extraScript`, it worked from a clean tree.
- **Berry's allocator is process-wide macros, not per-VM.** A per-VM cap or placement needs a global "active VM" pointer, plus a size header, because `free` gets no size.
- **Berry's GC control and heap counters are not in `berry.h`.** You need `be_gc.h`, which has no `extern "C"` guards and so needs wrapping for C++. `be_gc_setpause(vm, 0)` turns automatic GC *off*, despite the name.
- **`be_vm_new()` does not check its first allocation** (`be_assert` is compiled out). Running out of memory at VM creation is a crash, not an error return.
- **Berry's memory-exhaustion error arrives as a bare status code** (`BE_MALLOC_FAIL`, "berry error code 2") with no exception object. Lua reports "not enough memory".
- **Lua needs the `<climits>` include-order fix above.** Nothing else was needed: stock `luaconf.h`, stock sources.

## Host preview (not device numbers)

The native host runner builds the same engines, glue, driver, and scripts with the host GCC (Xeon @ 2.8 GHz, 64-bit). It exists to prove correctness before flashing, and it passed:
- both engines produce the expected touch-code sequence
- both searches visit 549,945 nodes
- all six isolation cases pass

**VM instruction counts are CPU-independent** (the same bytecode runs on the device), so they carry over exactly. **Timings and heap bytes do not**: the host has 64-bit pointers and a different CPU.

| | Lua | Berry |
|---|---:|---:|
| VM instructions, empty-board search, `for` loops | 67.2 M | 101.2 M |
| VM instructions, empty-board search, `while` loops | 79.6 M | 110.6 M |
| VM instructions, heaviest `onTouch` (first computer move) | 7.8 M | 11.8 M |
| Allocation churn, three games + `for` search | 3.0 KB | 265 MB (16,090 GC cycles) |
| Allocation churn, `while` search | 0.2 KB | 0.3 KB (0 GC cycles) |
| Host time, `for` search / `while` search | 0.45 s / 0.50 s | 3.7 s / 2.9 s |

Two findings:
1. Idiomatic Berry loops allocate on every iteration. That turns a pure-integer search into a garbage-collector workload.
2. Even without allocation, Berry spent about 5.7× Lua's time per search on the host. It also needed 1.4–1.5× more instructions per search.

On the S3 the gap is wider still; see the device results.

The desktop simulator (`.claude/skills/run-crosshatch-player`, with a local `simulator_x4pro_spike` env in the gitignored `platformio.local.ini`) also ran the full device harness end to end:
- boot hook, per-run tasks, and live game frames
- progress strip and results file
- summary screen, and tap-to-Home
- the crash-sentinel skip path

## Device results

One run on a real X4 Pro:
- firmware `1175389644c8`, ESP32-S3 rev 2 at 240 MHz for every run (`cpu_mhz_after_run: 240`), 8 MB PSRAM, ESP-IDF 5.5.5, Arduino-ESP32 3.3.11
- free internal heap at start: 187,892 B (largest block 139,252 B)
- total run time: 1,789.5 s (about 30 minutes), almost all of it Berry
- every run matched the expected touch codes and visited 549,945 nodes

"int" = VM heap in internal RAM; "PSRAM" = VM heap from `heap_caps_*` with `MALLOC_CAP_SPIRAM`.

| Metric | Lua int | Lua PSRAM | Berry int | Berry PSRAM |
|---|---:|---:|---:|---:|
| VM create, ms | 4.46 | 3.78 | 1.35 | 1.36 |
| Script compile, ms | 17.3 | 17.9 | 26.0 | 26.8 |
| Script top level, ms | 0.92 | 0.86 | 2.48 | 1.58 |
| `init()`, ms | 0.36 | 0.36 | 0.71 | 0.71 |
| `draw()` avg / max, ms (e-ink excluded) | 7.2 / 9.0 | 7.6 / 9.6 | 8.7 / 10.7 | 9.7 / 11.4 |
| `onTouch()` avg / max, s (includes the computer's search) | 0.65 / 3.62 | 0.65 / 3.64 | 10.3 / 57.3 | 10.4 / 57.7 |
| Minimax from empty board, s | **31.0** | **31.0** | **493.5** | **496.7** |
| … same search, `while` loops, s | 33.8 | — | 277.8 | — |
| … same search, GC stopped, s | 31.2 | — | n/a | n/a |
| Search throughput, M VM instructions/s (`for` / `while`) | 2.17 / 2.35 | 2.17 / — | 0.21 / 0.40 | 0.20 / — |
| Full GC with post-game heap live, ms | 0.54 | 1.26 | 0.51 | 0.52 |
| GC cycles / total / max pause | n/a | n/a | 15,879 / 24.4 s / 2.7 ms | 15,879 / 27.9 s / 3.2 ms |
| VM heap after create / after load, B | 9,023 / 22,771 | 9,023 / 22,771 | 2,640 / 17,836 | 2,640 / 17,836 |
| Peak VM heap, B | 25,727 | 25,551 | 31,460 | 31,460 |
| Allocation calls / churn over the run | 541 / 37 KB | 529 / 34 KB | 1,795,570 / 193.6 MB | 1,795,565 / 193.6 MB |
| Free internal RAM, before → low-water during run, B | 170,108 → 137,240 | 169,892 → 169,828 | 169,892 → 132,616 | 169,892 → 169,828 |
| Free PSRAM low-water during run, B | 8,254,440 | 8,222,224 | 8,254,440 | 8,217,324 |
| Leak check: free internal before vs after destroy | −216 B (first run only) | 0 | 0 | 0 |
| VM task stack used (of 16,384), B | 3,380 | 3,288 | 3,704 | 3,704 |
| Games and node count | correct | correct | correct | correct |

The 17 real e-ink fast refreshes in each internal-RAM run took 11.4 s in total, about 0.67 s each. They are excluded from `draw()`.

| Error isolation | Lua | Berry |
|---|---|---|
| Runtime error caught; same VM then answers `ok() == 42` | PASS in 1.4 ms: `[string "isolation"]:4: attempt to index a nil value (local 't')` | PASS in 0.6 ms: `attribute_error: 'nil' value has no attribute 'x'` |
| Infinite loop stopped by a 10M-instruction budget; VM recovers | PASS in 3.30 s: `instruction budget exceeded` | PASS in 3.08 s: `timeout_error: instruction budget exceeded` |
| Memory bomb stopped by a 256 KB cap (PSRAM); VM recovers | PASS in 0.19 s: `not enough memory` | PASS in 0.22 s: `berry error code 2`, a bare status code |

### What the device numbers say

- **Compute: Lua is 16× faster on idiomatic code, and 8× faster even when Berry avoids allocation.** In a plain loop both run about 3M instructions/s (the loop test: 10M in 3.30 s for Lua, 3.08 s for Berry), so Berry's core dispatch is not the problem. The gap comes from two places in Berry:
  - **Allocation.** `for i: 0..8` allocates a range object and an iterator per loop: 1.8M allocations and 194 MB of churn over the run.
  - **List indexing.** Every `b[i]` on a list is an `OP_GETIDX` that looks up the method `"item"` by name and calls it as a native function (`be_vm.c`, `opcase(GETIDX)`), because Berry lists are class instances. Lua's indexing is a direct array access.

  The garbage collector itself is only about 4% of Berry's time (18.3 s of 493.5 s in the search); allocating and indexing are the rest.
- **The Lua GC costs nothing measurable for this workload.** The Lua search allocates 212 B. With the collector stopped it took 31.2 s against 31.0 s running, which is within noise. A full collection of the live game heap takes 0.5 ms.
- **Heap placement: PSRAM is essentially free, and it spares the scarce internal RAM.** Lua's search time is identical in both placements (30.98 s each), `draw()` is 5% slower, and compile is 3% slower. With the VM in internal RAM, the internal low-water drops by 33 KB, to 137 KB. With the VM in PSRAM, internal RAM does not move. Berry's GC runs 14% slower in PSRAM.
- **Footprint: Berry wins at idle; Lua wins under load.**
  - A bare Berry VM is 2.6 KB against Lua's 9.0 KB, and it is created in 1.4 ms against 4.5 ms. That confirms the "about 10 KB" reputation.
  - Once the game is loaded, the two are close: Berry 17.8 KB, Lua 22.8 KB.
  - Berry's garbage pushes its peak above Lua's: 31.5 KB against 25.7 KB.
  - Either fits a 64 KB-per-game cap with room to spare.
- **Stack: a 3.3–3.7 KB high-water mark in both engines.** An 8 KB VM task stack is enough, with margin for deeper scripts.
- **Absolute speed is the real v1 constraint, whichever engine wins.**
  - The worst Lua move took 3.6 s: a full search from 8 empty cells, about 7.8M VM instructions.
  - A 10M-instruction budget is about 3 s of wall time.
  - v1 games need bounded AI (depth limits, alpha-beta, or precomputed tables), plus a per-callback budget of roughly 1–2M instructions to keep a tap under a second.

## Recommendation

**Use PUC Lua 5.4, compiled as C, with each game's VM heap in PSRAM behind a counting allocator with a cap.**

**Numbers.**
- Lua is 16× faster on the compute benchmark as idiomatically written, and 8× faster against Berry's allocation-free variant.
- Lua allocates almost nothing where Berry allocates 194 MB, so there are no GC pauses to schedule around. Berry's pauses are short (3.2 ms max), but there are 15,879 of them.
- Berry's advantages are small: about 11 KB less flash (88.8 KB vs 100.0 KB), a 7 KB smaller idle VM, and 3 ms faster VM creation. None of them matters on the S3 with 8 MB of PSRAM and 680 KB of flash headroom.

**Sandbox quality.** Both pass all three isolation tests, and both can bound a runaway script with an instruction hook that the script cannot swallow. Lua's design is cleaner:
- **Allocator.** Lua takes a per-VM allocator with a user pointer (`lua_newstate(alloc, ud)`), so caps, placement, and accounting are per game. Berry's allocator is a process-wide macro; per-VM caps need a global "active VM" and a size header on every block.
- **Failure reporting.** Lua reports running out of memory as an ordinary error. Berry reports it as a bare status code, and crashes if the very first allocation in `be_vm_new()` fails.
- **Trimming.** Lua's sandbox is trimmed by leaving library files out of the build and removing three base functions. Berry's is trimmed through config macros and a hand-written module table.

**`-fno-exceptions` fit.** A tie: both are C with `setjmp`/`longjmp`, and both build warning-free with no exception symbols. The rule is the same for both: bindings hold no RAII objects, and every entry into the VM goes through a protected call. Lua has one toolchain trap: include `<climits>` before `lua.h` in C++, which the spike pins with a `static_assert`.

**Author community for AI-assisted game writing (judgment, not measured).**
- **Lua** is one of the most widely used game scripting languages: LÖVE, PICO-8, Defold, Playdate, Roblox's Luau dialect, and WoW and Garry's Mod add-ons. AI assistants have seen a great deal of it, and the reference manual, books, and tutorials are mature.
- **Berry** is niche. Most public code is Tasmota drivers, not games.
- The spike also shows a concrete risk with Berry: the idiomatic loop an assistant would naturally write (`for i: 0..8`) is exactly the slow, allocating path. Lua's idiomatic code is its fast path.

**Ease of embedding.**
- **Lua:** vendored byte-for-byte from the release tarball, with no code generation, no port layer, stock config, and a public API that covers GC control and heap accounting. There is a current release (5.4.7).
- **Berry:** needs a Python constant-table generator at build time, a hand-written port and module table, internal headers for GC control, and `extern "C"` wrapping for C++. Its newest release tag is from 2022, with active development only on `master`.

**Carry into v1 architecture:**
1. One Lua VM per game.
   - The VM heap goes in PSRAM through a capped counting allocator; start the cap at 256 KB.
   - The VM gets its own task: 8 KB stack, core 1.
   - Every callback goes through a `lua_pcall` trampoline, with a sticky count-hook budget of 1–2M instructions per callback.
2. Trimmed libraries: base, table, string, math; remove `load`, `loadfile`, and `dofile`; no io, os, debug, or package.
3. Bindings are C-style functions only, with no RAII.
4. Budget and design first-party game AI for about 2–3M Lua VM instructions per second on the S3.
5. Follow-ups worth one measurement each before tuning:
   - `LUA_32BITS`: 64-bit integers are multi-instruction on Xtensa. This needs a one-line `luaconf.h` edit: `LUA_USER_H` is included after the number types are fixed, so it cannot switch them.
   - `-O2` for the Lua library alone.
   - Precompiling bundled game scripts to bytecode at package time, loaded only from trusted first-party packages. Compiling costs 17 ms per load today.

## Reading `/spike-results.txt`

It is plain text, written section by section as the run goes (the same lines also go to the serial log with the `SPIKE` tag).

### Header

| Key | Contents |
|---|---|
| `git_sha` | Firmware commit; `-dirty` if built from uncommitted changes |
| `build_env` | PlatformIO env |
| `built` | Build date and time |
| `engine_Lua`, `engine_Berry` | Engine versions |
| `engine_*_config` | Integer and float width, hook interval, exposed libraries |
| `build_flags` | Project build flags |
| `compiler_flags` | Compiler flags, including `-Os -fno-exceptions` |
| `esp_idf`, `arduino_esp32` | Framework versions |
| `chip` | Chip revision, cores, CPU MHz, PSRAM size |
| `free_heap_at_start` | Free heap when the run began |
| `workload` | The tap cells, the expected touch codes, and the budgets |

### One section per run

`[Lua / VM heap in internal RAM]`, `[Lua / VM heap in PSRAM]`, `[Berry / …]`:

| Key | Meaning |
|---|---|
| `completed`, `failure` | `completed: NO` plus `failure:` says where a run stopped |
| `vm_create_us` | VM creation, trimmed library set, and host bindings |
| `script_compile_us` | Source compile only (`luaL_loadbufferx` / `be_loadbuffer`) |
| `script_toplevel_us` | Running the chunk's top level (defines the callbacks) |
| `init_us` | `init(480, 800)` |
| `draw_us` | avg / max / count over 17 calls; e-ink `present()` time excluded |
| `onTouch_us` | avg / max / count over 16 taps; includes the computer's minimax, which dominates |
| `present_us_total` | E-ink refresh time spent inside `present()`; internal-RAM runs only |
| `touch_codes` | The actual `onTouch` return codes; must print `matches expected` |
| `onTouch_vm_instructions` | Instruction counts, at hook granularity (about 1000) |
| `minimax_empty_board_us` | The compute benchmark; nodes must be 549945 |
| `minimax_empty_board_while_loops_us` | The same search with `while` loops; internal-RAM runs only |
| `minimax_empty_board_gc_stopped_us` | Lua only, internal-RAM run: the search with the collector stopped. The difference from `minimax_empty_board_us` is Lua's GC cost for this workload. |
| `full_gc_us` | One full collection with the post-game heap live |
| `vm_heap_bytes` | Counting-allocator bytes at each stage |
| `vm_heap_peak_bytes` | Counting-allocator peak (requested bytes, before allocator overhead) |
| `alloc_churn_bytes` | Cumulative bytes allocated: what drives the GC |
| `gc_cycles` | Berry only, from the GC start/end hook events: count, total µs, and max pause per phase |
| `free_internal_bytes` / `free_psram_bytes` | `before`; `low_water_during_run`, exact, via `heap_caps_monitor_local_minimum_free_size_*`; `sampled_min`, after each callback; `after_destroy`, where comparing with `before` shows leaks; `min_since_boot` |
| `vm_task_stack_bytes` | The fresh task's stack size and bytes used at its high-water mark |
| `cpu_mhz_after_run` | Must be the full clock (240); 80 would mean power saving slipped in |

### Isolation sections

`[Lua error isolation]` and `[Berry error isolation]` hold one line per case. Each gives PASS/FAIL, the status of the failing call (`error` or `budget`), whether the same VM then answered `ok() == 42` (`recovered`), the time until the failure returned, and the error message.

### Footer

`total_elapsed_ms` and `status: complete`. A file without `status: complete` did not finish.

If a boot dies mid-run, the next boot skips the spike once and appends `previous_run: did not finish; last stage "…"`. The run before the current one is kept as `/spike-results.prev.txt`.

## Install and run (safest first)

The firmware is `firmware.bin` from `pio run -e x4pro-spike` at `1175389`, 5,877,872 bytes, SHA-256 `31a5859d17eaa12e9a308d8ce9de9686f9187b5e63bcadda6820332b5136ba37`.

1. **Web file manager, then Settings → SD firmware update (safest).**
   1. Upload `firmware.bin` to the SD card root with File Transfer.
   2. Open Settings → SD firmware update and pick it. The updater validates the image before writing it to the inactive OTA slot, so the running firmware stays intact until the switch.
   3. To go back, repeat this with your normal X4 Pro `.bin`.
2. **Web flasher "Custom .bin".** Use the same `firmware.bin`.
3. **`esptool`, over USB.**
   - The app lives in OTA slots: `app0` at `0x10000`, `app1` at `0x650000`, and `otadata` at `0xE000`.
   - After any OTA or SD update the device may be booting `app1`. Writing only `0x10000` would then change nothing, so also erase `otadata`, which makes the bootloader pick `app0`:
     ```bash
     esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 erase_region 0xe000 0x2000
     esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 firmware.bin
     ```

### On the device

- The spike starts after the boot splash. It shows each game and a status strip at the bottom, then a one-screen summary table.
- **It takes about 30 minutes (measured: 1,789 s), almost all of it Berry.** Keep the device on USB power and don't press power during the run.
- Tap anywhere, or press any button, on the summary to continue to Home. Retrieve `/spike-results.txt` with File Transfer.
- **The spike runs on every boot, including wakes from sleep.** Hold **Back** while booting to skip it. Recovery mode (Down + Power) still goes straight to the SD firmware picker.
