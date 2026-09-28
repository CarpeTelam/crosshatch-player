#pragma once

// Reads docs/crosshatch/api-level-<n>.txt for the host tests: the grammar in the
// list's header, the union of the levels from API_MIN_LEVEL to API_LEVEL, and the
// CRC that ApiLevel.h's API_SURFACE_CRC must equal. Shared by ApiLevelTest (the
// list and the header) and ApiSurfaceTest (the list and the live ch surface).
// Needs API_LEVEL_LIST_DIR, the directory holding the lists.

#include <ApiLevel.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace ApiLevelList {

struct Entry {
  std::string kind;
  std::string key;   // what two entries must not share, e.g. "sym ch.gfx.rect", "limit state_bytes"
  std::string name;  // the key without its space, e.g. "ch.gfx.rect", "state_bytes"
  std::string body;  // the text after the kind
};

inline std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream text;
  text << file.rdbuf();
  return file ? text.str() : std::string();
}

inline std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) lines.push_back(line);
  return lines;
}

inline bool isEntryLine(const std::string& line) { return !line.empty() && line[0] != '#'; }

// Parses one entry line; nullopt when it breaks the grammar in the list's header.
inline std::optional<Entry> parseEntry(const std::string& line) {
  static const std::regex LINE(R"(^([a-z_]+) ([ -~]+)$)");
  static const std::string NAME = R"([A-Za-z_][A-Za-z0-9_]*)";
  static const std::string PATH = NAME + R"((?:\.)" + NAME + ")*";
  static const std::string PARAM = R"((?:[a-z_][a-z0-9_]*\??|\.\.\.))";
  static const std::map<std::string, std::regex> BODY = {
      {"fn", std::regex("^(" + PATH + R"()\((?:)" + PARAM + "(?:, " + PARAM + R"()*)?\)(?: -> [a-z]+)?$)")},
      {"field", std::regex(R"(^(ch(?:\.[a-z_][a-z0-9_]*)+) [a-z]+$)")},
      {"enum", std::regex(R"(^([a-z_]+ [a-z_]+)$)")},
      {"event", std::regex(R"(^([a-z_]+)(?: [a-z_]+)*$)")},
      {"ctx", std::regex(R"(^([a-z_]+) [a-z]+$)")},
      {"manifest", std::regex(R"(^([a-z_]+(?:\.[a-z_]+)?) [a-z]+\??$)")},
      {"limit", std::regex(R"(^([a-z_]+) (?:0|[1-9][0-9]*)$)")},
      {"lib", std::regex("^(" + NAME + R"((?:\.)" + NAME + ")?)$")},
      {"icon", std::regex(R"(^([a-z][a-z0-9-]{0,31})$)")},
      {"draw", std::regex(R"(^(ch\.gfx\.[a-z_]+) (ink|opaque)$)")},
      {"name", std::regex(R"(^([a-z_]+) ([!-~]+)$)")},
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
  return Entry{kind, (symbol ? std::string("sym") : kind) + " " + name[1].str(), name[1].str(), body};
}

inline std::string listPath(const int level) {
  return std::string(API_LEVEL_LIST_DIR) + "/api-level-" + std::to_string(level) + ".txt";
}

inline uint32_t crc32(const std::string& bytes) {
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

  // The CRC API_SURFACE_CRC must equal.
  uint32_t crc() const {
    std::string bytes;
    for (const std::string& line : lines) bytes += line + "\n";
    return crc32(bytes);
  }

  // Every entry that parses, in order (a line that does not is left out; the
  // grammar test reports it).
  std::vector<Entry> entries() const {
    std::vector<Entry> parsed;
    for (const std::string& line : lines) {
      if (std::optional<Entry> entry = parseEntry(line)) parsed.push_back(*entry);
    }
    return parsed;
  }

  // The limits by name.
  std::map<std::string, std::string> limits() const {
    std::map<std::string, std::string> byName;
    for (const Entry& entry : entries()) {
      if (entry.kind == "limit") byName[entry.name] = entry.body.substr(entry.body.find(' ') + 1);
    }
    return byName;
  }
};

inline Surface loadSurface() {
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

}  // namespace ApiLevelList
