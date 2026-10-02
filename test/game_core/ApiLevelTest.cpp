#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ApiLevel.h"
#include "ApiLevelList.h"
#include "GameImages.h"
#include "Manifest.h"
#include "PackageLimits.h"
#include "Session.h"

// Checks docs/crosshatch/api-level-<n>.txt against its own grammar and against
// ApiLevel.h: the list is what API_SURFACE_CRC names, so the two change together.
// ApiSurfaceTest (test/game_script) compares the live ch surface with the same entries.

namespace {

using namespace ApiLevelList;

static_assert(API_MIN_LEVEL == 1, "spine AD-19: API_MIN_LEVEL is 1");

TEST(ApiLevelTest, EveryLineFollowsTheGrammar) {
  for (int level = API_MIN_LEVEL; level <= API_LEVEL; ++level) {
    const std::string text = readFile(listPath(level));
    ASSERT_FALSE(text.empty()) << "cannot read " << listPath(level);
    EXPECT_EQ(text.back(), '\n') << listPath(level) << " must end with a newline";
    for (const std::string& line : splitLines(text)) {
      if (!isEntryLine(line)) continue;
      EXPECT_TRUE(parseEntry(line).has_value()) << "not an entry: \"" << line << '"';
    }
  }
}

TEST(ApiLevelTest, GrammarRejectsMalformedEntries) {
  for (const char* bad :
       {"fn ch.gfx.rect", "fn ch.gfx.rect(x,y)", "fn ch.gfx.rect(x, y) ", "fn  ch.log(...)", "field ch.api",
        "field api integer", "enum color", "limit state_bytes 01", "limit state_bytes", "manifest seats.min",
        "seats_max 0", "icon Mark", "icon a_b", "global x", "lib string\r", "ctx mode", "lib string.", "lib a.b.c"}) {
    EXPECT_FALSE(parseEntry(bad).has_value()) << bad;
  }
  // draw names a ch.gfx function and ink or opaque; name a pattern of printable
  // ASCII without spaces.
  const char* const badRules[] = {
      "draw ch.gfx.icon", "draw ch.gfx.icon clear", "draw ch.log ink",  "draw ch.gfx.icon ink opaque",
      "name image",       "name image a b",         "name Image [a-z]", "name image [a-z] "};
  for (const char* bad : badRules) EXPECT_FALSE(parseEntry(bad).has_value()) << bad;
  EXPECT_TRUE(parseEntry("fn ch.gfx.text(x, y, str, size, color, align?)").has_value());
  EXPECT_TRUE(parseEntry("fn ch.text_width(str, size) -> integer").has_value());
  EXPECT_TRUE(parseEntry("lib string.format").has_value());
  EXPECT_EQ(parseEntry("draw ch.gfx.image opaque")->key, "draw ch.gfx.image");
  EXPECT_EQ(parseEntry("name image [a-z0-9_]{1,32}")->key, "name image");
  EXPECT_EQ(parseEntry("lib math.pi")->key, "sym math.pi");
}

TEST(ApiLevelTest, NoTwoEntriesShareAName) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::set<std::string> keys;
  for (const std::string& line : surface.lines) {
    const std::optional<Entry> entry = parseEntry(line);
    ASSERT_TRUE(entry.has_value()) << line;
    EXPECT_TRUE(keys.insert(entry->key).second) << "listed twice: " << entry->key;
  }
  EXPECT_EQ(keys.count("seats_max "), 1u);
}

TEST(ApiLevelTest, SurfaceCrcMatchesTheLists) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  EXPECT_EQ(surface.crc(), static_cast<uint32_t>(API_SURFACE_CRC))
      << "update API_SURFACE_CRC in lib/GameCore/ApiLevel.h to 0x" << std::hex << std::uppercase << std::setw(8)
      << std::setfill('0') << surface.crc();
}

TEST(ApiLevelTest, Crc32IsZlibs) { EXPECT_EQ(crc32("123456789"), 0xCBF43926u); }

TEST(ApiLevelTest, ManifestLimitsMatchTheParser) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::map<std::string, std::string> limits = surface.limits();
  EXPECT_EQ(limits["manifest_id_bytes"], std::to_string(GameCore::Manifest::MAX_ID_BYTES));
  EXPECT_EQ(limits["manifest_name_bytes"], std::to_string(GameCore::Manifest::MAX_NAME_BYTES));
  EXPECT_EQ(limits["manifest_version_bytes"], std::to_string(GameCore::Manifest::MAX_VERSION_BYTES));
  EXPECT_EQ(limits["manifest_icon_bytes"], std::to_string(GameCore::Manifest::MAX_ICON_BYTES));
  EXPECT_EQ(limits["settings_count"], std::to_string(GameCore::Manifest::MAX_SETTINGS));
  EXPECT_EQ(limits["setting_values_count"], std::to_string(GameCore::ManifestSetting::MAX_VALUES));
  EXPECT_EQ(limits["setting_id_bytes"], std::to_string(GameCore::ManifestSetting::MAX_ID_BYTES));
  EXPECT_EQ(limits["setting_name_bytes"], std::to_string(GameCore::ManifestSetting::MAX_NAME_BYTES));
  EXPECT_EQ(limits["setting_value_bytes"], std::to_string(GameCore::ManifestSetting::MAX_VALUE_BYTES));
}

// The package limits the installer enforces and pack_game.py mirrors (PackageLimits.h, GameImages.h) are the
// ones the list gives authors.
TEST(ApiLevelTest, PackageLimitsMatchTheList) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::map<std::string, std::string> limits = surface.limits();
  EXPECT_EQ(limits["package_bytes"], std::to_string(GameCore::PACKAGE_BYTES));
  EXPECT_EQ(limits["package_members_count"], std::to_string(GameCore::PACKAGE_MEMBERS));
  EXPECT_EQ(limits["member_bytes"], std::to_string(GameCore::MEMBER_BYTES));
  EXPECT_EQ(limits["lua_sources_bytes"], std::to_string(GameCore::LUA_SOURCES_BYTES));
  EXPECT_EQ(limits["member_stem_bytes"], std::to_string(GameCore::MEMBER_STEM_BYTES));
  EXPECT_EQ(limits["image_width_pixels"], std::to_string(GameCore::IMAGE_MAX_WIDTH));
  EXPECT_EQ(limits["image_height_pixels"], std::to_string(GameCore::IMAGE_MAX_HEIGHT));
  EXPECT_EQ(limits["images_bytes"], std::to_string(GameCore::IMAGES_BYTES));
  EXPECT_EQ(limits["images_count"], std::to_string(GameCore::MAX_IMAGES));
  EXPECT_EQ(limits["title_image_width_pixels"], std::to_string(GameCore::TITLE_IMAGE_WIDTH));
  EXPECT_EQ(limits["title_image_height_pixels"], std::to_string(GameCore::TITLE_IMAGE_HEIGHT));
  EXPECT_EQ(limits["handoff_image_width_pixels"], std::to_string(GameCore::HANDOFF_IMAGE_WIDTH));
  EXPECT_EQ(limits["handoff_image_height_pixels"], std::to_string(GameCore::HANDOFF_IMAGE_HEIGHT));
}

// The list's manifest keys are the ones the parser reads (GameCore::MANIFEST_KEYS),
// in both directions, each named once.
TEST(ApiLevelTest, ManifestKeysMatchTheParser) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::set<std::string> listed;
  for (const Entry& entry : surface.entries()) {
    if (entry.kind == "manifest") listed.insert(entry.name);
  }
  std::set<std::string> parsed;
  for (const GameCore::ManifestKey& key : GameCore::MANIFEST_KEYS) {
    EXPECT_TRUE(parsed.insert(std::string(key.path)).second) << "MANIFEST_KEYS names twice: " << key.path;
  }
  for (const std::string& name : listed) {
    EXPECT_EQ(parsed.count(name), 1u) << "listed but not in MANIFEST_KEYS: " << name;
  }
  for (const std::string& name : parsed) {
    EXPECT_EQ(listed.count(name), 1u) << "in MANIFEST_KEYS but not listed: " << name;
  }
  EXPECT_FALSE(listed.empty());
}

TEST(ApiLevelTest, SessionLimitsMatchTheList) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::map<std::string, std::string> limits = surface.limits();
  EXPECT_EQ(limits["state_bytes"], std::to_string(GameCore::SNAPSHOT_BYTES));
  EXPECT_EQ(limits["move_bytes"], std::to_string(GameCore::MOVE_BYTES));
  EXPECT_EQ(limits["reject_reason_bytes"], std::to_string(GameCore::REJECT_REASON_BYTES));
}

// scripts/check_api_freeze.py and scripts/fork_release.py read the defines with a
// regular expression; keep them one per line in this shape.
TEST(ApiLevelTest, DefinesStayOnSingleRegularLines) {
  const std::string header = readFile(API_LEVEL_HEADER_PATH);
  ASSERT_FALSE(header.empty()) << "cannot read " << API_LEVEL_HEADER_PATH;
  static const std::regex DEFINE(R"(^#define (API_[A-Z_]+) (0|[1-9][0-9]*|true|false|0x[0-9A-F]{8})$)");
  std::set<std::string> names;
  for (const std::string& line : splitLines(header)) {
    if (line.rfind("#define", 0) != 0) continue;
    std::smatch parts;
    ASSERT_TRUE(std::regex_match(line, parts, DEFINE)) << "irregular define: \"" << line << '"';
    names.insert(parts[1]);
  }
  EXPECT_EQ(names, (std::set<std::string>{"API_LEVEL", "API_MIN_LEVEL", "API_LEVEL_FROZEN", "API_SURFACE_CRC"}));
}

// The manifest_icon entry's pattern, with manifest_icon_bytes as its length cap, is Manifest::parse's icon
// rule: every name built from the grammar's edge cases matches exactly when parse takes it as an icon.
TEST(ApiLevelTest, ManifestIconMatchesTheParser) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::string pattern;
  size_t cap = 0;
  for (const Entry& entry : surface.entries()) {
    if (entry.kind == "name" && entry.name == "manifest_icon") pattern = entry.body.substr(entry.name.size() + 1);
    if (entry.kind == "limit" && entry.name == "manifest_icon_bytes") {
      cap = std::stoul(entry.body.substr(entry.name.size() + 1));
    }
  }
  ASSERT_FALSE(pattern.empty());
  ASSERT_EQ(cap, GameCore::Manifest::MAX_ICON_BYTES);
  const std::regex icon(pattern);

  std::vector<std::string> names = {"",   "-", "a",  "a-",  "-a",  "a--b", "a-b",   "a_b", "_",        "1",       "1a",
                                    "a1", "A", "aB", "a b", "a.b", "a-1",  "a-b-c", "x-",  "\xC3\xA9", "old_name"};
  names.push_back(std::string(cap, 'a'));
  names.push_back(std::string(cap + 1, 'a'));
  names.push_back(std::string(cap - 1, 'a') + "-b");  // cap + 1 bytes
  names.push_back(std::string(cap - 2, 'a') + "-b");  // exactly cap bytes
  for (int byte = 0; byte <= 0xFF; ++byte) {
    const char c = static_cast<char>(byte);
    names.push_back(std::string(1, c));
    names.push_back("a" + std::string(1, c));
    names.push_back("a" + std::string(1, c) + "b");
    names.push_back("a-" + std::string(1, c));
  }
  std::set<std::string> exceptions;
  for (const std::string& name : names) {
    // A quote, a backslash, or a control byte would need an escape in the JSON string below.
    const auto plain = [](const unsigned char c) { return c >= 0x20 && c != '"' && c != '\\'; };
    if (!std::all_of(name.begin(), name.end(), plain)) continue;
    GameCore::Manifest parsed;
    const std::string json = R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, )"
                             R"("modes": ["solo"], "icon": ")" +
                             name + R"("})";
    const bool takes = GameCore::Manifest::parse(json, parsed) == GameCore::ManifestError::None;
    const bool listed = name.size() <= cap && std::regex_match(name, icon);
    if (takes == listed) continue;
    std::string shown;
    for (const unsigned char c : name) {
      char hex[5];
      snprintf(hex, sizeof(hex), "\\x%02X", c);
      shown += (c >= 0x21 && c <= 0x7E) ? std::string(1, static_cast<char>(c)) : std::string(hex);
    }
    exceptions.insert(shown + (listed ? " listed only" : " parsed only"));
  }
  EXPECT_EQ(exceptions, std::set<std::string>{});
}

// The setting_id entry's pattern is Manifest::parse's rule for a setting's id, over the grammar's edge cases and
// every one-byte change of them: a name the pattern matches parses as an id, and any other is BadSettings.
TEST(ApiLevelTest, SettingIdMatchesTheParser) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::string pattern;
  for (const Entry& entry : surface.entries()) {
    if (entry.kind == "name" && entry.name == "setting_id") pattern = entry.body.substr(entry.name.size() + 1);
  }
  ASSERT_FALSE(pattern.empty());
  const std::regex id(pattern);
  const size_t cap = GameCore::ManifestSetting::MAX_ID_BYTES;
  std::vector<std::string> names = {"",   "a",   "a_",  "_a",  "a1",    "1a",       "A",
                                    "aB", "a-b", "a b", "a.b", "level", "ai_level", "\xC3\xA9"};
  names.push_back(std::string(cap, 'a'));
  names.push_back(std::string(cap + 1, 'a'));
  for (int byte = 0x20; byte <= 0x7E; ++byte) {
    const char c = static_cast<char>(byte);
    names.push_back(std::string(1, c));
    names.push_back("a" + std::string(1, c));
  }
  std::set<std::string> exceptions;
  for (const std::string& name : names) {
    if (name.find('"') != std::string::npos || name.find('\\') != std::string::npos) continue;
    const std::string json = R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, )"
                             R"("modes": ["solo"], "settings": [{"id": ")" +
                             name + R"(", "name": "N", "values": ["a", "b"]}]})";
    GameCore::Manifest parsed;
    const bool takes = GameCore::Manifest::parse(json, parsed) == GameCore::ManifestError::None;
    const bool listed = std::regex_match(name, id);
    if (takes != listed) exceptions.insert(name + (listed ? " listed only" : " parsed only"));
  }
  EXPECT_EQ(exceptions, std::set<std::string>{});
}

// The list's `enum icon_weight` values are the ones Manifest::parse accepts, both ways: each listed value parses
// (regular as ICON_REGULAR, fill as ICON_FILL), and any other value is BadIconWeight. scripts/pack_game_test.py
// reads the same lines for the packer.
TEST(ApiLevelTest, IconWeightsMatchTheParser) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::set<std::string> listed;
  for (const Entry& entry : surface.entries()) {
    if (entry.kind == "enum" && entry.body.rfind("icon_weight ", 0) == 0) listed.insert(entry.body.substr(12));
  }
  EXPECT_EQ(listed, (std::set<std::string>{"fill", "regular"}));
  const std::string head =
      R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, "modes": ["solo"], )";
  for (const std::string& weight : listed) {
    GameCore::Manifest m;
    ASSERT_EQ(GameCore::Manifest::parse(head + "\"icon_weight\": \"" + weight + "\"}", m),
              GameCore::ManifestError::None)
        << weight;
    EXPECT_EQ(m.iconWeight, weight == "fill" ? GameCore::Manifest::ICON_FILL : GameCore::Manifest::ICON_REGULAR);
  }
  for (const std::string weight : {"bold", "Regular", "FILL", "", "regular ", "duotone"}) {
    GameCore::Manifest m;
    EXPECT_EQ(GameCore::Manifest::parse(head + "\"icon_weight\": \"" + weight + "\"}", m),
              GameCore::ManifestError::BadIconWeight)
        << weight;
  }
}

}  // namespace
