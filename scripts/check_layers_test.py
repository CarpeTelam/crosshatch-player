#!/usr/bin/env python3
"""
Tests for check_layers.py. Standard library only:

    python3 scripts/check_layers_test.py [-v]

Each case writes a small fixture tree (the game folders and a few upstream headers) to a temp directory, runs the
script on it, and asserts the exit code, so a regression that makes the check always pass is caught. The pre-fix
GameMatchActivity case uses the include lines and names e8420aaa added (retro O3).
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / 'check_layers.py'
sys.path.insert(0, str(HERE))

import check_layers  # noqa: E402

# A tree whose every edge the spine's table allows.
BASE = {
    'lib/GameCore/Manifest.h': '#pragma once\n#include <cstdint>\n#include <Memory.h>\n',
    'lib/GameCore/GameEvent.h': '#pragma once\n#include "Manifest.h"\n',
    'lib/GameIcons/GameIcons.h': '#pragma once\n#include <cstdint>\n',
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
    # Upstream code that includes game code other than lib/GameScript and lib/lua (OtaUpdater's row).
    'src/network/OtaUpdater.cpp': '#include <Manifest.h>\n#include "games/GameVM.h"\n#include <WiFi.h>\n',
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


if __name__ == '__main__':
    unittest.main()
