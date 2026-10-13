# Fork scripts

The fork's tooling in `scripts/` (the CI checks, the release pipeline, and the game tools) follows one pattern, so
each new script copies it instead of inventing its own. Upstream's scripts in the same folder keep their own style;
these rules apply only to fork scripts.

## Rules

- **Fork-only path.** Add the script and its test to the **Game paths** of
  [`upstream-touches.md`](upstream-touches.md), so an upstream merge can never add or change a file of the same name.
- **Standard library only**, Python 3.11 or later, plus `git` where the script needs it. A check then runs before
  any toolchain install, and its tests run anywhere.
- **Sidecar test.** `scripts/<name>.py` has `scripts/<name>_test.py`, a `unittest` file that runs on its own with
  `python3 scripts/<name>_test.py`. It asserts the exit code of every outcome, so a regression that makes a check
  always pass is caught. Build fixtures (throwaway git repositories, fake `pio` on `PATH`, sparse files) in a temp
  directory; never depend on this repository's history or submodule.
- **Exit contract.** 0: passed. 1: a rule is broken. 2: the check could not run (a missing file or ref, a failed
  tool, a shallow clone). CI fails on both 1 and 2; the difference tells the reader whether to fix the change or the
  setup. A script may add a code above 2 for a broken rule whose fix differs from the rest, so CI can give that fix
  alone: `gen_game_icons.py` exits 3 (`PIN_MISMATCH`) when the SVGs and `SHA256SUMS` disagree, and only then does the
  `Icons up to date` job advise re-pinning. The module docstring says what each code means for that script and how to
  run it locally.
- **Shared plumbing from `scripts/fork_common.py`.** Never define another `SetupError`, git runner, exit-code
  handler, step-summary writer, or copy of the games-flag literal.
- Style: single quotes; the whole-tree clang-format check does not cover Python.

## `fork_common.py`

A script imports it by name (`import fork_common` or `from fork_common import SetupError`). That works when the
script runs as `python3 scripts/<name>.py` and from its sidecar test, which puts `scripts/` on `sys.path`; the release
workflow's sparse checkout of `scripts` includes it too.

| Name | Use |
| --- | --- |
| `PASS`, `FAIL`, `COULD_NOT_RUN` | The exit codes 0, 1, 2. |
| `Failure` | Raise for a broken rule (exit 1). A script may instead return `FAIL` after printing every problem. |
| `SetupError` | Raise when the check could not run (exit 2). Turn expected environment errors (`OSError` from a missing tool or file) into it. |
| `exit_code(step, summary_heading=None)` | Calls `step()`: `None` is 0, an `int` passes through, `Failure` is 1, `SetupError` is 2, each printed as `error: <message>` on stderr. With a heading, a `SetupError` also goes to the job summary. Anything else propagates as a traceback. |
| `git(*args, cwd=None, ok_codes=(0,))` | Runs `git` (`git -C cwd` when given) and returns `(exit code, stdout bytes)`. An exit code outside `ok_codes`, or no `git` on `PATH`, is a `SetupError` naming the command and git's stderr. |
| `git_text(*args, cwd=None)` | git's stdout as stripped text; any non-zero exit code is a `SetupError`. |
| `file_at(ref, path, cwd=None)` | The bytes of `path` at `ref`, or `None` when `ref` has no such file; an unknown `ref` is a `SetupError`. |
| `write_step_summary(text, path=None)` | Appends Markdown to `path`, or else to `$GITHUB_STEP_SUMMARY`; a no-op outside Actions. |
| `GAMES_MACRO`, `GAMES_BUILD_FLAG` | `FREEINK_CAP_GAMES` and `-DFREEINK_CAP_GAMES=1`, as `platformio.ini` spells it. |
| `API_LEVEL_HEADER`, `api_list_path(level)` | `lib/GameCore/ApiLevel.h` and `docs/crosshatch/api-level-<level>.txt`, relative to the repository root. |
| `ApiLevel`, `parse_api_level(text)` | Reads `(level, min_level, frozen)` from the header's one-line `#define`s; a missing, repeated, or malformed define is a `SetupError`. |
| `frozen_top(level)` | The highest frozen level of an `ApiLevel`: `API_LEVEL` when `API_LEVEL_FROZEN` is true, else `API_LEVEL - 1`; 0 for `None` (no header) or when nothing is frozen. |

A script's `main()` parses its arguments and returns `exit_code(...)`; the file ends with
`if __name__ == '__main__': sys.exit(main())`:

```python
def main(argv=None):
    args = parser.parse_args(argv)
    return fork_common.exit_code(lambda: check(args.ref), summary_heading='My check')
```

Add a name to `fork_common.py` only when two fork scripts would otherwise both write it, and give it a test in
`scripts/fork_common_test.py`. A helper only one script needs, such as `pio` runs or `GITHUB_OUTPUT` values, stays in
that script.

## CI

The `Fork script tests` job in `.github/workflows/crosshatch-ci.yml` runs every `scripts/*_test.py` on every pull
request, each file in its own process, and reports each failing file. It also fails a file whose output has no
`Ran <n> tests` line with n above 0, so a test file that forgets `unittest.main()`, and so exits 0 having run
nothing, cannot pass. A new sidecar test is picked up with no workflow change; `scripts/sim_sh_test.py`, which tests
the simulator skill's `sim.sh setup` and `check` on scratch git repositories, runs there too. A script that is itself
a CI gate gets its own job there, listed in `Crosshatch Test Status`'s `needs`, and is run once from a fresh clone
before its ticket counts as built. `gen_game_icons.py` is one: the `Icons up to date` job runs it, and it checks the
vendored SVGs against `assets/game-icons/SHA256SUMS` before rendering, so an edited SVG fails that job
(`docs/crosshatch/game-icons.md`).

Run the same tests locally from the repository root:

```sh
for t in scripts/*_test.py; do
  out="$(python3 "$t" 2>&1)" || { echo "FAILED: $t"; continue; }
  printf '%s\n' "$out" | grep -Eq '^Ran [1-9][0-9]* tests? in ' || echo "RAN NO TESTS: $t"
done
```

## Capturing a device run

`scripts/device_log.py` reads the serial logs of two or more devices at once, so a two-device run (epic-play-nearby)
has one log on one clock instead of two files kept by hand. It uses `termios` and `select` only, so it needs no
pyserial and runs on Linux and macOS. It needs a computer with one USB port per device, two for a two-device run.

```sh
python3 scripts/device_log.py capture \
  --port A=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_AA:BB:CC:DD:EE:01-if00 \
  --port B=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_AA:BB:CC:DD:EE:02-if00 \
  --out run-logs
```

- **Port paths.** Use `/dev/serial/by-id/...` on Linux: the name follows the board's serial number, while
  `/dev/ttyACM0` and `/dev/ttyACM1` swap or renumber when a device sleeps and wakes. On macOS the ports are
  `/dev/cu.usbmodem*` (use `cu`, not `tty`). List the directory with each device plugged in alone to learn which is
  which, then name them (`A`, `B`; letters, digits, `_`, `-`).
- **Output.** `--out` is required and is made if missing; point it outside the repository. `A.raw.log` and
  `B.raw.log` hold every byte each port gave, unchanged, across reconnects. `merged.log` holds one line per device
  line, `<UTC stamp> <NAME> <line>`, in host-time order, for example
  `2026-10-12T09:15:02.481Z A [41230] [INF] [GAME] sudoku: Playing -> Leaving on ForcedExit`. The stamp is taken when
  the line's newline arrives on the host, so a slow line is stamped up to its transfer time late. Files already in the
  directory are appended to.
- **Reconnect.** A port that disappears (a sleep renumbers the USB device) or is not there yet is tried again every
  0.5 s. The merged log gets `<stamp> <NAME> [device_log] lost <path>: <reason>; waiting for it to come back` and
  later `... connected <path>`; a half line at the loss is written to the merged log; the other device keeps flowing.
  Stop with Ctrl-C, SIGTERM, or `--duration SECONDS`; a second Ctrl-C acts as it always does. If the host clock steps
  backwards, the stamps hold at the latest value until it catches up, so the merged log stays in order.
- **Exit codes.** 0: done. 2: it could not run (a bad argument, a log that cannot be written, an unreadable log).
- **Permissions.** The port needs read permission (on Linux, membership of the `dialout` group). ModemManager can
  grab a CDC port and swallow bytes; stop it for the run (`sudo systemctl stop ModemManager`) if bytes go missing.
- **DTR and RTS** are cleared on every connect (best effort), so connecting does not reset a board that wires them to
  reset. Opening the port can still pulse them once before they are cleared. `--baud` defaults to 115200.
- **Not yet measured.** The tests use pseudo-terminals, which close with an immediate error and have no sleep timing.
  Whether the reconnect holds on a real X4 Pro's CDC port across a sleep is unmeasured until a device run.

`summary` lists the device-run packet's figures from a raw log or from `merged.log`, per device: the connects and
losses (merged log only), each `VM stopped` line with its game (the last lifecycle line before it) and time, the
dropped touches and hand-off screens per game, and the `[MEM]` lines (count, first and last, the smallest `Min Free`
and `MaxAlloc`; `--mem-all` lists every line). For a nearby match (epic-play-nearby) it adds a `nearby:` block: each
`radio up` line (one per bring-up), then per `LINK` session id the `STATE`s sent and received with the last `ver`, the
adopt verdicts, queue-full count, the resend, silence, `ABORT` sent, and link-refused running totals, each `ABORT`
received (reason and seat; the firmware logs one line per copy it hears, and a leaver sends three), each leave (reason, wait, unacked, `ABORT` copies), and the `GameLink` task's stack and its
high-water at each stop, with the least over all stops; then each entry into `PeerGone` and the reason line "the other
player is gone: ...", every `[ERR] [LINK]` line and the lobby's `[ERR] [GAME] Nearby: ...` lines, and any `LINK` line it has no pattern for (listed verbatim, so a
reworded firmware line shows up rather than vanishes). It takes one log per device, the raw log or `merged.log` and not
both, since a device found in two files would count every figure twice (exit 2):

```sh
python3 scripts/device_log.py summary run-logs/merged.log
python3 scripts/device_log.py summary --mem-all run-logs/A.raw.log
```

The `nearby:` block's patterns are pinned to the firmware's format strings (`src/games/GameLinkLog.h`,
`src/games/EspNowLink.h`, `src/activities/games/GameMatchActivity.cpp`) by `scripts/device_log_test.py`, which also
reads a recording of two simulator instances playing a round to "player left"; the simulator prints the device's format
with its own constants (a stack high-water of 2048 B, no `radio up` line), so the device's figures are the smoke run's.
