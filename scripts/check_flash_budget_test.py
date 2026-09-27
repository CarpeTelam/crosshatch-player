#!/usr/bin/env python3
"""
Tests for check_flash_budget.py. Standard library only, no firmware build:

    python3 scripts/check_flash_budget_test.py [-v]

The cases write fixture `pio project metadata` files and sparse fake images to a temp directory and assert the
exit code of every outcome, so a regression that makes the check always pass is caught.
"""

import contextlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / 'check_flash_budget.py'
sys.path.insert(0, str(HERE))

import check_flash_budget as cfb  # noqa: E402

ON_DEFINES = ['FREEINK_DEVICE_X4PRO=1', 'FREEINK_CAP_GAMES=1', 'BOARD_HAS_PSRAM']
OFF_DEFINES = ['FREEINK_DEVICE_X4PRO=1', 'BOARD_HAS_PSRAM']


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.meta = self.root / 'meta'
        self.summary = self.root / 'summary.md'

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, state, size, defines, image=True, dir_name=None):
        build_dir = self.root / (dir_name or state) / 'x4pro'
        build_dir.mkdir(parents=True, exist_ok=True)
        if image:
            with open(build_dir / 'firmware.bin', 'wb') as f:
                f.truncate(size)
        self.meta.mkdir(exist_ok=True)
        data = {'x4pro': {'defines': defines, 'prog_path': str(build_dir / 'firmware.elf')}}
        cfb.metadata_path(self.meta, state).write_text(json.dumps(data))

    def run_compare(self, limit_bytes=cfb.DEFAULT_LIMIT_KIB * cfb.KIB):
        with contextlib.redirect_stdout(io.StringIO()):
            return cfb.compare(self.meta, limit_bytes, self.summary)

    def run_cli(self, *args):
        env = dict(os.environ)
        env['GITHUB_STEP_SUMMARY'] = str(self.summary)
        proc = subprocess.run(
            [sys.executable, str(SCRIPT), '--metadata-dir', str(self.meta), 'compare', *args],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return proc.returncode, proc.stdout + proc.stderr

    def test_default_limit_is_250_kib_in_bytes(self):
        self.assertEqual(cfb.DEFAULT_LIMIT_KIB * cfb.KIB, 256000)

    def test_workflow_limit_matches_the_script_default(self):
        workflow = cfb.PROJECT_DIR / '.github' / 'workflows' / 'crosshatch-ci.yml'
        values = [line.split(':', 1)[1].strip() for line in workflow.read_text().splitlines()
                  if line.strip().startswith('FLASH_BUDGET_KIB:')]
        self.assertEqual(values, [str(cfb.DEFAULT_LIMIT_KIB)])

    def test_within_budget_passes_and_writes_summary(self):
        self.put('on', 5_700_000, ON_DEFINES)
        self.put('off', 5_657_610, OFF_DEFINES)
        self.assertEqual(self.run_compare(), 0)
        text = self.summary.read_text()
        self.assertIn('5,700,000', text)
        self.assertIn('5,657,610', text)
        self.assertIn('+42,390', text)
        self.assertIn('Within budget', text)

    def test_exact_limit_passes(self):
        self.put('on', 1_256_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        self.assertEqual(self.run_compare(), 0)

    def test_over_budget_fails(self):
        self.put('on', 1_256_001, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        self.assertEqual(self.run_compare(), 1)
        self.assertIn('Over budget** by 1 B', self.summary.read_text())

    def test_zero_difference_passes_at_zero_limit_and_fails_below(self):
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        self.assertEqual(self.run_compare(0), 0)
        self.assertEqual(self.run_compare(-1), 1)

    def test_shrink_passes_with_negative_difference(self):
        self.put('on', 999_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        self.assertEqual(self.run_compare(), 0)
        self.assertIn('-1,000', self.summary.read_text())

    def test_flag_missing_from_on_build_is_setup_error(self):
        self.put('on', 1_000_000, OFF_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        with self.assertRaisesRegex(cfb.SetupError, 'games-on build must define'):
            self.run_compare()

    def test_flag_with_other_value_in_on_build_is_setup_error(self):
        self.put('on', 1_000_000, ['FREEINK_CAP_GAMES=0'])
        self.put('off', 1_000_000, OFF_DEFINES)
        with self.assertRaises(cfb.SetupError):
            self.run_compare()

    def test_flag_left_in_off_build_is_setup_error(self):
        for leftover in ('FREEINK_CAP_GAMES=1', 'FREEINK_CAP_GAMES'):
            with self.subTest(leftover=leftover):
                self.put('on', 1_000_000, ON_DEFINES)
                self.put('off', 1_000_000, OFF_DEFINES + [leftover])
                with self.assertRaisesRegex(cfb.SetupError, 'games-off build still defines'):
                    self.run_compare()

    def test_similar_define_names_are_not_the_flag(self):
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES + ['FREEINK_CAP_GAMES_EXTRA=1'])
        self.assertEqual(self.run_compare(), 0)

    def test_missing_image_is_setup_error(self):
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES, image=False)
        with self.assertRaisesRegex(cfb.SetupError, 'missing games-off image'):
            self.run_compare()

    def test_missing_metadata_is_setup_error(self):
        self.put('on', 1_000_000, ON_DEFINES)
        with self.assertRaisesRegex(cfb.SetupError, 'no usable metadata for the games-off build'):
            self.run_compare()

    def test_same_image_for_both_builds_is_setup_error(self):
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES, dir_name='on')
        with self.assertRaisesRegex(cfb.SetupError, 'same image'):
            self.run_compare()

    def test_cli_exit_codes_and_limit_options(self):
        self.put('on', 1_000_100, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        self.assertEqual(self.run_cli()[0], 0)
        self.assertEqual(self.run_cli('--limit-kib', '0')[0], 1)
        self.assertEqual(self.run_cli('--limit-bytes', '100')[0], 0)
        self.assertEqual(self.run_cli('--limit-bytes', '99')[0], 1)
        self.assertEqual(self.run_cli('--limit-kib', '1', '--limit-bytes', '1')[0], 2)  # argparse usage error
        self.assertIn('Over budget', self.summary.read_text())
        cfb.metadata_path(self.meta, 'off').unlink()
        code, out = self.run_cli()
        self.assertEqual(code, 2)
        self.assertIn('error:', out)
        self.assertIn('could not run', self.summary.read_text())

    def test_null_prog_path_is_setup_error(self):
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        cfb.metadata_path(self.meta, 'off').write_text(json.dumps({'x4pro': {'defines': [], 'prog_path': None}}))
        with self.assertRaisesRegex(cfb.SetupError, 'no usable metadata'):
            self.run_compare()


class BuildEnvironmentTest(unittest.TestCase):
    def test_on_is_the_plain_env_even_with_overrides_set(self):
        env = cfb.build_environment('on', {'PATH': '/bin', 'PLATFORMIO_BUILD_UNFLAGS': 'x', 'PLATFORMIO_BUILD_DIR': 'y'})
        self.assertEqual(env, {'PATH': '/bin'})

    def test_off_removes_exactly_the_flag_into_its_own_build_dir(self):
        env = cfb.build_environment('off', {'PATH': '/bin'})
        self.assertEqual(env['PLATFORMIO_BUILD_UNFLAGS'], '-DFREEINK_CAP_GAMES=1')
        self.assertEqual(env['PLATFORMIO_BUILD_DIR'], str(cfb.OFF_BUILD_DIR))
        self.assertNotEqual(cfb.OFF_BUILD_DIR, cfb.PROJECT_DIR / '.pio' / 'build')

    def test_unflag_matches_the_define_in_platformio_ini(self):
        text = (cfb.PROJECT_DIR / 'platformio.ini').read_text()
        section = text.split('[env:x4pro]', 1)[1].split('\n[', 1)[0]
        self.assertIn(cfb.UNFLAG, section.split())


class BuildTest(unittest.TestCase):
    """`build` with a stand-in `pio` on PATH: a failing pio is a setup error, and stale metadata is removed."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.meta = self.root / 'meta'
        self.path = os.environ.get('PATH', '')

    def tearDown(self):
        os.environ['PATH'] = self.path
        self.tmp.cleanup()

    def fake_pio(self, exit_code):
        script = self.bin / 'pio'
        script.write_text(f'#!/bin/sh\nexit {exit_code}\n')
        script.chmod(0o755)
        os.environ['PATH'] = f'{self.bin}{os.pathsep}{self.path}'

    def test_pio_failure_is_setup_error_and_stale_metadata_is_gone(self):
        self.fake_pio(1)
        stale = cfb.metadata_path(self.meta, 'off')
        self.meta.mkdir()
        stale.write_text('{}')
        with self.assertRaisesRegex(cfb.SetupError, 'pio project metadata .* failed'):
            cfb.build('off', self.meta)
        self.assertFalse(stale.exists())

    def recording_pio(self, write_image):
        """A pio that logs each call, writes metadata naming an ELF under the temp dir, and builds its image."""
        log = self.root / 'calls.log'
        elf = self.root / 'build' / 'firmware.elf'
        script = self.bin / 'pio'
        script.write_text(
            f'#!{sys.executable}\n'
            'import json, pathlib, sys\n'
            f'pathlib.Path({str(log)!r}).open("a").write(" ".join(sys.argv[1:3]) + "\\n")\n'
            'if sys.argv[1:3] == ["project", "metadata"]:\n'
            '    out = pathlib.Path(sys.argv[sys.argv.index("--json-output-path") + 1])\n'
            f'    out.write_text(json.dumps({{"x4pro": {{"defines": [], "prog_path": {str(elf)!r}}}}}))\n'
            f'elif sys.argv[1] == "run" and {write_image!r}:\n'
            f'    pathlib.Path({str(elf.parent)!r}).mkdir(parents=True, exist_ok=True)\n'
            f'    pathlib.Path({str(elf.with_suffix(".bin"))!r}).write_bytes(b"x" * 10)\n'
        )
        script.chmod(0o755)
        os.environ['PATH'] = f'{self.bin}{os.pathsep}{self.path}'
        return log

    def test_metadata_is_saved_before_the_build(self):
        # On a fresh tree `pio project metadata` empties the build dir, so it must never run after `pio run`.
        log = self.recording_pio(write_image=True)
        cfb.build('on', self.meta)
        self.assertEqual(log.read_text().splitlines(), ['project metadata', 'run -e'])
        self.assertTrue(cfb.metadata_path(self.meta, 'on').is_file())

    def test_build_without_an_image_is_setup_error(self):
        self.recording_pio(write_image=False)
        with self.assertRaisesRegex(cfb.SetupError, 'left no image'):
            cfb.build('off', self.meta)

    def test_missing_pio_is_setup_error(self):
        os.environ['PATH'] = str(self.bin)  # empty directory: no pio
        with self.assertRaisesRegex(cfb.SetupError, 'cannot run pio'):
            cfb.build('on', self.meta)

    def test_cli_build_failure_exits_2(self):
        self.fake_pio(3)
        proc = subprocess.run(
            [sys.executable, str(SCRIPT), '--metadata-dir', str(self.meta), 'build', 'on'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(proc.returncode, 2, proc.stderr)


if __name__ == '__main__':
    unittest.main()
