#!/usr/bin/env python3
"""
Tests for check_layers.py. Standard library only:

    python3 scripts/check_layers_test.py [-v]

Each case writes a small fixture tree (the game folders and a few upstream headers) to a temp directory, runs the
script on it, and asserts the exit code, so a regression that makes the check always pass is caught. SpineTest parses
the spine's layer table (SPINE_PATH, a tracked file) and compares it with TABLE and UPSTREAM_EDGES. The pre-fix
GameMatchActivity case uses the include lines and names e8420aaa added (retro O3).
"""

import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / 'check_layers.py'
sys.path.insert(0, str(HERE))

import check_layers  # noqa: E402
import check_upstream_touches  # noqa: E402

# A tree whose every edge the spine's table allows.
BASE = {
    'lib/GameCore/Manifest.h': '#pragma once\n#include <cstdint>\n#include <Memory.h>\n',
    'lib/GameCore/GameEvent.h': '#pragma once\n#include "Manifest.h"\n',
    'lib/GameIcons/GameIcons.h': '#pragma once\n#include <cstdint>\n',
    'lib/GameIcons/GameIcons.generated.h': '#pragma once\n#include <cstdint>\n',
    'lib/GameScript/StoreSlot.h': '#pragma once\n#include <mutex>\n',
    'lib/GameScript/Codec.h': '#pragma once\n#include <GameEvent.h>\n#include <Utf8.h>\n#include "StoreSlot.h"\n',
    'lib/GameScript/LuaGame.cpp': '#include <lua.hpp>\n#include <GameIcons.h>\n',
    'lib/lua/src/lua.hpp': '// Lua\n',
    'lib/Memory/Memory.h': '#pragma once\n',
    'lib/Logging/Logging.h': '#pragma once\n',
    'lib/Utf8/Utf8.h': '#pragma once\n',
    'lib/hal/HalMemory.h': '#pragma once\n',
    'lib/I18n/I18n.h': '#pragma once\n',
    'lib/GfxRenderer/GfxRenderer.h': '#pragma once\n',
    'lib/EpdFont/EpdFontData.h': '#pragma once\n',
    'src/fontIds.h': '#pragma once\n',
    'src/activities/Activity.h': '#pragma once\n',
    'src/components/UiAppHost.h': '#pragma once\n',
    'src/games/MatchStore.h': '#pragma once\n#include <HalMemory.h>\n#include <StoreSlot.h>\n',
    # A member alias and a using inside a namespace are not global re-exports; a .cpp's using-directive leaks nowhere.
    'src/games/GameVM.h': ('#pragma once\n#include <Codec.h>\n#include <freertos/task.h>\n#include <Logging.h>\n'
                           '#include "MatchStore.h"\nclass GameVM {\n  using Slot = GameScript::StoreSlot;\n};\n'
                           'namespace GameTouch {\nusing GameScript::StoreSlot;\n}\n'),
    'src/games/GameVM.cpp': '#include "GameVM.h"\nusing namespace GameScript;\n',
    'src/games/EspNowLink.cpp': '#include <esp_now.h>\n#include <WiFi.h>\n#include <mbedtls/sha256.h>\n',
    'src/games/FrameReplay.cpp': '#include <EpdFontData.h>\n#include <GfxRenderer.h>\n#include "fontIds.h"\n',
    # Upstream code that includes game code other than lib/GameScript and lib/lua, as its ledger row allows (rows 5,
    # 9, and 10; UPSTREAM_EDGES).
    # Each guard spelling the check accepts, nested and after an #elif included.
    'src/network/OtaUpdater.cpp': ('#include <WiFi.h>\n#if defined(FREEINK_CAP_GAMES) && !defined(SIMULATOR)\n'
                                   '#include <Manifest.h>\n#endif\n#ifdef FREEINK_CAP_GAMES\n#if X\n#else\n'
                                   '#include "games/GameVM.h"\n#endif\n#endif\n#if X\n#elif FREEINK_CAP_GAMES == 1 '
                                   '// games\n#include "games/GameVM.h"\n#endif\n'),
    'src/activities/ActivityManager.cpp': ('#include "Activity.h"\n#if FREEINK_CAP_GAMES\n'
                                           '#include "games/GameMatchActivity.h"\n#endif\n'),
    'src/components/CoverGridHomeUi.cpp': ('#include "UiAppHost.h"\n#include <GfxRenderer.h>\n#if FREEINK_CAP_GAMES\n'
                                           '#include <GameIcons.generated.h>\n#endif\n'),
    'src/games/GameTouch.h': '#pragma once\n#include <FreeInkUICore.h>\n',
    'src/games/ForkReleaseProbe.cpp': '#include <SecureHttpClient.h>\n',
    'src/games/GamesBuildAnchor.cpp': '#include <GameIcons.h>\n#include <climits>\n#include <lua.hpp>\n',
    'src/activities/games/GameMatchActivity.h': (
        '#pragma once\n#include <Manifest.h>\n#include "activities/Activity.h"\n#include "components/UiAppHost.h"\n'
        '#include "games/GameVM.h"\n#include "games/MatchStore.h"\n// Not GameScript::StoreSlot: MatchStore.\n'
        'class GameMatchActivity { MatchStore store; };\n'),
    'src/activities/games/GameMatchActivity.cpp': (
        '#include "GameMatchActivity.h"\n#include <Arduino.h>\n#include <GfxRenderer.h>\n#include <I18n.h>\n'
        '#include <games/GameVM.h>\n#include <freertos/task.h>\n'
        '/*\n#include <Codec.h>\n*/\nconst char* text = "GameScript::Canvas";\nvoid f() { GameCore::GameEvent e; }\n'),
}

# The include lines and names e8420aaa added to GameMatchActivity (retro O3).
PRE_FIX = {
    'src/activities/games/GameMatchActivity.h': (
        '#pragma once\n#include <HalMemory.h>\n#include <Manifest.h>\n#include <StoreSlot.h>\n'
        '#include "games/GameVM.h"\nclass GameMatchActivity {\n  std::unique_ptr<GameScript::StoreSlot> store;\n};\n'),
    'src/activities/games/GameMatchActivity.cpp': (
        '#include "GameMatchActivity.h"\n#include <Arduino.h>\n#include <Codec.h>\n'
        'void f() {\n  constexpr size_t slotBytes = GameScript::Codec::STORE_LIMIT;\n'
        '  const GameScript::Canvas canvas{};\n  GameScript::InputEvent event;\n}\n'),
}


class CheckLayersTest(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='check-layers-test-')
        self.env = dict(os.environ)
        self.env.pop('GITHUB_STEP_SUMMARY', None)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def tree(self, changes=None, remove=()):
        files = dict(BASE, **(changes or {}))
        for path in remove:
            files.pop(path)
        for path, text in files.items():
            target = pathlib.Path(self.tmp, path)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        return self.tmp

    def run_check(self, root):
        proc = subprocess.run([sys.executable, str(SCRIPT), '--root', str(root)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True, env=self.env)
        return proc.returncode, proc.stdout + proc.stderr

    def assert_fails(self, changes, *expected, remove=()):
        code, out = self.run_check(self.tree(changes, remove))
        self.assertEqual(code, 1, out)
        for text in expected:
            self.assertIn(text, out)
        return out

    def test_allowed_tree_passes(self):
        # Comments and string literals naming GameScript, and an include in a block comment, do not count.
        code, out = self.run_check(self.tree())
        self.assertEqual(code, 0, out)
        self.assertIn('passed', out)

    def test_pre_fix_game_match_activity_fails(self):
        out = self.assert_fails(
            PRE_FIX,
            'src/activities/games/GameMatchActivity.h:4: src/activities/games may not include <StoreSlot.h> '
            '(lib/GameScript)',
            'src/activities/games/GameMatchActivity.cpp:3: src/activities/games may not include <Codec.h> '
            '(lib/GameScript)',
            'GameMatchActivity.h:7: Screens name lib/GameScript',
            'GameMatchActivity.cpp:5: Screens name lib/GameScript',
            'GameMatchActivity.cpp:6: Screens name lib/GameScript',
            'GameMatchActivity.cpp:7: Screens name lib/GameScript',
            '6 problem(s)')
        self.assertNotIn('HalMemory', out)

    def test_screen_using_namespace_game_script_fails(self):
        self.assert_fails({'src/activities/games/Other.cpp': 'using namespace GameScript;\n'},
                          'Other.cpp:1: Screens name lib/GameScript')

    def test_screen_forward_declaring_game_script_fails(self):
        self.assert_fails({'src/activities/games/Other.h': 'namespace GameScript {\nclass StoreSlot;\n}\n'},
                          'Other.h:1: Screens name lib/GameScript')

    def test_screen_including_lua_fails(self):
        self.assert_fails({'src/activities/games/Other.cpp': '#include <lua.hpp>\n'},
                          'Other.cpp:1: src/activities/games may not include <lua.hpp> (lib/lua)')

    def test_unknown_header_fails(self):
        self.assert_fails({'src/games/Other.cpp': '#include <Mystery.h>\n'},
                          'src/games/Other.cpp:1: <Mystery.h> is not a header in lib/ or src/')

    def test_computed_include_fails(self):
        self.assert_fails({'src/games/Other.cpp': '#include LUA_HEADER\n'},
                          'src/games/Other.cpp:1: cannot check a computed include')

    def test_ambiguous_header_fails(self):
        self.assert_fails({'lib/Memory/Same.h': '', 'lib/Utf8/Same.h': '',
                           'src/games/Other.cpp': '#include <Same.h>\n'},
                          '"Same.h" matches headers in lib/Memory, lib/Utf8')

    def test_secure_http_client_only_from_fork_release_probe(self):
        self.assert_fails({'src/games/Other.cpp': '#include <SecureHttpClient.h>\n'},
                          'src/games/Other.cpp:1: src/games may not include <SecureHttpClient.h> '
                          '(sdk:SecureHttpClient.h)')

    def test_lua_only_from_the_build_anchor(self):
        self.assert_fails({'src/games/Other.cpp': '#include <lua.hpp>\n'},
                          'src/games/Other.cpp:1: src/games may not include <lua.hpp> (lib/lua)')

    def test_game_core_including_arduino_fails(self):
        self.assert_fails({'lib/GameCore/Clock.h': '#include <Arduino.h>\n'},
                          'lib/GameCore/Clock.h:1: lib/GameCore may not include <Arduino.h> (platform)')

    def test_game_core_reaching_src_by_a_relative_path_fails(self):
        self.assert_fails({'lib/GameCore/Up.h': '#include "../../src/games/GameVM.h"\n'},
                          'lib/GameCore/Up.h:1: lib/GameCore may not include "../../src/games/GameVM.h" (src/games)')

    def test_game_script_including_hal_fails(self):
        self.assert_fails({'lib/GameScript/Io.cpp': '#include <HalMemory.h>\n'},
                          'lib/GameScript/Io.cpp:1: lib/GameScript may not include <HalMemory.h> (lib/hal)')

    def test_adapters_including_upstream_src_fails(self):
        self.assert_fails({'src/games/Other.cpp': '#include "activities/Activity.h"\n'},
                          'src/games/Other.cpp:1: src/games may not include "activities/Activity.h" (src (upstream))')

    def test_upstream_src_laundering_game_script_fails(self):
        self.assert_fails({'src/util2/Launder.h': '#pragma once\n#include <StoreSlot.h>\n#include <lua.hpp>\n',
                           'src/activities/games/Other.cpp': '#include "util2/Launder.h"\n'},
                          'src/util2/Launder.h:2: upstream code may not include <StoreSlot.h> (lib/GameScript)',
                          'src/util2/Launder.h:3: upstream code may not include <lua.hpp> (lib/lua)', '2 problem(s)')

    def test_upstream_lib_laundering_game_script_fails(self):
        self.assert_fails({'lib/hal/HalLaunder.h': '#include "../GameScript/Codec.h"\n'},
                          'lib/hal/HalLaunder.h:1: upstream code may not include "../GameScript/Codec.h" '
                          '(lib/GameScript)')

    def test_upstream_file_without_a_ledger_edge_fails(self):
        self.assert_fails({'src/activities/home/HomeActivity.cpp': '#include <I18n.h>\n#include <GameIcons.h>\n'},
                          'src/activities/home/HomeActivity.cpp:2: upstream code may not include <GameIcons.h> '
                          '(lib/GameIcons); an upstream file includes game code only as its ledger row allows',
                          '1 problem(s)')

    def test_upstream_file_beyond_its_ledger_edge_fails(self):
        self.assert_fails({'src/components/CoverGridHomeUi.cpp': ('#if FREEINK_CAP_GAMES\n'
                                                                  '#include <GameIcons.generated.h>\n'
                                                                  '#include <Manifest.h>\n#endif\n')},
                          'src/components/CoverGridHomeUi.cpp:3: upstream code may not include <Manifest.h> '
                          '(lib/GameCore)', '1 problem(s)')

    def test_unguarded_upstream_edge_fails(self):
        # Deferred finding 3.6: the ledgered include with no guard, in the #else, or behind a guard that does not
        # require the games flag. Each case is one problem at the include's line.
        cases = {
            'no guard': ('#include <GfxRenderer.h>\n#include <GameIcons.generated.h>\n', 2),
            'the #else': ('#if FREEINK_CAP_GAMES\n#else\n#include <GameIcons.generated.h>\n#endif\n', 3),
            '#ifndef': ('#ifndef FREEINK_CAP_GAMES\n#include <GameIcons.generated.h>\n#endif\n', 2),
            'negated': ('#if !FREEINK_CAP_GAMES\n#include <GameIcons.generated.h>\n#endif\n', 2),
            'an || condition': ('#if FREEINK_CAP_GAMES || X\n#include <GameIcons.generated.h>\n#endif\n', 2),
            'another flag': ('#if FREEINK_CAP_GAMES_EXTRA\n#include <GameIcons.generated.h>\n#endif\n', 2),
            'after the #endif': ('#if FREEINK_CAP_GAMES\n#endif\n#include <GameIcons.generated.h>\n', 3),
            'an #elif after it': ('#if FREEINK_CAP_GAMES\n#elif X\n#include <GameIcons.generated.h>\n#endif\n', 3),
            'a commented guard': ('// #if FREEINK_CAP_GAMES\n#include <GameIcons.generated.h>\n', 2),
            'an #elifndef after it': ('#if FREEINK_CAP_GAMES\n#elifndef X\n#include <GameIcons.generated.h>\n'
                                      '#endif\n', 3),
            'an #elifdef after it': ('#if FREEINK_CAP_GAMES\n#elifdef X\n#include <GameIcons.generated.h>\n'
                                     '#endif\n', 3),
            'an #elifndef of the flag': ('#if X\n#elifndef FREEINK_CAP_GAMES\n#include <GameIcons.generated.h>\n'
                                         '#endif\n', 3),
            # epic-icon-library retrospective R9 (b): the condition is read whole after joining the continued line, so
            # its || counts.
            'an || on a continued line': ('#if FREEINK_CAP_GAMES && \\\n    X || 1\n#include <GameIcons.generated.h>\n'
                                          '#endif\n', 3),
            # After splicing, the #if is part of the #define's replacement text, not a directive.
            'an #if continuing a #define': ('#define A \\\n#if FREEINK_CAP_GAMES\n#include <GameIcons.generated.h>\n'
                                            '#endif\n', 3),
            # Splicing comes before comments are blanked, as in C: the #if continues the line comment.
            'an #if continuing a // comment': ('// x \\\n#if FREEINK_CAP_GAMES\n#include <GameIcons.generated.h>\n'
                                               '#endif\n', 3),
            # The condition may go on after a block comment that spans lines, so the guard does not count.
            'a block comment spanning lines': ('#if FREEINK_CAP_GAMES /*\n*/ || 1\n#include <GameIcons.generated.h>\n'
                                               '#endif\n', 3),
        }
        for name, (text, line) in cases.items():
            with self.subTest(name):
                self.assert_fails({'src/components/CoverGridHomeUi.cpp': text},
                                  f'src/components/CoverGridHomeUi.cpp:{line}: upstream code includes '
                                  '<GameIcons.generated.h> (lib/GameIcons) outside an #if FREEINK_CAP_GAMES branch',
                                  '1 problem(s)')

    def test_a_guard_whose_comment_spans_lines_is_told_why_it_does_not_count(self):
        # Cross-story finding 13: the message was the unguarded include's, with no word on the comment.
        text = '#if FREEINK_CAP_GAMES /*\n*/\n#include <GameIcons.generated.h>\n#endif\n'
        self.assert_fails({'src/components/CoverGridHomeUi.cpp': text},
                          'src/components/CoverGridHomeUi.cpp:3: upstream code includes <GameIcons.generated.h> '
                          '(lib/GameIcons) outside an #if FREEINK_CAP_GAMES branch; its #if line opens a block '
                          'comment that spans lines', '1 problem(s)')
        # An include that is unguarded for another reason gets no such hint.
        code, out = self.run_check(self.tree({'src/components/CoverGridHomeUi.cpp':
                                              '/*\n*/\n#include <GameIcons.generated.h>\n'}))
        self.assertEqual(code, 1, out)
        self.assertNotIn('block comment', out)

    def test_continued_guard_passes(self):
        cases = {
            # A guard continued onto a second line is read whole.
            'a continued guard': ('#if FREEINK_CAP_GAMES && \\\n    defined(X)\n#include <GameIcons.generated.h>\n'
                                  'static const int kIcons[] = {1, \\\n    2};\n#endif\n'),
            # Every physical line of a logical line inside a games branch is guarded.
            'an include continuing a #define': ('#if FREEINK_CAP_GAMES\n#define A \\\n'
                                               '#include <GameIcons.generated.h>\n#endif\n'),
            # A backslash before a line comment, not at the line's end, splices nothing: the #if stays a directive.
            'a backslash before a comment': ('#ifdef A\n#else \\ // x\n#if FREEINK_CAP_GAMES\n'
                                             '#include <GameIcons.generated.h>\n#endif\n#endif\n'),
        }
        for name, text in cases.items():
            with self.subTest(name):
                code, out = self.run_check(self.tree({'src/components/CoverGridHomeUi.cpp': text}))
                self.assertEqual(code, 0, out)

    def test_logical_lines_keep_physical_numbers(self):
        text = 'a \\\nb\\  \nc\nd\ne \\'
        self.assertEqual(list(check_layers.logical_lines(text)), [(1, 3, 'a bc'), (4, 4, 'd'), (5, 5, 'e ')])

    def test_games_branch_reads_each_guard_spelling(self):
        accepted = [('if', 'FREEINK_CAP_GAMES'), ('if', 'FREEINK_CAP_GAMES == 1'), ('ifdef', 'FREEINK_CAP_GAMES'),
                    ('if', 'defined(FREEINK_CAP_GAMES)'), ('if', 'defined FREEINK_CAP_GAMES'),
                    ('elif', 'FREEINK_CAP_GAMES && !defined(SIMULATOR)'), ('if', 'X && FREEINK_CAP_GAMES'),
                    ('elifdef', 'FREEINK_CAP_GAMES')]
        refused = [('ifndef', 'FREEINK_CAP_GAMES'), ('if', '!FREEINK_CAP_GAMES'), ('if', 'FREEINK_CAP_GAMES == 0'),
                   ('if', 'FREEINK_CAP_GAMES || X'), ('if', '(FREEINK_CAP_GAMES)'), ('else', ''),
                   ('ifdef', 'FREEINK_CAP_GAMES_X'), ('if', 'X'), ('elifndef', 'FREEINK_CAP_GAMES'),
                   ('elifdef', 'X'), ('elifndef', 'X')]
        for kind, condition in accepted:
            self.assertTrue(check_layers.games_branch(kind, condition), (kind, condition))
        for kind, condition in refused:
            self.assertFalse(check_layers.games_branch(kind, condition), (kind, condition))

    def test_upstream_header_without_a_ledger_edge_fails(self):
        self.assert_fails({'src/components/CoverGridHomeUi.h': '#pragma once\n#include <GameIcons.generated.h>\n'},
                          'src/components/CoverGridHomeUi.h:2: upstream code may not include '
                          '<GameIcons.generated.h> (lib/GameIcons); an upstream file includes game code only as its '
                          'ledger row allows', '1 problem(s)')

    def test_upstream_lib_file_without_a_ledger_edge_fails(self):
        self.assert_fails({'lib/hal/HalIcons.h': '#pragma once\n#include <GameIcons.h>\n'},
                          'lib/hal/HalIcons.h:2: upstream code may not include <GameIcons.h> (lib/GameIcons); an '
                          'upstream file includes game code only as its ledger row allows', '1 problem(s)')

    def test_ledgered_upstream_file_still_may_not_include_game_script(self):
        self.assert_fails({'src/network/OtaUpdater.cpp': ('#if FREEINK_CAP_GAMES\n#include <Manifest.h>\n'
                                                          '#include <Codec.h>\n#endif\n')},
                          'src/network/OtaUpdater.cpp:3: upstream code may not include <Codec.h> (lib/GameScript); '
                          'Screens may include it', '1 problem(s)')

    def test_screen_namespace_alias_fails(self):
        self.assert_fails({'src/activities/games/Other.cpp': 'namespace GS = GameScript;\nGS::StoreSlot* s;\n'},
                          'Other.cpp:1: Screens name lib/GameScript', '1 problem(s)')

    def test_screen_define_fails(self):
        self.assert_fails({'src/activities/games/Other.cpp': '#define GS GameScript\n'},
                          'Other.cpp:1: Screens name lib/GameScript')

    def test_adapters_header_global_using_namespace_fails(self):
        self.assert_fails({'src/games/Leak.h': '#pragma once\n#include <StoreSlot.h>\n\nusing namespace GameScript;\n'},
                          'src/games/Leak.h:4: src/games header re-exports lib/GameScript at global scope '
                          '(using namespace GameScript;)')

    def test_adapters_header_global_alias_fails(self):
        self.assert_fails({'src/games/Leak.h': ('#pragma once\n#include <StoreSlot.h>\nnamespace Games {\n}\n'
                                                'using Slot =\n    GameScript::StoreSlot;\n'
                                                'namespace GS = GameScript;\nusing GameScript::StoreSlot;\n')},
                          'src/games/Leak.h:5: src/games header re-exports lib/GameScript at global scope '
                          '(using Slot = GameScript::StoreSlot;)',
                          'src/games/Leak.h:7:', 'src/games/Leak.h:8:', '3 problem(s)')

    def test_include_next_and_import_are_read(self):
        self.assert_fails({'src/activities/games/Other.cpp': '#include_next <StoreSlot.h>\n#import <Codec.h>\n'},
                          'Other.cpp:1: src/activities/games may not include <StoreSlot.h>',
                          'Other.cpp:2: src/activities/games may not include <Codec.h>')

    def test_inline_and_cxx_files_are_scanned(self):
        self.assert_fails({'src/activities/games/Other.inl': '#include <StoreSlot.h>\n',
                           'src/activities/games/Other.cxx': '#include <Codec.h>\n'},
                          'Other.inl:1: src/activities/games may not include <StoreSlot.h>',
                          'Other.cxx:1: src/activities/games may not include <Codec.h>')

    def test_screen_radio_and_crypto_headers_fail(self):
        radio = '#include <esp_now.h>\n#include <WiFi.h>\n#include <mbedtls/sha256.h>\n#include <openssl/sha.h>\n'
        self.assert_fails({'src/activities/games/Other.cpp': radio},
                          'Other.cpp:1: src/activities/games may not include <esp_now.h> (platform radio/crypto)',
                          'Other.cpp:4: src/activities/games may not include <openssl/sha.h>', '4 problem(s)')

    def test_missing_game_folder_could_not_run(self):
        root = self.tree(remove=[p for p in BASE if p.startswith('lib/GameIcons/')])
        code, out = self.run_check(root)
        self.assertEqual(code, 2, out)
        self.assertIn('no lib/GameIcons', out)

    def test_missing_root_could_not_run(self):
        code, out = self.run_check(pathlib.Path(self.tmp, 'nowhere'))
        self.assertEqual(code, 2, out)

    def test_failure_writes_the_step_summary(self):
        summary = pathlib.Path(self.tmp, 'summary.md')
        self.env['GITHUB_STEP_SUMMARY'] = str(summary)
        code, out = self.run_check(self.tree(PRE_FIX))
        self.assertEqual(code, 1, out)
        self.assertIn('## Layer check', summary.read_text())
        self.assertIn('<StoreSlot.h>', summary.read_text())


class TableTest(unittest.TestCase):
    """The data mirrors the spine's rules that the fixtures do not reach."""

    def test_screens_never_reach_game_script_or_lua(self):
        self.assertNotIn('lib/GameScript', check_layers.LAYERS[check_layers.SCREENS])
        self.assertNotIn('lib/lua', check_layers.LAYERS[check_layers.SCREENS])

    def test_game_core_has_no_device_dependency(self):
        self.assertEqual(check_layers.LAYERS['lib/GameCore'], {'std', 'lib/Memory', 'lib/JsonParser'})

    def test_every_component_has_a_row(self):
        self.assertEqual(set(check_layers.COMPONENTS), set(check_layers.LAYERS))

    def test_beyond_table_only_adds_to_table_rows(self):
        self.assertLessEqual(set(check_layers.BEYOND_TABLE), set(check_layers.TABLE))
        for comp, edges in check_layers.TABLE.items():
            self.assertFalse(edges & check_layers.BEYOND_TABLE.get(comp, set()), comp)

    def test_every_upstream_edge_is_a_ledger_row(self):
        ledger = (HERE.parent / check_upstream_touches.LEDGER_PATH).read_text()
        rows = set(check_upstream_touches.parse_ledger(ledger)['Ledger'])
        for path, targets in check_layers.UPSTREAM_EDGES.items():
            self.assertIn(path, rows)
            self.assertTrue(targets <= set(check_layers.COMPONENTS), path)
            self.assertFalse(targets & check_layers.GAME_SCRIPT_OR_LUA, path)


TABLE_HEADER = '| Layer | Lives in | May depend on |'
ENGINE_ROW = 'Engine (vendored)'
HOOKS_ROW = 'Upstream hooks (AD-3 ledger rows)'
PARENTHETICAL = re.compile(r'\s*\([^()]*\)')
TERM_SEPARATOR = re.compile(r'\s*[,;]\s*| and | / | or ')
REPO_PATH = re.compile(r'(?:lib|src)/[A-Za-z0-9_./-]+')


def spine_terms(row, text, problems):
    """The components the terms of one table cell name (backticks and parentheticals dropped, SPINE_TERMS applied);
    an unknown term is appended to problems."""
    reached = set()
    for term in TERM_SEPARATOR.split(PARENTHETICAL.sub('', text.replace('`', '')).strip()):
        if term in check_layers.SPINE_TERMS:
            if check_layers.SPINE_TERMS[term] is not None:
                reached.add(check_layers.SPINE_TERMS[term])
        elif REPO_PATH.fullmatch(term):
            reached.add(term.rstrip('/'))
        else:
            problems.append(f'{row}: unknown spine term "{term}"; add it to SPINE_TERMS in scripts/check_layers.py')
    return reached


def parse_hooks(lives_in, depends, problems):
    """{upstream path: components} from the Upstream hooks row's two cells."""
    paths = {}
    for part in lives_in.replace('`', '').split(','):
        match = re.fullmatch(r'\s*(\S+) \(row (\d+)\)\s*', part)
        if not match:
            problems.append(f'{HOOKS_ROW}: cannot read "{part.strip()}" as "<path> (row <n>)"')
            continue
        paths[match.group(2)] = match.group(1)
    edges = {}
    never_read = False
    for segment in PARENTHETICAL.sub('', depends.replace('`', '')).split(';'):
        segment = segment.strip()
        row = re.fullmatch(r'row (\d+): (.*)', segment)
        if row and row.group(1) in paths:
            edges[paths[row.group(1)]] = spine_terms(HOOKS_ROW, row.group(2), problems)
        elif segment.startswith('never '):
            never_read = True
            never = spine_terms(HOOKS_ROW, segment[len('never '):].split('. ')[0], problems)
            if never != check_layers.GAME_SCRIPT_OR_LUA:
                problems.append(f'{HOOKS_ROW}: the spine never allows {", ".join(sorted(never))}; '
                                f'GAME_SCRIPT_OR_LUA is {", ".join(sorted(check_layers.GAME_SCRIPT_OR_LUA))}')
        else:
            problems.append(f'{HOOKS_ROW}: cannot read "{segment}" as "row <n>: <terms>" of a row it lists')
    if not never_read:
        problems.append(f'{HOOKS_ROW}: no "never" clause; the spine must keep GameScript and lib/lua from upstream')
    return edges


def compare(row, spine, script, name):
    """Problems for each edge one side has and the other lacks."""
    problems = [f'{row}: the spine allows {edge}; {name} does not' for edge in sorted(spine - script)]
    problems += [f'{row}: {name} allows {edge}; the spine does not' for edge in sorted(script - spine)]
    return problems


def spine_problems(text):
    """How the spine's layer table (text: ARCHITECTURE-SPINE.md) differs from TABLE and UPSTREAM_EDGES."""
    lines = text.splitlines()
    if TABLE_HEADER not in lines:
        return [f'no line "{TABLE_HEADER}" in the spine']
    problems = []
    layers = {}
    hooks = None
    start = lines.index(TABLE_HEADER) + 2  # past the header and its separator
    for line in lines[start:]:
        if not line.startswith('|'):
            break
        cells = [cell.strip() for cell in line.strip().strip('|').split('|')]
        if len(cells) != 3:
            problems.append(f'not a three-cell row: {line}')
            continue
        row, lives_in, depends = cells
        if row == ENGINE_ROW:
            continue
        if row == HOOKS_ROW:
            hooks = parse_hooks(lives_in, depends, problems)
            continue
        comp = lives_in.replace('`', '').rstrip('/')
        layers[comp] = spine_terms(row, depends, problems)
        if comp in check_layers.TABLE:
            problems += compare(f'{row} ({comp})', layers[comp], check_layers.TABLE[comp], 'TABLE')
    table = set(check_layers.TABLE)
    problems += [f'the spine has a row for {comp}; TABLE does not' for comp in sorted(set(layers) - table)]
    problems += [f'TABLE has a row for {comp}; the spine does not' for comp in sorted(table - set(layers))]
    if hooks is None:
        return problems + [f'no "{HOOKS_ROW}" row in the spine']
    for path in sorted(set(hooks) | set(check_layers.UPSTREAM_EDGES)):
        problems += compare(f'{HOOKS_ROW} ({path})', hooks.get(path, set()),
                            check_layers.UPSTREAM_EDGES.get(path, set()), 'UPSTREAM_EDGES')
    return problems


class SpineTest(unittest.TestCase):
    """TABLE and UPSTREAM_EDGES say what the spine's layer table says, term for term."""

    def setUp(self):
        self.spine = (HERE.parent / check_layers.SPINE_PATH).read_text()

    def doctored(self, old, new):
        self.assertIn(old, self.spine)
        return spine_problems(self.spine.replace(old, new, 1))

    def assert_problem(self, problems, *parts):
        self.assertTrue(any(all(part in problem for part in parts) for problem in problems),
                        f'no problem naming {parts} in {problems}')

    def test_table_matches_the_spine(self):
        self.assertEqual(spine_problems(self.spine), [])

    def test_tightened_row_fails(self):
        problems = self.doctored('`lib/Memory`, `lib/JsonParser` |', '`lib/Memory` |')
        self.assert_problem(problems, 'Domain', 'lib/JsonParser', 'TABLE allows')

    def test_loosened_row_fails(self):
        problems = self.doctored('`UiListActivity` / `UiAppHost` |', '`UiListActivity` / `UiAppHost`, `GameScript` |')
        self.assert_problem(problems, 'Screens', 'lib/GameScript', 'the spine allows')

    def test_unknown_term_fails(self):
        problems = self.doctored('HAL, `Storage`', 'HAL, Bluetooth, `Storage`')
        self.assert_problem(problems, 'Device adapters', 'unknown spine term "Bluetooth"')

    def test_unknown_hooks_term_fails(self):
        problems = self.doctored('row 9: `lib/GameIcons`', 'row 9: `lib/GameIcons`, Bluetooth')
        self.assert_problem(problems, 'Upstream hooks', 'unknown spine term "Bluetooth"')

    def test_tightened_hook_fails(self):
        problems = self.doctored('`GameCore` (`ForkRelease.h`), ', '')
        self.assert_problem(problems, 'src/network/OtaUpdater.cpp', 'lib/GameCore', 'UPSTREAM_EDGES allows')

    def test_loosened_hook_fails(self):
        problems = self.doctored('row 5: `src/activities/games/`', 'row 5: `src/activities/games/`, `GameCore`')
        self.assert_problem(problems, 'src/activities/ActivityManager.cpp', 'lib/GameCore', 'the spine allows')

    def test_hooks_never_list_must_be_game_script_and_lua(self):
        problems = self.doctored('never `GameScript` or `lib/lua`', 'never `lib/lua`')
        self.assert_problem(problems, 'never allows lib/lua')

    def test_hooks_without_a_never_clause_fails(self):
        problems = self.doctored('; never `GameScript` or `lib/lua`.', '.')
        self.assert_problem(problems, 'Upstream hooks', 'no "never" clause')

    def test_missing_row_fails(self):
        row = next(line for line in self.spine.splitlines() if line.startswith('| Icon data |'))
        self.assert_problem(self.doctored(row + '\n', ''), 'TABLE has a row for lib/GameIcons')

    def test_missing_table_fails(self):
        self.assert_problem(self.doctored(TABLE_HEADER, '| Layer |'), 'no line')


if __name__ == '__main__':
    unittest.main()
