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

# The measured x4pro sections (2026-09-27); each fixture ELF starts from these.
RAM = {'.dram0.data': 29_615, '.dram0.bss': 72_200, '.noinit': 1}

# Real `xtensa-esp32s3-elf-size -A firmware.elf` output, shortened.
REAL_SIZE_OUTPUT = """\
.pio/build/x4pro/firmware.elf  :
section                    size         addr
.rtc.text                    92   1611653120
.rtc_noinit                5528   1342177792
.iram0.text               84091   1077363716
.dram0.dummy              68864   1070104576
.dram0.data               29615   1070173440
.noinit                       1   1070203055
.dram0.bss                72200   1070203056
.flash.text             2137372   1107296288
.ext_ram.dummy          5570528   1006632992
.debug_info            44334835            0
Total                  86252745
"""

# Real `xtensa-esp32s3-elf-readelf -W -S -s` output of a scratch object, shortened (the section and symbol numbers
# are the real ones; the two announced counts are those of the shortened rows). Its source: `struct Foo { Foo(); int v; }; Foo g_foo; int* get() { static Foo local; ... }`,
# `char g_buf[128]; static char s_buf[128]; constexpr char k_table[200]; constinit int g_ci[40];`,
# `inline char g_inline[100];`.
REAL_READELF_OUTPUT = """\
There are 18 section headers, starting at offset 0xb98:

Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 1] .group            GROUP           00000000 000034 000008 04     41  40  4
  [ 2] .literal._Z3getv  PROGBITS        00000000 00003c 000008 00  AX  0   0  4
  [13] .data             PROGBITS        00000000 000054 000000 00  WA  0   0  1
  [14] .bss              NOBITS          00000000 000054 000000 00  WA  0   0  1
  [15] .text._Z3getv     PROGBITS        00000000 000054 00002b 00  AX  0   0  4
  [23] .text.startup._GLOBAL__sub_I_g_foo PROGBITS        00000000 000098 00000b 00  AX  0   0  4
  [25] .ctors            PROGBITS        00000000 0000a4 000004 00  WA  0   0  4
  [26] .rela.ctors       RELA            00000000 00083c 00000c 0c   I 41  25  4
  [27] .bss.g_inline     NOBITS          00000000 0000a8 000064 00 WAG  0   0  1
  [28] .data.g_ci        PROGBITS        00000000 0000a8 0000a0 00  WA  0   0  4
  [29] .rodata._ZL7k_table PROGBITS        00000000 000148 0000c8 00   A  0   0  1
  [30] .bss._ZL5s_buf    NOBITS          00000000 000210 000080 00  WA  0   0  1
  [31] .bss.g_buf        NOBITS          00000000 000210 000080 00  WA  0   0  1
  [32] .bss._ZGVZ3getvE5local NOBITS          00000000 000210 000008 00  WA  0   0  8
  [33] .bss._ZZ3getvE5local NOBITS          00000000 000210 000004 00  WA  0   0  4
  [34] .bss.g_foo        NOBITS          00000000 000210 000004 00  WA  0   0  4
  [36] .xtensa.info      NOTE            00000000 000240 000038 00      0   0  1
Key to Flags:
  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),
  L (link order), O (extra OS processing required), G (group), T (TLS),
  C (compressed), x (unknown), o (OS specific), E (exclude),
  D (mbind), p (processor specific)

Symbol table '.symtab' contains 14 entries:
   Num:    Value  Size Type    Bind   Vis      Ndx Name
     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 00000000     0 FILE    LOCAL  DEFAULT  ABS ctor.cpp
     3: 00000000     0 SECTION LOCAL  DEFAULT   13 .data
     6: 00000000     8 OBJECT  LOCAL  DEFAULT   32 _ZGVZ3getvE5local
     7: 00000000     4 OBJECT  LOCAL  DEFAULT   33 _ZZ3getvE5local
     9: 00000000   128 OBJECT  LOCAL  DEFAULT   30 _ZL5s_buf
    11: 00000000   200 OBJECT  LOCAL  DEFAULT   29 _ZL7k_table
    14: 00000000    11 FUNC    LOCAL  DEFAULT   23 _GLOBAL__sub_I_g_foo
    34: 00000000    43 FUNC    GLOBAL DEFAULT   15 _Z3getv
    35: 00000000     0 NOTYPE  GLOBAL DEFAULT  UND __cxa_guard_acquire
    40: 00000000   100 OBJECT  WEAK   DEFAULT   27 g_inline
    42: 00000000     4 OBJECT  GLOBAL DEFAULT   34 g_foo
    43: 00000000   160 OBJECT  GLOBAL DEFAULT   28 g_ci
    44: 00000000   128 OBJECT  GLOBAL DEFAULT   31 g_buf
"""


def write_fake_tools(bin_dir):
    """A stand-in toolchain: `xt-size` and `xt-readelf` print the file they are given, which the tests fill with
    `size -A` or `readelf -W -S -s` text. Returns the matching cc_path."""
    bin_dir.mkdir(parents=True, exist_ok=True)
    for name in ('xt-size', 'xt-readelf'):
        tool = bin_dir / name
        tool.write_text(f'#!{sys.executable}\nimport sys\nsys.stdout.write(open(sys.argv[-1]).read())\n')
        tool.chmod(0o755)
    return str(bin_dir / 'xt-gcc')


def size_output(sections):
    rows = ['firmware.elf  :', 'section                    size         addr', '.iram0.text  84091   1077363716']
    rows += [f'{name:<20} {size:>10}   {1070173440 + i}' for i, (name, size) in enumerate(sections.items())]
    rows += ['.debug_info  44334835  0', f'Total  {sum(sections.values())}', '', '']
    return '\n'.join(rows)


def readelf_output(sections=(), symbols=()):
    """readelf -W -S -s text in the tool's layout. sections: (name, type, flags, size); symbols: (name, type, size,
    section name or UND/COM/ABS)."""
    lines = [
        f'There are {len(sections) + 1} section headers, starting at offset 0x2d8:',
        '',
        'Section Headers:',
        '  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al',
        '  [ 0]                   NULL            00000000 000000 000000 00      0   0  0',
    ]
    index = {}
    for number, (name, kind, flags, size) in enumerate(sections, 1):
        index[name] = number
        lines.append(f'  [{number:2}] {name:<17} {kind:<15} 00000000 000034 {size:06x} 00 {flags:>3}  0   0  4')
    lines += ['', f"Symbol table '.symtab' contains {len(symbols) + 1} entries:", '   Num:    Value  Size Type    Bind   Vis      Ndx Name',
              '     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND ']
    for number, (name, kind, size, where) in enumerate(symbols, 1):
        ndx = where if where in ('UND', 'COM', 'ABS') else str(index[where])
        size_text = f'{size:5}' if size < 100_000 else f'0x{size:x}'
        lines.append(f'{number:6}: 00000000 {size_text} {kind:<7} GLOBAL DEFAULT {ndx:>4} {name}')
    return '\n'.join(lines) + '\n'


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.meta = self.root / 'meta'
        self.summary = self.root / 'summary.md'
        self.cc_path = write_fake_tools(self.root / 'bin')

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, state, size, defines, image=True, dir_name=None, ram=None, elf=True):
        build_dir = self.root / (dir_name or state) / 'x4pro'
        build_dir.mkdir(parents=True, exist_ok=True)
        if image:
            with open(build_dir / 'firmware.bin', 'wb') as f:
                f.truncate(size)
        if elf:
            (build_dir / 'firmware.elf').write_text(size_output(RAM if ram is None else ram))
        self.meta.mkdir(exist_ok=True)
        data = {'x4pro': {'defines': defines, 'prog_path': str(build_dir / 'firmware.elf'), 'cc_path': self.cc_path}}
        cfb.metadata_path(self.meta, state).write_text(json.dumps(data))

    def run_compare(self, limit_bytes=cfb.DEFAULT_LIMIT_KIB * cfb.KIB, ram_limit_bytes=cfb.DEFAULT_RAM_LIMIT_BYTES):
        with contextlib.redirect_stdout(io.StringIO()):
            return cfb.compare(self.meta, limit_bytes, self.summary, ram_limit_bytes)

    def put_ram(self, bss_growth, **on_changes):
        """Both builds within the flash limit; the games-on ELF has bss_growth more .dram0.bss (plus on_changes)."""
        on_ram = dict(RAM, **on_changes)
        on_ram['.dram0.bss'] += bss_growth
        self.put('on', 1_000_000, ON_DEFINES, ram=on_ram)
        self.put('off', 1_000_000, OFF_DEFINES)

    def test_default_ram_limit_is_1_kib(self):
        self.assertEqual(cfb.DEFAULT_RAM_LIMIT_BYTES, 1024)

    def test_workflow_ram_limit_matches_the_script_default(self):
        workflow = cfb.PROJECT_DIR / '.github' / 'workflows' / 'crosshatch-ci.yml'
        values = [line.split(':', 1)[1].strip() for line in workflow.read_text().splitlines()
                  if line.strip().startswith('RAM_BUDGET_BYTES:')]
        self.assertEqual(values, [str(cfb.DEFAULT_RAM_LIMIT_BYTES)])

    def test_ram_under_and_at_the_limit_pass(self):
        for growth in (1023, 1024):
            with self.subTest(growth=growth):
                self.put_ram(growth)
                self.assertEqual(self.run_compare(), 0)

    def test_ram_one_over_the_limit_fails_while_flash_is_within(self):
        self.put_ram(1025)
        self.assertEqual(self.run_compare(), 1)
        text = self.summary.read_text()
        self.assertIn('Within budget', text.split('## x4pro static internal RAM')[0])
        self.assertIn('Over budget** by 1 B', text.split('## x4pro static internal RAM')[1])

    def test_ram_sums_all_three_sections(self):
        # 400 + 400 + 225 = 1,025 B: no single section is over, the sum is.
        self.put_ram(400, **{'.dram0.data': RAM['.dram0.data'] + 400, '.noinit': RAM['.noinit'] + 225})
        self.assertEqual(self.run_compare(), 1)
        self.assertEqual(self.run_compare(ram_limit_bytes=1025), 0)

    def test_ram_table_lists_each_section(self):
        self.put_ram(300)
        self.assertEqual(self.run_compare(), 0)
        text = self.summary.read_text()
        self.assertIn('| `.dram0.bss` | 72,500 | 72,200 | +300 |', text)
        self.assertIn('| `.dram0.data` | 29,615 | 29,615 | +0 |', text)
        self.assertIn('| `.noinit` | 1 | 1 | +0 |', text)
        self.assertIn('| Total | 102,116 | 101,816 | +300 |', text)
        self.assertIn('724 B to spare', text)

    def test_ram_shrink_passes(self):
        self.put_ram(-5000)
        self.assertEqual(self.run_compare(), 0)
        self.assertIn('-5,000', self.summary.read_text())

    def test_missing_required_ram_section_is_setup_error(self):
        for name in cfb.REQUIRED_RAM_SECTIONS:
            with self.subTest(section=name):
                self.put('on', 1_000_000, ON_DEFINES, ram={k: v for k, v in RAM.items() if k != name})
                self.put('off', 1_000_000, OFF_DEFINES)
                with self.assertRaisesRegex(cfb.SetupError, f'has no {name} section'):
                    self.run_compare()

    def test_missing_noinit_counts_as_zero(self):
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES, ram={k: v for k, v in RAM.items() if k != '.noinit'})
        self.assertEqual(self.run_compare(), 0)
        self.assertIn('| `.noinit` | 1 | 0 | +1 |', self.summary.read_text())

    def test_missing_elf_is_setup_error(self):
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES, elf=False)
        with self.assertRaisesRegex(cfb.SetupError, 'missing games-off ELF'):
            self.run_compare()

    def test_unusable_cc_path_is_setup_error(self):
        for cc_path in (None, '/opt/bin/xtensa-esp32s3-elf-g++'):
            with self.subTest(cc_path=cc_path):
                self.cc_path = cc_path
                self.put('on', 1_000_000, ON_DEFINES)
                self.put('off', 1_000_000, OFF_DEFINES)
                with self.assertRaisesRegex(cfb.SetupError, 'cannot find the toolchain'):
                    self.run_compare()

    def test_missing_size_tool_is_setup_error(self):
        self.cc_path = str(self.root / 'nowhere' / 'xt-gcc')
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        with self.assertRaisesRegex(cfb.SetupError, 'cannot run .*xt-size'):
            self.run_compare()

    def test_failing_size_tool_is_setup_error(self):
        tool = self.root / 'bin' / 'xt-size'
        tool.write_text('#!/bin/sh\necho "bad elf" >&2\nexit 1\n')
        self.put('on', 1_000_000, ON_DEFINES)
        self.put('off', 1_000_000, OFF_DEFINES)
        with self.assertRaisesRegex(cfb.SetupError, 'failed \\(1\\): bad elf'):
            self.run_compare()

    def test_cli_ram_limit_option(self):
        self.put_ram(100)
        self.assertEqual(self.run_cli('--ram-limit-bytes', '100')[0], 0)
        self.assertEqual(self.run_cli('--ram-limit-bytes', '99')[0], 1)
        self.assertEqual(self.run_cli()[0], 0)

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


class ParseTest(unittest.TestCase):
    """The parsers against real tool output, so a format they misread fails here and not silently in CI."""

    def test_size_output(self):
        sizes = cfb.section_sizes(REAL_SIZE_OUTPUT)
        self.assertEqual({name: sizes[name] for name in cfb.RAM_SECTIONS}, RAM)
        self.assertEqual(sizes['.debug_info'], 44_334_835)
        self.assertNotIn('Total', sizes)
        self.assertNotIn('section', sizes)

    def test_readelf_output_rows(self):
        sections, symbols, counts = cfb.parse_readelf(REAL_READELF_OUTPUT)
        self.assertEqual(counts, (18, 14))
        self.assertEqual(len(sections), 18)
        self.assertEqual(sections[0], cfb.Section('', 'NULL', '', 0))
        self.assertEqual(sections[23].name, '.text.startup._GLOBAL__sub_I_g_foo')
        self.assertEqual(sections[25], cfb.Section('.ctors', 'PROGBITS', 'WA', 4))
        self.assertEqual(sections[27].flags, 'WAG')
        self.assertEqual(sections[29].flags, 'A')
        self.assertEqual(sections[36].flags, '')
        self.assertEqual(len(symbols), 14)
        self.assertIn(cfb.Symbol('g_ci', 'OBJECT', 160, '28'), symbols)
        self.assertIn(cfb.Symbol('', 'NOTYPE', 0, 'UND'), symbols)

    def test_readelf_output_problems(self):
        problems, largest = cfb.object_problems(*cfb.parse_readelf(REAL_READELF_OUTPUT)[:2])
        self.assertEqual(largest, (160, 'g_ci'))
        text = '\n'.join(problems)
        self.assertIn('.ctors holds 4 B', text)
        self.assertIn('guard variable _ZGVZ3getvE5local', text)
        for flagged in ('_ZL5s_buf is 128 B', 'g_buf is 128 B', 'g_ci is 160 B'):
            self.assertIn(flagged, text)
        # constexpr k_table (read-only) and the COMDAT inline variable g_inline are not this object's statics.
        self.assertNotIn('k_table', text)
        self.assertNotIn('g_inline', text)
        self.assertEqual(len(problems), 5)

    def test_hex_symbol_size(self):
        _, symbols, _ = cfb.parse_readelf(readelf_output([('.bss.big', 'NOBITS', 'WA', 200_000)],
                                                      [('big', 'OBJECT', 200_000, '.bss.big')]))
        self.assertEqual(symbols[-1].size, 200_000)


class ObjectsTest(unittest.TestCase):
    """`objects` over a fixture project and build directory, with a stand-in readelf that prints each object."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.project = self.root / 'project'
        self.project.mkdir()
        self.build = self.root / 'build' / 'x4pro'
        self.build.mkdir(parents=True)
        self.meta = self.root / 'meta'
        self.meta.mkdir()
        self.summary = self.root / 'summary.md'
        self.cc_path = write_fake_tools(self.root / 'bin')
        self.write_metadata()

    def tearDown(self):
        self.tmp.cleanup()

    def write_metadata(self, defines=ON_DEFINES):
        data = {'x4pro': {'defines': defines, 'prog_path': str(self.build / 'firmware.elf'), 'cc_path': self.cc_path}}
        cfb.metadata_path(self.meta, 'on').write_text(json.dumps(data))

    def game(self, source, obj, sections=(), symbols=()):
        """A game source file and its object, which holds readelf text for the stand-in tool."""
        path = self.project / source
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('// game code\n')
        target = self.build / obj
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(readelf_output(sections, symbols))

    def static(self, name, size, flags='WA', kind='OBJECT'):
        section = f'.bss.{name}'
        return [(section, 'NOBITS', flags, size)], [(name, kind, size, section)]

    def run_objects(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return cfb.check_objects(self.meta, self.summary, self.project)

    def run_cli(self):
        proc = subprocess.run(
            [sys.executable, str(SCRIPT), '--metadata-dir', str(self.meta), '--project-dir', str(self.project),
             'objects'],
            env=dict(os.environ, GITHUB_STEP_SUMMARY=str(self.summary)),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return proc.returncode, proc.stdout + proc.stderr

    def test_no_game_code_passes_with_zero_objects(self):
        self.assertEqual(self.run_objects(), 0)
        self.assertIn('Game objects checked: 0. Largest mutable static: none', self.summary.read_text())

    def test_mutable_static_under_and_at_the_limit_pass(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', *self.static('a', 63))
        self.game('src/games/B.cpp', 'src/games/B.cpp.o', *self.static('b', 64))
        self.assertEqual(self.run_objects(), 0)
        self.assertIn('Game objects checked: 2. Largest mutable static: 64 B (`b` in `src/games/B.cpp.o`)',
                      self.summary.read_text())

    def test_mutable_static_one_over_the_limit_fails(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', *self.static('buffer', 65))
        self.assertEqual(self.run_objects(), 1)
        self.assertIn('`src/games/A.cpp.o`: mutable static buffer is 65 B in .bss.buffer', self.summary.read_text())

    def test_every_writable_kind_counts(self):
        cases = [
            self.static('data', 65, flags='WA'),
            ([('.noinit', 'NOBITS', 'WA', 65)], [('kept', 'OBJECT', 65, '.noinit')]),
            ([('.ext_ram.bss', 'NOBITS', 'WA', 65)], [('psram', 'OBJECT', 65, '.ext_ram.bss')]),
            ([], [('common', 'OBJECT', 65, 'COM')]),
            self.static('tls', 65, flags='WAT', kind='TLS'),
        ]
        for sections, symbols in cases:
            with self.subTest(symbol=symbols[0][0]):
                self.game('src/games/A.cpp', 'src/games/A.cpp.o', sections, symbols)
                self.assertEqual(self.run_objects(), 1)

    def test_read_only_data_of_any_size_passes(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', [('.rodata.table', 'PROGBITS', 'A', 200_000)],
                  [('table', 'OBJECT', 200_000, '.rodata.table'), ('code', 'FUNC', 500, '.rodata.table')])
        self.assertEqual(self.run_objects(), 0)

    def test_comdat_statics_are_not_the_objects_own(self):
        # An upstream header's inline singleton, with its guard, lands in every object that uses it.
        self.game('src/games/A.cpp', 'src/games/A.cpp.o',
                  [('.bss._ZZ3getvE8instance', 'NOBITS', 'WAG', 216), ('.bss._ZGVZ3getvE8instance', 'NOBITS', 'WAG', 8)],
                  [('_ZZ3getvE8instance', 'OBJECT', 216, '.bss._ZZ3getvE8instance'),
                   ('_ZGVZ3getvE8instance', 'OBJECT', 8, '.bss._ZGVZ3getvE8instance')])
        self.assertEqual(self.run_objects(), 0)

    def test_static_initializer_sections(self):
        for name, kind in (('.ctors', 'PROGBITS'), ('.init_array', 'INIT_ARRAY'), ('.ctors.00100', 'PROGBITS'),
                           ('.preinit_array', 'PREINIT_ARRAY'), ('.odd_name', 'INIT_ARRAY')):
            with self.subTest(section=name):
                self.game('src/games/A.cpp', 'src/games/A.cpp.o', [(name, kind, 'WA', 4)])
                self.assertEqual(self.run_objects(), 1)
                self.assertIn(f'static initializer: {name} holds 4 B', self.summary.read_text())

    def test_empty_initializer_section_passes(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', [('.ctors', 'PROGBITS', 'WA', 0), ('.rela.ctors', 'RELA', 'I', 12)])
        self.assertEqual(self.run_objects(), 0)

    def test_guard_variable_of_a_local_static_fails(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', *self.static('_ZGVZ3getvE5local', 8))
        self.assertEqual(self.run_objects(), 1)
        self.assertIn('guard variable _ZGVZ3getvE5local', self.summary.read_text())

    def test_undefined_symbols_are_ignored(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', [], [('_ZGVZ3getvE5local', 'NOTYPE', 0, 'UND'),
                                                               ('elsewhere', 'OBJECT', 500, 'UND')])
        self.assertEqual(self.run_objects(), 0)

    def test_every_game_directory_and_library_is_checked(self):
        places = [
            ('src/activities/games/M.cpp', 'src/activities/games/M.cpp.o'),
            ('lib/GameCore/Core.cpp', 'lib42e/GameCore/Core.cpp.o'),
            ('lib/GameNew/src/deep/New.cpp', 'lib7a1/GameNew/deep/New.cpp.o'),
        ]
        for source, obj in places:
            with self.subTest(source=source):
                self.game(source, obj, *self.static('big', 65))
                self.assertEqual(self.run_objects(), 1)
                self.assertIn(obj, self.summary.read_text())
                (self.build / obj).write_text(readelf_output(*self.static('small', 8)))

    def test_other_code_is_not_checked(self):
        self.game('src/main.cpp', 'src/main.cpp.o', *self.static('big', 4096))
        self.game('lib/Epub/Epub.cpp', 'lib9ed/Epub/Epub.cpp.o', *self.static('big', 4096))
        self.game('lib/lua/lapi.c', 'lib123/lua/lapi.c.o', *self.static('big', 4096))
        self.game('lib/GameCore/Core.cpp', 'lib42e/GameCore/Core.cpp.o')
        self.assertEqual(self.run_objects(), 0)
        self.assertIn('Game objects checked: 1.', self.summary.read_text())

    def test_sources_without_objects_are_setup_error(self):
        for source in ('src/games/A.cpp', 'lib/GameScript/Script.cpp', 'src/activities/games/M.c'):
            with self.subTest(source=source):
                path = self.project / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('// game code\n')
                with self.assertRaisesRegex(cfb.SetupError, 'has sources but .* holds no object'):
                    self.run_objects()
                path.unlink()

    def test_stale_object_of_a_deleted_source_is_not_checked(self):
        # An incremental build never deletes the object of a removed source.
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', *self.static('a', 8))
        self.game('src/games/Gone.cpp', 'src/games/Gone.cpp.o', *self.static('big', 4096))
        (self.project / 'src/games/Gone.cpp').unlink()
        self.assertEqual(self.run_objects(), 0)
        self.assertIn('Game objects checked: 1.', self.summary.read_text())

    def test_only_stale_objects_is_setup_error(self):
        self.game('src/games/Gone.cpp', 'src/games/Gone.cpp.o')
        (self.project / 'src/games/Gone.cpp').rename(self.project / 'src/games/New.cpp')
        with self.assertRaisesRegex(cfb.SetupError, 'holds no object'):
            self.run_objects()

    def test_header_only_library_needs_no_objects(self):
        (self.project / 'lib' / 'GameHeaders').mkdir(parents=True)
        (self.project / 'lib' / 'GameHeaders' / 'Only.h').write_text('#pragma once\n')
        self.assertEqual(self.run_objects(), 0)

    def test_games_off_metadata_is_setup_error(self):
        self.write_metadata(OFF_DEFINES)
        with self.assertRaisesRegex(cfb.SetupError, 'games-on build must define'):
            self.run_objects()

    def test_unreadable_readelf_output_is_setup_error(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o')
        (self.build / 'src/games/A.cpp.o').write_text('readelf: Error: not an ELF file\n')
        with self.assertRaisesRegex(cfb.SetupError, 'parsed 0 of None section headers'):
            self.run_objects()

    def test_a_row_that_does_not_parse_is_setup_error(self):
        # A future readelf layout that the patterns misread must fail the check, not skip the row.
        text = readelf_output(*self.static('big', 4096))
        for old, new in (('WA  0   0  4', 'WA  0   0  4  1'), ('  4096 OBJECT', '  4096  OBJECT')):
            with self.subTest(changed=new):
                self.game('src/games/A.cpp', 'src/games/A.cpp.o')
                (self.build / 'src/games/A.cpp.o').write_text(text.replace(old, new))
                with self.assertRaisesRegex(cfb.SetupError, 'could not read .* parsed'):
                    self.run_objects()

    def test_object_without_a_symbol_table_passes(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', [('.text', 'PROGBITS', 'AX', 0)])
        text = (self.build / 'src/games/A.cpp.o').read_text()
        (self.build / 'src/games/A.cpp.o').write_text(text.split("\nSymbol table")[0])
        self.assertEqual(self.run_objects(), 0)

    def test_cli_exit_codes(self):
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', *self.static('a', 64))
        self.assertEqual(self.run_cli()[0], 0)
        self.game('src/games/A.cpp', 'src/games/A.cpp.o', *self.static('a', 65))
        code, out = self.run_cli()
        self.assertEqual(code, 1)
        self.assertIn('mutable static a is 65 B', out)
        (self.root / 'bin' / 'xt-readelf').unlink()
        code, out = self.run_cli()
        self.assertEqual(code, 2)
        self.assertIn('error: cannot run', out)
        self.assertIn('## x4pro game objects\n\nThe check could not run', self.summary.read_text())


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
