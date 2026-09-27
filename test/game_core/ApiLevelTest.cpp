#include <gtest/gtest.h>

#include <cstdint>
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
#include "Manifest.h"

// Checks docs/crosshatch/api-level-<n>.txt against its own grammar and against
// ApiLevel.h: the list is what API_SURFACE_CRC names, so the two change together.
// Story 2.14's surface test compares the live ch table with the same entries.

namespace {

struct Entry {
  std::string kind;
  std::string key;   // what two entries must not share, e.g. "sym ch.gfx.rect", "limit state_bytes"
  std::string body;  // the text after the kind
};

std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream text;
  text << file.rdbuf();
  return file ? text.str() : std::string();
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) lines.push_back(line);
  return lines;
}

bool isEntryLine(const std::string& line) { return !line.empty() && line[0] != '#'; }

// Parses one entry line; nullopt when it breaks the grammar in the list's header.
std::optional<Entry> parseEntry(const std::string& line) {
  static const std::regex LINE(R"(^([a-z_]+) ([ -~]+)$)");
  static const std::string PATH = R"([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)";
  static const std::string PARAM = R"((?:[a-z_][a-z0-9_]*\??|\.\.\.))";
  static const std::map<std::string, std::regex> BODY = {
      {"fn", std::regex("^(" + PATH + R"()\((?:)" + PARAM + "(?:, " + PARAM + R"()*)?\)(?: -> [a-z]+)?$)")},
      {"field", std::regex(R"(^(ch(?:\.[a-z_][a-z0-9_]*)+) [a-z]+$)")},
      {"enum", std::regex(R"(^([a-z_]+ [a-z_]+)$)")},
      {"event", std::regex(R"(^([a-z_]+)(?: [a-z_]+)*$)")},
      {"ctx", std::regex(R"(^([a-z_]+) [a-z]+$)")},
      {"manifest", std::regex(R"(^([a-z_]+(?:\.[a-z_]+)?) [a-z]+\??$)")},
      {"limit", std::regex(R"(^([a-z_]+) (?:0|[1-9][0-9]*)$)")},
      {"lib", std::regex(R"(^([A-Za-z_][A-Za-z0-9_]*)$)")},
      {"icon", std::regex(R"(^([a-z0-9_]{1,32})$)")},
      {"seats_max", std::regex(R"(^()[1-9][0-9]*$)")},
  };
  std::smatch parts;
  if (!std::regex_match(line, parts, LINE)) return std::nullopt;
  const std::string kind = parts[1];
  const std::string body = parts[2];
  const auto rule = BODY.find(kind);
  std::smatch name;
  if (rule == BODY.end() || !std::regex_match(body, name, rule->second)) return std::nullopt;
  // fn, field, and lib share the Lua name space.
  const bool symbol = kind == "fn" || kind == "field" || kind == "lib";
  return Entry{kind, (symbol ? std::string("sym") : kind) + " " + name[1].str(), body};
}

std::string listPath(const int level) {
  return std::string(API_LEVEL_LIST_DIR) + "/api-level-" + std::to_string(level) + ".txt";
}

uint32_t crc32(const std::string& bytes) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const unsigned char byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// The union of the lists from API_MIN_LEVEL to API_LEVEL.
struct Surface {
  bool loaded = true;
  std::vector<std::string> lines;  // entry lines, in level order
};

Surface loadSurface() {
  Surface surface;
  for (int level = API_MIN_LEVEL; level <= API_LEVEL; ++level) {
    const std::string text = readFile(listPath(level));
    if (text.empty()) surface.loaded = false;
    for (const std::string& line : splitLines(text)) {
      if (isEntryLine(line)) surface.lines.push_back(line);
    }
  }
  return surface;
}

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
  for (const char* bad : {"fn ch.gfx.rect", "fn ch.gfx.rect(x,y)", "fn ch.gfx.rect(x, y) ", "fn  ch.log(...)",
                          "field ch.api", "field api integer", "enum color", "limit state_bytes 01",
                          "limit state_bytes", "manifest seats.min", "seats_max 0", "icon Mark", "global x",
                          "lib string\r", "ctx mode"}) {
    EXPECT_FALSE(parseEntry(bad).has_value()) << bad;
  }
  EXPECT_TRUE(parseEntry("fn ch.gfx.text(x, y, str, size, color, align?)").has_value());
  EXPECT_TRUE(parseEntry("fn ch.text_width(str, size) -> integer").has_value());
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
  std::string bytes;
  for (const std::string& line : surface.lines) bytes += line + "\n";
  EXPECT_EQ(crc32(bytes), static_cast<uint32_t>(API_SURFACE_CRC))
      << "update API_SURFACE_CRC in lib/GameCore/ApiLevel.h to 0x" << std::hex << std::uppercase << std::setw(8)
      << std::setfill('0') << crc32(bytes);
}

TEST(ApiLevelTest, Crc32IsZlibs) { EXPECT_EQ(crc32("123456789"), 0xCBF43926u); }

TEST(ApiLevelTest, ManifestLimitsMatchTheParser) {
  const Surface surface = loadSurface();
  ASSERT_TRUE(surface.loaded);
  std::map<std::string, std::string> limits;
  for (const std::string& line : surface.lines) {
    const std::optional<Entry> entry = parseEntry(line);
    if (entry && entry->kind == "limit") {
      const size_t space = entry->body.find(' ');
      limits[entry->body.substr(0, space)] = entry->body.substr(space + 1);
    }
  }
  EXPECT_EQ(limits["manifest_id_bytes"], std::to_string(GameCore::Manifest::MAX_ID_BYTES));
  EXPECT_EQ(limits["manifest_name_bytes"], std::to_string(GameCore::Manifest::MAX_NAME_BYTES));
  EXPECT_EQ(limits["manifest_version_bytes"], std::to_string(GameCore::Manifest::MAX_VERSION_BYTES));
  EXPECT_EQ(limits["manifest_icon_bytes"], std::to_string(GameCore::Manifest::MAX_ICON_BYTES));
}

// Story 2.5's freeze job and the release script read the defines with a regular
// expression; keep them one per line in this shape.
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

}  // namespace
