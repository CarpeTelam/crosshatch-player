#pragma once

// A fake SD card for GameSaveStoreTest: whole files in a map, an operation log, and
// one-shot failures. Like SdFat, rename refuses an existing target and a written
// file holds whatever reached it before a failure.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fakesd {

inline std::map<std::string, std::vector<uint8_t>> files;
inline std::vector<std::string> directories;
inline std::vector<std::string> ops;  // "open-write <path>", "close", "remove <path>", "rename <a> <b>", ...
// When set, the next matching operation fails once.
inline bool failOpenWrite = false;
inline bool failWrite = false;  // the second write of a file writes nothing
inline bool failClose = false;
inline bool failRemove = false;
inline bool failRename = false;
inline bool failOpenRead = false;  // opening an existing file for reading fails
inline bool failRead = false;      // the next read returns -1, as an SD read error does

inline void reset() {
  files.clear();
  directories.clear();
  ops.clear();
  failOpenWrite = failWrite = failClose = failRemove = failRename = false;
  failOpenRead = failRead = false;
}

inline bool take(bool& flag) {
  const bool was = flag;
  flag = false;
  return was;
}

}  // namespace fakesd

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::string path) : path(std::move(path)), open(true) {}

  explicit operator bool() const { return open; }
  size_t fileSize() const { return fakesd::files[path].size(); }
  int read(void* out, const size_t count) {
    if (fakesd::take(fakesd::failRead)) return -1;
    const std::vector<uint8_t>& bytes = fakesd::files[path];
    const size_t n = pos >= bytes.size() ? 0 : std::min(count, bytes.size() - pos);
    if (n > 0) std::memcpy(out, bytes.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  size_t write(const uint8_t* data, const size_t count) {
    ++writes;
    if (writes == 2 && fakesd::take(fakesd::failWrite)) return 0;
    std::vector<uint8_t>& bytes = fakesd::files[path];
    bytes.insert(bytes.end(), data, data + count);
    return count;
  }
  bool close() {
    if (!open) return false;
    open = false;
    fakesd::ops.push_back("close " + path);
    return !fakesd::take(fakesd::failClose);
  }

 private:
  std::string path;
  bool open = false;
  size_t pos = 0;
  int writes = 0;
};

class HalStorage {
 public:
  bool exists(const char* path) {
    const std::string p(path);
    for (const auto& dir : fakesd::directories)
      if (dir == p) return true;
    return fakesd::files.count(p) != 0;
  }
  bool remove(const char* path) {
    fakesd::ops.push_back(std::string("remove ") + path);
    if (fakesd::take(fakesd::failRemove)) return false;
    return fakesd::files.erase(path) != 0;
  }
  bool rename(const char* from, const char* to) {
    fakesd::ops.push_back(std::string("rename ") + from + " " + to);
    if (fakesd::take(fakesd::failRename)) return false;
    if (fakesd::files.count(to) != 0 || fakesd::files.count(from) == 0) return false;
    fakesd::files[to] = fakesd::files[from];
    fakesd::files.erase(from);
    return true;
  }
  bool ensureDirectoryExists(const char* path) {
    if (!exists(path)) fakesd::directories.emplace_back(path);
    return true;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (fakesd::files.count(path) == 0 || fakesd::take(fakesd::failOpenRead)) return false;
    file = HalFile(path);
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    fakesd::ops.push_back(std::string("open-write ") + path);
    if (fakesd::take(fakesd::failOpenWrite)) return false;
    fakesd::files[path].clear();  // O_TRUNC
    file = HalFile(path);
    return true;
  }

  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
};

#define Storage HalStorage::getInstance()
