#pragma once

// LOG_ERR / LOG_INF / LOG_DBG captured in fakelog::lines: the one fake log every host suite
// that checks log lines shares (GameSaveStoreTest's own capture folded in, AI-4).

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace fakelog {

inline std::vector<std::string> lines;  // "ERR GAME: text"

__attribute__((format(printf, 3, 4))) inline void add(const char* level, const char* origin, const char* format, ...) {
  char text[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  lines.push_back(std::string(level) + " " + origin + ": " + text);
}

inline bool any(const std::string& part) {
  for (const auto& line : lines)
    if (line.find(part) != std::string::npos) return true;
  return false;
}

}  // namespace fakelog

#define LOG_ERR(origin, format, ...) fakelog::add("ERR", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) fakelog::add("INF", origin, format, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) fakelog::add("DBG", origin, format, ##__VA_ARGS__)
