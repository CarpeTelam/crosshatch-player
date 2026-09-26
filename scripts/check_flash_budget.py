#!/usr/bin/env python3
"""
Check how much flash the game runtime adds to the X4 Pro firmware image.

Builds the x4pro env twice on the same commit, as configured (FREEINK_CAP_GAMES=1) and with that define removed,
and fails when the image with games is more than the limit larger than the image without. Both images come from
the same commit, so upstream growth never counts against the budget.

  build on    `pio run -e x4pro`, the normal build in .pio/build
  build off   the same with PLATFORMIO_BUILD_UNFLAGS=-DFREEINK_CAP_GAMES=1, built into .pio/build-games-off
  compare     compare the two firmware.bin sizes against the limit

The limit is in KiB (1 KiB = 1,024 bytes) because ESP32 flash and the app slot in partitions.csv are sized in
binary units (the slot is 0x640000 bytes = 6,400 KiB). The default 250 KiB is 256,000 bytes.

Each build also saves `pio project metadata` for itself. `compare` checks from it that the "on" build defines
FREEINK_CAP_GAMES=1 and the "off" build does not define it at all: a misspelled unflag, or the flag dropped from
the env, would make both images the same and the check pass without measuring anything.

Exit 0: within the limit. 1: over the limit. 2: the check could not run (build failed, missing output, wrong
flag state).

Local run, the same commands as the CI job (each build takes several minutes):
    python3 scripts/check_flash_budget.py build on
    python3 scripts/check_flash_budget.py build off
    python3 scripts/check_flash_budget.py compare [--limit-kib 250 | --limit-bytes N]
A limit below the measured difference (zero or negative is allowed) shows the failing case without a rebuild.
"""

import argparse
import json
import os
import pathlib
import subprocess
import sys

PIO_ENV = 'x4pro'
FLAG = 'FREEINK_CAP_GAMES'
# Written as the define appears in platformio.ini; compare checks the result, so an unflag that stops matching fails
# the job instead of passing it.
UNFLAG = f'-D{FLAG}=1'
KIB = 1024
DEFAULT_LIMIT_KIB = 250
PROJECT_DIR = pathlib.Path(__file__).resolve().parent.parent
OFF_BUILD_DIR = PROJECT_DIR / '.pio' / 'build-games-off'
DEFAULT_METADATA_DIR = PROJECT_DIR / '.pio' / 'flash-budget'
STATES = ('on', 'off')


class SetupError(Exception):
    pass


def build_environment(state, base=None):
    """Process environment for one build. Both start without the two overrides so "on" is the plain env."""
    env = dict(os.environ if base is None else base)
    env.pop('PLATFORMIO_BUILD_UNFLAGS', None)
    env.pop('PLATFORMIO_BUILD_DIR', None)
    if state == 'off':
        env['PLATFORMIO_BUILD_UNFLAGS'] = UNFLAG
        env['PLATFORMIO_BUILD_DIR'] = str(OFF_BUILD_DIR)
    return env


def metadata_path(metadata_dir, state):
    return pathlib.Path(metadata_dir) / f'{state}.json'


def pio(args, env, quiet=False):
    print(f'$ pio {" ".join(args)}', flush=True)
    try:
        code = subprocess.run(
            ['pio', *args], cwd=PROJECT_DIR, env=env, stdout=subprocess.DEVNULL if quiet else None
        ).returncode
    except OSError as exc:
        raise SetupError(f'cannot run pio: {exc}')
    if code != 0:
        raise SetupError(f'pio {" ".join(args)} failed ({code})')


def build(state, metadata_dir):
    env = build_environment(state)
    if state == 'off':
        print(f'Games off: PLATFORMIO_BUILD_UNFLAGS={UNFLAG} PLATFORMIO_BUILD_DIR={OFF_BUILD_DIR}', flush=True)
    target = metadata_path(metadata_dir, state)
    target.parent.mkdir(parents=True, exist_ok=True)
    # A stale file from an earlier run must never stand in for this build's metadata.
    target.unlink(missing_ok=True)
    pio(['run', '-e', PIO_ENV], env)
    # --json-output skips the dependency install; quiet keeps the whole metadata out of the log.
    pio(['project', 'metadata', '-e', PIO_ENV, '--json-output', '--json-output-path', str(target)], env, quiet=True)
    print(f'Saved {target}', flush=True)


def load_build(metadata_dir, state):
    """Return (defines, firmware.bin path) for one build, read from its saved metadata."""
    path = metadata_path(metadata_dir, state)
    try:
        data = json.loads(path.read_text())
        entry = data[PIO_ENV]
        defines = list(entry['defines'])
        # The image is written beside the ELF under the same name (firmware.elf -> firmware.bin).
        image = pathlib.Path(entry['prog_path']).with_suffix('.bin')
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise SetupError(f'no usable metadata for the games-{state} build at {path} ({exc}); run "build {state}"')
    return defines, image


def check_flag(state, defines):
    ours = [d for d in defines if d == FLAG or d.startswith(FLAG + '=')]
    if state == 'on' and ours != [f'{FLAG}=1']:
        raise SetupError(f'the games-on build must define exactly {FLAG}=1, found {ours or "none"}')
    if state == 'off' and ours:
        raise SetupError(f'the games-off build still defines {", ".join(ours)}; is the flag spelled {UNFLAG}?')


def signed(value):
    return f'{value:+,}'


def report(on_size, off_size, limit_bytes):
    diff = on_size - off_size
    within = diff <= limit_bytes
    if within:
        verdict = f'**Within budget**: {limit_bytes - diff:,} B to spare.'
    else:
        verdict = f'**Over budget** by {diff - limit_bytes:,} B.'
    lines = [
        '## x4pro flash budget',
        '',
        '| x4pro `firmware.bin` | Bytes | KiB |',
        '| --- | ---: | ---: |',
        f'| Games on (`{FLAG}=1`) | {on_size:,} | {on_size / KIB:,.1f} |',
        f'| Games off | {off_size:,} | {off_size / KIB:,.1f} |',
        f'| Difference | {signed(diff)} | {diff / KIB:+,.1f} |',
        f'| Limit | {limit_bytes:,} | {limit_bytes / KIB:,.1f} |',
        '',
        verdict,
        '',
    ]
    return within, '\n'.join(lines)


def compare(metadata_dir, limit_bytes, summary_path=None):
    """Return 0 within the limit, 1 over it; raise SetupError when the two builds cannot be compared."""
    sizes = {}
    images = {}
    for state in STATES:
        defines, image = load_build(metadata_dir, state)
        check_flag(state, defines)
        if not image.is_file():
            raise SetupError(f'missing games-{state} image {image}; run "build {state}"')
        images[state] = image.resolve()
        sizes[state] = image.stat().st_size
    if images['on'] == images['off']:
        raise SetupError(f'both builds point at the same image {images["on"]}')

    within, text = report(sizes['on'], sizes['off'], limit_bytes)
    print(text)
    if summary_path:
        with open(summary_path, 'a', encoding='utf-8') as summary:
            summary.write(text + '\n')
    return 0 if within else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--metadata-dir', default=str(DEFAULT_METADATA_DIR), help=argparse.SUPPRESS)
    sub = parser.add_subparsers(dest='command', required=True)
    build_parser = sub.add_parser('build', help='build x4pro with games on or off')
    build_parser.add_argument('state', choices=STATES)
    compare_parser = sub.add_parser('compare', help='compare the two images against the limit')
    limit = compare_parser.add_mutually_exclusive_group()
    limit.add_argument('--limit-kib', type=int, help=f'limit in KiB (default {DEFAULT_LIMIT_KIB})')
    limit.add_argument('--limit-bytes', type=int, help='limit in bytes')
    args = parser.parse_args(argv)

    try:
        if args.command == 'build':
            build(args.state, args.metadata_dir)
            return 0
        if args.limit_bytes is not None:
            limit_bytes = args.limit_bytes
        else:
            limit_bytes = (DEFAULT_LIMIT_KIB if args.limit_kib is None else args.limit_kib) * KIB
        return compare(args.metadata_dir, limit_bytes, os.environ.get('GITHUB_STEP_SUMMARY'))
    except SetupError as exc:
        print(f'error: {exc}', file=sys.stderr)
        # Also in the job summary, so a check that could not run does not look like an empty report.
        summary_path = os.environ.get('GITHUB_STEP_SUMMARY')
        if summary_path:
            with open(summary_path, 'a', encoding='utf-8') as summary:
                summary.write(f'## x4pro flash budget\n\nThe check could not run: {exc}\n')
        return 2


if __name__ == '__main__':
    sys.exit(main())
