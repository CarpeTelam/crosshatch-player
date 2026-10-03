#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "GameHostCaps.h"
#include "Manifest.h"

using GameCore::Manifest;
using GameCore::ManifestError;
using GameCore::ManifestReader;

namespace {

constexpr const char* FULL = R"({
  "id": "tic-tac-toe",
  "name": "Tic-Tac-Toe",
  "version": "1.0.0",
  "api": 1,
  "seats": { "min": 2, "max": 2 },
  "modes": ["pass", "nearby"],
  "hidden": true,
  "icon": "game-controller",
  "icon_weight": "fill"
})";

// A minimal valid manifest with one extra member spliced in before the closing brace.
std::string withExtra(const std::string& member) {
  return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, "modes": ["solo"])" +
         (member.empty() ? std::string() : ", " + member) + "}";
}

ManifestError parse(const std::string& json, Manifest& out) { return Manifest::parse(json, out); }

ManifestError parse(const std::string& json) {
  Manifest out;
  return parse(json, out);
}

TEST(ManifestTest, ParsesEveryKey) {
  Manifest m;
  ASSERT_EQ(parse(FULL, m), ManifestError::None);
  EXPECT_STREQ(m.id, "tic-tac-toe");
  EXPECT_STREQ(m.name, "Tic-Tac-Toe");
  EXPECT_STREQ(m.version, "1.0.0");
  EXPECT_EQ(m.api, 1);
  EXPECT_EQ(m.seatsMin, 2);
  EXPECT_EQ(m.seatsMax, 2);
  EXPECT_FALSE(m.hasMode(Manifest::MODE_SOLO));
  EXPECT_TRUE(m.hasMode(Manifest::MODE_PASS));
  EXPECT_TRUE(m.hasMode(Manifest::MODE_NEARBY));
  EXPECT_TRUE(m.hidden);
  EXPECT_STREQ(m.icon, "game-controller");
  EXPECT_EQ(m.iconWeight, Manifest::ICON_FILL);
}

TEST(ManifestTest, OptionalKeysDefault) {
  Manifest m;
  ASSERT_EQ(parse(withExtra(""), m), ManifestError::None);
  EXPECT_FALSE(m.hidden);
  EXPECT_STREQ(m.icon, "");
  EXPECT_EQ(m.iconWeight, Manifest::ICON_REGULAR);
  EXPECT_STREQ(m.version, "");
  EXPECT_TRUE(m.hasMode(Manifest::MODE_SOLO));
}

TEST(ManifestTest, IgnoresUnknownKeysOfAnyType) {
  Manifest m;
  const std::string json = withExtra(
      R"("author": "x", "year": 2026, "beta": false, "none": null, "extra": {"id": "zzz", "api": [1, {"a": 2}]},)"
      R"( "tags": ["a", ["b"], {"c": null}])");
  ASSERT_EQ(parse(json, m), ManifestError::None);
  EXPECT_STREQ(m.id, "g");
  EXPECT_EQ(m.api, 1);
  // Unknown keys inside seats are ignored too.
  EXPECT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1,)"
                  R"( "seats": {"min": 1, "note": {"x": [1]}, "max": 2, "why": "x"}, "modes": ["solo"]})",
                  m),
            ManifestError::None);
  EXPECT_EQ(m.seatsMax, 2);
}

TEST(ManifestTest, IgnoresUnknownStringsLongerThanTheParserBuffer) {
  // The JSON parser drops string tokens over 511 bytes without a callback.
  const std::string longText(600, 'd');
  EXPECT_EQ(parse(withExtra(R"("description": ")" + longText + R"(", "x": 1)")), ManifestError::None);
  EXPECT_EQ(parse(withExtra(R"("description": ")" + longText + R"(")")), ManifestError::None);  // last member
  EXPECT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "note": ")" + longText +
                  R"(", "max": 1}, "modes": ["solo"]})"),
            ManifestError::None);
  EXPECT_NE(parse(withExtra(R"("icon": ")" + longText + R"(")")), ManifestError::None);  // a known key still fails
}

TEST(ManifestTest, IgnoresUnknownKeysLongerThanTheParserBuffer) {
  // The JSON parser drops a key over 511 bytes, so its value arrives with no key.
  const std::string longKey(600, 'k');
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": "v", "x": 1)")), ManifestError::None);
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": 5)")), ManifestError::None);
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": {"a": [1, 2]})")), ManifestError::None);
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": null, "hidden": true)")), ManifestError::None);
  Manifest m;
  ASSERT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, ")" + longKey +
                      R"(": 3, "max": 2}, "modes": ["solo"]})",
                  m),
            ManifestError::None);
  EXPECT_EQ(m.seatsMax, 2);
  EXPECT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, ")" + longKey +
                  R"(": {"q": 1}}, "modes": ["solo"]})"),
            ManifestError::BadSeats);  // still needs max
}

TEST(ManifestTest, ReadsInChunks) {
  auto reader = std::make_unique<ManifestReader>();
  const std::string json = FULL;
  reader->begin();
  for (size_t i = 0; i < json.size(); i += 3) {
    reader->feed(json.data() + i, std::min<size_t>(3, json.size() - i));
  }
  Manifest m;
  ASSERT_EQ(reader->finish(m), ManifestError::None);
  EXPECT_STREQ(m.icon, "game-controller");
  // The same reader is reusable after begin().
  reader->begin();
  reader->feed("[]", 2);
  EXPECT_EQ(reader->finish(m), ManifestError::Syntax);
}

TEST(ManifestTest, RejectsBadIds) {
  for (const char* id : {"", "-abc", "Abc", "a_b", "a.b", "a b", "abcdefghijklmnopqrstuvwxyz0123456"}) {
    const std::string json =
        R"({"id": ")" + std::string(id) +
        R"(", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, "modes": ["solo"]})";
    EXPECT_EQ(parse(json), ManifestError::BadId) << id;
  }
  const std::string ok = R"({"id": "0abcdefghijklmnopqrstuvwxyz-123", "name": "G", "version": "", "api": 1,)"
                         R"( "seats": {"min": 1, "max": 1}, "modes": ["solo"]})";
  EXPECT_EQ(parse(ok), ManifestError::None);  // 32 characters, digit first, dash inside
}

TEST(ManifestTest, RejectsMissingRequiredKeys) {
  for (const char* key : {"id", "name", "version", "api", "seats", "modes"}) {
    std::string json = FULL;
    const size_t at = json.find(std::string("\"") + key + "\"");
    ASSERT_NE(at, std::string::npos);
    json.replace(at + 1, 1, "x");  // renames the key, which is then ignored
    EXPECT_EQ(parse(json), ManifestError::MissingKey) << key;
  }
}

TEST(ManifestTest, RejectsWrongTypes) {
  EXPECT_EQ(parse(R"({"id": 5})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"name": ["G"]})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"api": "1"})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"seats": [1, 2]})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"seats": {"min": "1", "max": 1}})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"modes": "solo"})"), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("hidden": "yes")")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("icon": null)")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("icon_weight": null)")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("icon_weight": 1)")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("icon_weight": ["fill"])")), ManifestError::WrongType);
}

TEST(ManifestTest, RejectsBadApi) {
  for (const char* api : {"0", "-1", "1.5", "1e2", "01", "1234567890"}) {
    std::string json = withExtra("");
    json.replace(json.find("\"api\": 1"), 8, std::string("\"api\": ") + api);
    EXPECT_EQ(parse(json), ManifestError::BadApi) << api;
  }
}

TEST(ManifestTest, RejectsBadSeats) {
  const auto withSeats = [](const std::string& seats) {
    return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": )" + seats + R"(, "modes": ["solo"]})";
  };
  EXPECT_EQ(parse(withSeats(R"({"min": 2, "max": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": 0, "max": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": -1, "max": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": 1, "max": 1, "max": 2})")), ManifestError::DuplicateKey);
  EXPECT_EQ(parse(withSeats(R"({"min": 1, "max": 8})")), ManifestError::None);
}

TEST(ManifestTest, RejectsBadModes) {
  const auto withModes = [](const std::string& modes) {
    return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, "modes": )" + modes +
           "}";
  };
  EXPECT_EQ(parse(withModes("[]")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", "online"])")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", 1])")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", ["pass"]])")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", "solo"])")), ManifestError::None);
}

TEST(ManifestTest, RejectsBadTextFields) {
  EXPECT_EQ(parse(R"({"name": ""})"), ManifestError::BadName);
  EXPECT_EQ(parse(R"({"name": ")" + std::string(Manifest::MAX_NAME_BYTES + 1, 'n') + R"("})"), ManifestError::BadName);
  std::string longest = withExtra("");
  longest.replace(longest.find("\"G\""), 3, "\"" + std::string(Manifest::MAX_NAME_BYTES, 'n') + "\"");
  Manifest m;
  ASSERT_EQ(parse(longest, m), ManifestError::None);
  EXPECT_EQ(std::string(m.name).size(), Manifest::MAX_NAME_BYTES);
  EXPECT_EQ(parse(R"({"version": ")" + std::string(Manifest::MAX_VERSION_BYTES + 1, 'v') + R"("})"),
            ManifestError::BadVersion);
  EXPECT_EQ(parse(withExtra(R"("icon": "Mark")")), ManifestError::BadIcon);
  EXPECT_EQ(parse(withExtra(R"("icon": "")")), ManifestError::BadIcon);
  EXPECT_EQ(parse(withExtra(R"("icon": ")" + std::string(Manifest::MAX_ICON_BYTES + 1, 'i') + R"(")")),
            ManifestError::BadIcon);
}

// The icon grammar is the library's, [a-z][a-z0-9]*(-[a-z0-9]+)* in at most 32 bytes (spine AD-15).
// scripts/pack_game_test.py's test_icon_grammar holds the same accept and reject cases (plus the
// non-string values only JSON typing decides), and ApiLevelTest.ManifestIconMatchesTheParser ties the
// API list's pattern to this parser.
TEST(ManifestTest, AcceptsLibraryIconNames) {
  const std::string longest = std::string(Manifest::MAX_ICON_BYTES, 'x');
  for (const std::string& name : {std::string("x"), std::string("a1"), std::string("game-controller"),
                                  std::string("a-1"), std::string("a1-b2-c3"), longest}) {
    Manifest m;
    ASSERT_EQ(parse(withExtra("\"icon\": \"" + name + "\""), m), ManifestError::None) << name;
    EXPECT_EQ(std::string(m.icon), name);
  }
}

TEST(ManifestTest, RejectsIconNamesOutsideTheGrammar) {
  const std::string tooLong = std::string(Manifest::MAX_ICON_BYTES + 1, 'x');
  for (const std::string& name : {std::string("old_name"), std::string("a_b"), std::string("_a"), std::string("a--b"),
                                  std::string("-a"), std::string("a-"), std::string("1a"), std::string("a b"),
                                  std::string("a.b"), std::string("Foo"), std::string("\xC3\xA9"), tooLong}) {
    EXPECT_EQ(parse(withExtra("\"icon\": \"" + name + "\"")), ManifestError::BadIcon) << name;
  }
}

TEST(ManifestTest, ParsesIconWeight) {
  Manifest m;
  ASSERT_EQ(parse(withExtra(R"("icon_weight": "regular")"), m), ManifestError::None);
  EXPECT_EQ(m.iconWeight, Manifest::ICON_REGULAR);
  ASSERT_EQ(parse(withExtra(R"("icon_weight": "fill")"), m), ManifestError::None);
  EXPECT_EQ(m.iconWeight, Manifest::ICON_FILL);
  // The weight needs no icon: the key is read on its own.
  EXPECT_STREQ(m.icon, "");
}

TEST(ManifestTest, RejectsMalformedIconWeight) {
  for (const char* weight : {"bold", "Regular", "FILL", "", "thin", "fill ", "fill-regular"}) {
    EXPECT_EQ(parse(withExtra(std::string(R"("icon_weight": ")") + weight + "\"")), ManifestError::BadIconWeight)
        << weight;
  }
  EXPECT_STREQ(GameCore::describe(ManifestError::BadIconWeight), "invalid icon_weight");
}

TEST(ManifestTest, RejectsDuplicateKnownKeys) {
  EXPECT_EQ(parse(withExtra(R"("id": "h")")), ManifestError::DuplicateKey);
  EXPECT_EQ(parse(withExtra(R"("hidden": true, "hidden": false)")), ManifestError::DuplicateKey);
  EXPECT_EQ(parse(withExtra(R"("icon_weight": "fill", "icon_weight": "fill")")), ManifestError::DuplicateKey);
  EXPECT_EQ(parse(withExtra(R"("x": 1, "x": 2)")), ManifestError::None);  // unknown keys may repeat
}

TEST(ManifestTest, RejectsMalformedJson) {
  const std::string ok = withExtra("");
  EXPECT_EQ(parse(""), ManifestError::Syntax);
  EXPECT_EQ(parse("[]"), ManifestError::Syntax);
  EXPECT_EQ(parse("\"id\""), ManifestError::Syntax);
  EXPECT_EQ(parse("42"), ManifestError::Syntax);
  EXPECT_EQ(parse(ok.substr(0, ok.size() - 1)), ManifestError::Syntax);  // truncated
  EXPECT_EQ(parse(ok + "{}"), ManifestError::Syntax);                    // a second value
  EXPECT_EQ(parse(R"({"id" "g"})"), ManifestError::Syntax);              // key without a value
  EXPECT_EQ(parse(R"({"id": })"), ManifestError::Syntax);
  EXPECT_EQ(parse(R"({"extra": [1})"), ManifestError::Syntax);  // mismatched brackets
  EXPECT_EQ(parse(R"({"hidden": tru})"), ManifestError::Syntax);
}

// ---- default_mode and settings (AD-15, as amended 2026-10-02) ----

// A minimal valid manifest for `modes` (seats 1 to 2) with `member` spliced in.
std::string withModes(const std::string& modes, const std::string& member) {
  return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 2}, "modes": )" + modes +
         (member.empty() ? std::string() : ", " + member) + "}";
}

// One settings object from its members, e.g. setting(R"("id": "a")").
std::string setting(const std::string& members) { return "{" + members + "}"; }

// A valid setting with id `id` and values `values` (a JSON array).
std::string settingWith(const std::string& id, const std::string& values = R"(["Easy", "Hard"])") {
  return setting(R"("id": ")" + id + R"(", "name": "Level", "values": )" + values);
}

ManifestError parseSettings(const std::string& settings) { return parse(withExtra("\"settings\": " + settings)); }

TEST(ManifestTest, DefaultModeIsOneOfTheModesInEitherKeyOrder) {
  Manifest m;
  ASSERT_EQ(parse(withModes(R"(["solo", "pass"])", R"("default_mode": "pass")"), m), ManifestError::None);
  EXPECT_EQ(m.defaultMode, Manifest::MODE_PASS);
  // Before modes: checked in finish(), so key order is free.
  ASSERT_EQ(parse(R"({"default_mode": "solo", "id": "g", "name": "G", "version": "", "api": 1,)"
                  R"( "seats": {"min": 1, "max": 2}, "modes": ["pass", "solo"]})",
                  m),
            ManifestError::None);
  EXPECT_EQ(m.defaultMode, Manifest::MODE_SOLO);
  ASSERT_EQ(parse(withExtra(""), m), ManifestError::None);
  EXPECT_EQ(m.defaultMode, 0) << "absent: none";
}

TEST(ManifestTest, RejectsADefaultModeOutsideTheModes) {
  EXPECT_EQ(parse(withModes(R"(["solo"])", R"("default_mode": "pass")")), ManifestError::BadDefaultMode);
  EXPECT_EQ(parse(R"({"default_mode": "nearby", "id": "g", "name": "G", "version": "", "api": 1,)"
                  R"( "seats": {"min": 1, "max": 2}, "modes": ["solo", "pass"]})"),
            ManifestError::BadDefaultMode);
  for (const char* bad : {"", "Solo", "duo", "solo ", "online"}) {
    EXPECT_EQ(parse(withModes(R"(["solo"])", std::string(R"("default_mode": ")") + bad + "\"")),
              ManifestError::BadDefaultMode)
        << bad;
  }
  EXPECT_EQ(parse(withExtra(R"("default_mode": 1)")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("default_mode": ["solo"])")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("default_mode": null)")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("default_mode": "solo", "default_mode": "solo")")), ManifestError::DuplicateKey);
  EXPECT_STREQ(GameCore::describe(ManifestError::BadDefaultMode), "invalid default_mode");
}

TEST(ManifestTest, ParsesSettingsInOrderWithTheirDefaults) {
  auto reader = std::make_unique<ManifestReader>();
  const std::string json =
      withExtra(R"("settings": [)"
                R"({"id": "level", "name": "AI level", "values": ["Easy", "Normal", "Hard"], "default": "Hard"},)"
                R"({"default": "Off", "values": ["On", "Off"], "name": "Sound", "id": "sound_2"},)"
                R"({"id": "board", "name": "Board", "values": ["Small", "Large"]}])");
  reader->begin();
  reader->feed(json.data(), json.size());
  Manifest m;
  ASSERT_EQ(reader->finish(m), ManifestError::None);
  EXPECT_EQ(m.settingsCount, 3);
  const GameCore::ManifestSettings& settings = reader->settings();
  ASSERT_EQ(settings.count, 3);
  EXPECT_STREQ(settings.settings[0].id, "level");
  EXPECT_STREQ(settings.settings[0].name, "AI level");
  ASSERT_EQ(settings.settings[0].count, 3);
  EXPECT_STREQ(settings.settings[0].values[1], "Normal");
  EXPECT_EQ(settings.settings[0].defaultIndex, 2);
  // Key order inside a setting is free: the default may come before its values.
  EXPECT_STREQ(settings.settings[1].id, "sound_2");
  EXPECT_EQ(settings.settings[1].defaultIndex, 1);
  EXPECT_EQ(settings.settings[2].defaultIndex, 0) << "absent: the first value";
  EXPECT_EQ(settings.indexOf("sound_2"), 1);
  EXPECT_EQ(settings.indexOf("sound"), -1);
  EXPECT_EQ(settings.settings[0].indexOf("Hard"), 2);
  EXPECT_EQ(settings.settings[0].indexOf("hard"), -1);

  // Each setting's value at an index, the default for one out of range.
  const uint8_t chosen[GameCore::ManifestSettings::MAX_SETTINGS] = {1, 9, 1, 0};
  const GameCore::SettingValues values = settings.valuesAt(chosen);
  ASSERT_EQ(values.count, 3);
  EXPECT_STREQ(values.entries[0].id, "level");
  EXPECT_STREQ(values.entries[0].value, "Normal");
  EXPECT_STREQ(values.entries[1].value, "Off");
  EXPECT_STREQ(values.entries[2].value, "Large");

  // A reused reader starts with no settings.
  reader->begin();
  const std::string plain = withExtra("");
  reader->feed(plain.data(), plain.size());
  ASSERT_EQ(reader->finish(m), ManifestError::None);
  EXPECT_EQ(m.settingsCount, 0);
  EXPECT_EQ(reader->settings().count, 0);
}

TEST(ManifestTest, SettingsLimitsHoldOnBothSides) {
  const std::string four =
      "[" + settingWith("a") + ", " + settingWith("b") + ", " + settingWith("c") + ", " + settingWith("d") + "]";
  Manifest m;
  ASSERT_EQ(parse(withExtra("\"settings\": " + four), m), ManifestError::None);
  EXPECT_EQ(m.settingsCount, 4);
  EXPECT_EQ(parseSettings(four.substr(0, four.size() - 1) + ", " + settingWith("e") + "]"), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[]"), ManifestError::None);

  // Values: 2 to 6.
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["1", "2"])") + "]"), ManifestError::None);
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["1", "2", "3", "4", "5", "6"])") + "]"), ManifestError::None);
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["1"])") + "]"), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"([])") + "]"), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["1", "2", "3", "4", "5", "6", "7"])") + "]"),
            ManifestError::BadSettings);

  // A value: 1 to 16 bytes.
  const std::string v16(GameCore::ManifestSetting::MAX_VALUE_BYTES, 'v');
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["x", ")" + v16 + R"("])") + "]"), ManifestError::None);
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["x", ")" + v16 + R"(v"])") + "]"), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["x", ""])") + "]"), ManifestError::BadSettings);

  // An id: 1 to 16 bytes of [a-z][a-z0-9_]*.
  const std::string id16(GameCore::ManifestSetting::MAX_ID_BYTES, 'i');
  EXPECT_EQ(parseSettings("[" + settingWith(id16) + "]"), ManifestError::None);
  EXPECT_EQ(parseSettings("[" + settingWith(id16 + "i") + "]"), ManifestError::BadSettings);
  for (const char* id : {"", "1a", "_a", "A", "a-b", "a b"}) {
    EXPECT_EQ(parseSettings("[" + settingWith(id) + "]"), ManifestError::BadSettings) << id;
  }

  // A name: 1 to 24 bytes.
  const std::string n24(GameCore::ManifestSetting::MAX_NAME_BYTES, 'n');
  const auto named = [](const std::string& name) {
    return "[" + setting(R"("id": "a", "name": ")" + name + R"(", "values": ["1", "2"])") + "]";
  };
  EXPECT_EQ(parseSettings(named(n24)), ManifestError::None);
  EXPECT_EQ(parseSettings(named(n24 + "n")), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings(named("")), ManifestError::BadSettings);
}

TEST(ManifestTest, RejectsSettingsThatBreakTheirRules) {
  // A duplicate id, or a duplicate value.
  EXPECT_EQ(parseSettings("[" + settingWith("a") + ", " + settingWith("a") + "]"), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + settingWith("a", R"(["x", "y", "x"])") + "]"), ManifestError::BadSettings);
  // A default not in the values, before or after them.
  EXPECT_EQ(parseSettings("[" + setting(R"("id": "a", "name": "A", "values": ["x", "y"], "default": "z")") + "]"),
            ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + setting(R"("default": "z", "id": "a", "name": "A", "values": ["x", "y"])") + "]"),
            ManifestError::BadSettings);
  // A missing id, name, or values.
  EXPECT_EQ(parseSettings("[" + setting(R"("name": "A", "values": ["x", "y"])") + "]"), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + setting(R"("id": "a", "values": ["x", "y"])") + "]"), ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + setting(R"("id": "a", "name": "A")") + "]"), ManifestError::BadSettings);
  // An unknown key inside a setting, unlike one at the top level.
  EXPECT_EQ(parseSettings("[" + setting(R"("id": "a", "name": "A", "values": ["x", "y"], "note": "n")") + "]"),
            ManifestError::BadSettings);
  EXPECT_EQ(parseSettings("[" + setting(R"("id": "a", "name": "A", "values": ["x", "y"], "x": {"y": 1})") + "]"),
            ManifestError::BadSettings);
  // A key the JSON parser drops for length is unknown too.
  EXPECT_EQ(parseSettings("[" +
                          setting(R"(")" + std::string(600, 'k') +
                                  R"(": "v", "id": "a", "name": "A",)"
                                  R"( "values": ["x", "y"])") +
                          "]"),
            ManifestError::BadSettings);
  // A key twice in one setting.
  EXPECT_EQ(parseSettings("[" + setting(R"("id": "a", "id": "b", "name": "A", "values": ["x", "y"])") + "]"),
            ManifestError::DuplicateKey);
  // Wrong shapes: settings not an array, a setting not an object, a field not a string, values not strings.
  EXPECT_EQ(parseSettings("{}"), ManifestError::WrongType);
  EXPECT_EQ(parseSettings(R"("level")"), ManifestError::WrongType);
  for (const char* bad : {R"(["a"])", "[1]", "[null]", "[true]", "[[]]"}) {
    EXPECT_EQ(parseSettings(bad), ManifestError::BadSettings) << bad;
  }
  for (const std::string& members : {std::string(R"("id": 1, "name": "A", "values": ["x", "y"])"),
                                     std::string(R"("id": "a", "name": null, "values": ["x", "y"])"),
                                     std::string(R"("id": "a", "name": "A", "values": "x")"),
                                     std::string(R"("id": "a", "name": "A", "values": {"x": "y"})"),
                                     std::string(R"("id": "a", "name": "A", "values": ["x", 2])"),
                                     std::string(R"("id": "a", "name": "A", "values": ["x", ["y"]])"),
                                     std::string(R"("id": "a", "name": "A", "values": ["x", "y"], "default": true)"),
                                     std::string(R"("id": ["a"], "name": "A", "values": ["x", "y"])")}) {
    EXPECT_EQ(parseSettings("[" + setting(members) + "]"), ManifestError::BadSettings) << members;
  }
  EXPECT_EQ(parse(withExtra(R"("settings": [], "settings": [])")), ManifestError::DuplicateKey);
  EXPECT_STREQ(GameCore::describe(ManifestError::BadSettings), "invalid settings");
}

// The current mode (Design Notes): remembered if the host can start it, else default_mode if it can, else the first
// startable of solo, pass, and nearby.
TEST(ManifestTest, StartModeFallsBackRememberedThenDefaultThenTheFirstStartable) {
  constexpr uint8_t SOLO = Manifest::MODE_SOLO;
  constexpr uint8_t PASS = Manifest::MODE_PASS;
  constexpr uint8_t NEARBY = Manifest::MODE_NEARBY;
  Manifest m;
  m.modes = SOLO | PASS | NEARBY;
  EXPECT_EQ(m.startMode(0, SOLO | PASS), SOLO) << "no default, solo listed: solo";
  EXPECT_EQ(m.startMode(PASS, SOLO | PASS), PASS) << "remembered wins";
  EXPECT_EQ(m.startMode(NEARBY, SOLO | PASS), SOLO) << "remembered not startable here";
  m.defaultMode = PASS;
  EXPECT_EQ(m.startMode(0, SOLO | PASS), PASS) << "the default";
  EXPECT_EQ(m.startMode(SOLO, SOLO | PASS), SOLO) << "remembered over the default";
  EXPECT_EQ(m.startMode(0, SOLO), SOLO) << "a default the host cannot start";
  EXPECT_EQ(m.startMode(0, PASS | NEARBY), PASS);
  m.defaultMode = NEARBY;
  EXPECT_EQ(m.startMode(0, PASS), PASS) << "pass-only on this host";
  // A remembered byte that is not one mode bit is stale.
  EXPECT_EQ(m.startMode(SOLO | PASS, SOLO | PASS), SOLO);
  EXPECT_EQ(m.startMode(0x08, SOLO | PASS), SOLO);
  EXPECT_EQ(m.startMode(0xFF, NEARBY), NEARBY);
  EXPECT_EQ(m.startMode(SOLO, 0), 0) << "nothing startable";
}

// Every fixture game's manifest.json passes what GameRegistry requires to list a game,
// and what GamesLauncherActivity requires to start it: it parses, its id is the folder's
// name, it passes Manifest::check against this host, and it offers solo (pass-hidden, the hidden hand-off, which
// has no solo form, offers pass instead, and says hidden). Exactly the folders that
// are not games of their own (changed, which holds only game folders one level down, and faults, modules, surface) have
// none, so a game fixture that loses its manifest fails here.
TEST(ManifestTest, EveryFixtureManifestIsListed) {
  std::set<std::string> listed;
  std::set<std::string> withoutManifest;
  for (const auto& entry : std::filesystem::directory_iterator(GAME_SCRIPT_FIXTURES_DIR)) {
    if (!entry.is_directory()) continue;
    const std::string folder = entry.path().filename().string();
    const std::filesystem::path manifestPath = entry.path() / "manifest.json";
    if (!std::filesystem::exists(manifestPath)) {
      withoutManifest.insert(folder);
      continue;
    }
    std::ifstream file(manifestPath, std::ios::binary);
    ASSERT_TRUE(file) << manifestPath;
    std::ostringstream json;
    json << file.rdbuf();
    Manifest m;
    const ManifestError error = parse(json.str(), m);
    if (error != ManifestError::None) {
      ADD_FAILURE() << folder << ": " << GameCore::describe(error);
      continue;  // m is unspecified after a failed parse
    }
    EXPECT_EQ(std::string(m.id), folder);
    const GameCore::CheckResult check = m.check(gameHostCaps());
    EXPECT_TRUE(check.ok()) << folder << ": " << GameCore::describe(check.reason);
    if (folder == "pass-hidden") {
      EXPECT_EQ(check.modes, Manifest::MODE_PASS) << folder << " offers more than pass, or not pass, on this host";
      EXPECT_TRUE(m.hidden) << folder << " does not say hidden";
    } else {
      EXPECT_NE(check.modes & Manifest::MODE_SOLO, 0) << folder << " offers no solo mode on this host";
    }
    listed.insert(folder);
  }
  EXPECT_EQ(withoutManifest, (std::set<std::string>{"changed", "faults", "modules", "surface"}));
  EXPECT_TRUE(listed.count("slow-restart")) << "slow-restart's manifest was not found";
  EXPECT_TRUE(listed.count("timing")) << "timing's manifest was not found";
  EXPECT_TRUE(listed.count("tracer")) << "tracer's manifest was not found";
}

}  // namespace
