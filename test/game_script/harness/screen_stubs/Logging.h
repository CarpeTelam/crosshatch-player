#pragma once

// LOG_* into fakelog::lines (the harness's shared capture), made safe for a VM task that
// logs while the test reads: every write and every read here takes one mutex. A log line
// is also a stub call (FakeRtos.h), so a task thread can be held inside ch.log, a locked
// binding. Read the log through the functions below, never fakelog::lines directly, while
// a task may still run.

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../stubs/Logging.h"
#include "FakeRtos.h"

namespace fakelog {

inline std::mutex& guard() {
  static std::mutex* mutex = new std::mutex;
  return *mutex;
}
inline bool anyLine(const std::string& part) {
  std::lock_guard<std::mutex> lock(guard());
  return any(part);
}
inline size_t countLines(const std::string& part) {
  std::lock_guard<std::mutex> lock(guard());
  size_t n = 0;
  for (const auto& line : lines)
    if (line.find(part) != std::string::npos) ++n;
  return n;
}
inline std::vector<std::string> snapshot() {
  std::lock_guard<std::mutex> lock(guard());
  return lines;
}
// Called with each line just logged, after the capture's mutex is released (on the thread that
// logged it, task threads included, so it must be safe to run on either): lets a test act at the
// moment the code under test says something, such as releasing a held task when the match logs
// that it is stopping the VM. Empty by default.
inline std::function<void(const std::string&)>& hook() {
  static auto* function = new std::function<void(const std::string&)>;
  return *function;
}
inline void clearLines() {
  std::lock_guard<std::mutex> lock(guard());
  lines.clear();
}

}  // namespace fakelog

#undef LOG_ERR
#undef LOG_INF
#undef LOG_DBG
#define LOG_LOCKED(level, origin, format, ...)                 \
  do {                                                         \
    fakertos::checkpoint(fakertos::At::Log);                   \
    std::string loggedLine_;                                   \
    {                                                          \
      std::lock_guard<std::mutex> logGuard_(fakelog::guard()); \
      fakelog::add(level, origin, format, ##__VA_ARGS__);      \
      loggedLine_ = fakelog::lines.back();                     \
    }                                                          \
    if (fakelog::hook()) fakelog::hook()(loggedLine_);         \
  } while (0)
#define LOG_ERR(origin, format, ...) LOG_LOCKED("ERR", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) LOG_LOCKED("INF", origin, format, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) LOG_LOCKED("DBG", origin, format, ##__VA_ARGS__)
