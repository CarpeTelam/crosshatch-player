#pragma once

// A fake SD card for the src/games harness, grown from save_store_stubs/HalStorage.h:
// files and folders in creation order (so a folder lists as a FAT one does), the
// calls the game code makes (open, read, write, rename, remove, mkdir, folder
// listing), and a failure injected per path. Like SdFat, rename refuses an existing
// target, a written file holds whatever reached it before a failure, and getName
// returns 0 for a name that does not fit (fakesd::sim.getNameCuts switches to the
// simulator's, which cuts the name instead).
//
// Failures stay set until fakesd::reset(); each is keyed by the exact path the code
// passes. A test that must fail one pass of a two-pass reader keys a read failure
// by offset (failReadAt) or changes the card between the passes (onRewind).

#include <fcntl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using oflag_t = int;

namespace fakesd {

struct Entry {
  std::string path;
  bool isDir = false;
  std::vector<uint8_t> bytes;
};

struct Card {
  std::vector<Entry> entries;  // creation order
  std::vector<std::string>
      ops;  // "open <p>", "close <p>", "write <p>", "rename <a> <b>", "remove <p>", "mkdir <p>", "list <p>"

  // Per-path failures.
  std::set<std::string> failOpen;       // open of the path returns an empty file
  std::set<std::string> failOpenWrite;  // opening the path for writing fails
  std::set<std::string> failWrite;      // write to the path stores nothing and returns 0
  std::set<std::string> failClose;      // close of the path returns false
  std::set<std::string> failRemove;
  std::set<std::string> failRename;  // as source or as target
  std::set<std::string> failMkdir;
  // A read that would touch byte `n` or later of the path fails (returns -1, as an SD error does).
  std::map<std::string, size_t> failReadAt;
  // Reads of the path return no bytes from offset `n` on (a short read, not an error).
  std::map<std::string, size_t> shortReadAt;

  // getName: false = SdFat (0 when the name does not fit), true = the simulator's (cut to fit).
  bool getNameCuts = false;
  // Called on every rewindDirectory with the folder's path and how many rewinds it has had,
  // so a test can change the card between a reader's passes.
  std::function<void(const std::string& dir, int rewinds)> onRewind;
  std::map<std::string, int> rewinds;
};

inline Card& sim() {
  static Card card;
  return card;
}

inline void reset() { sim() = Card(); }

inline Entry* find(const std::string& path) {
  for (auto& entry : sim().entries)
    if (entry.path == path) return &entry;
  return nullptr;
}

inline std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

inline std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Test set-up: a folder (and the folders above it), or a file (whose folder must exist
// or is made). Replaces a file's bytes when it exists.
inline void addDir(const std::string& path) {
  if (path.empty() || path == "/" || find(path)) return;
  addDir(parentOf(path));
  sim().entries.push_back(Entry{path, true, {}});
}

inline void addFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  addDir(parentOf(path));
  if (Entry* const existing = find(path)) {
    existing->bytes = bytes;
    return;
  }
  sim().entries.push_back(Entry{path, false, bytes});
}

inline void addFile(const std::string& path, const std::string& text) {
  addFile(path, std::vector<uint8_t>(text.begin(), text.end()));
}

inline void removeEntry(const std::string& path) {
  auto& entries = sim().entries;
  entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.path == path; }),
                entries.end());
}

// The card's root always exists.
inline bool has(const std::string& path) { return path == "/" || find(path) != nullptr; }

inline std::vector<uint8_t> bytesOf(const std::string& path) {
  const Entry* const entry = find(path);
  return entry ? entry->bytes : std::vector<uint8_t>{};
}

// How many ops start with `prefix` ("open /a", "rename ").
inline size_t countOps(const std::string& prefix) {
  size_t n = 0;
  for (const auto& op : sim().ops)
    if (op.compare(0, prefix.size(), prefix) == 0) ++n;
  return n;
}

}  // namespace fakesd

class HalFile {
 public:
  HalFile() = default;
  HalFile(std::string path, const bool directory) : path(std::move(path)), directory(directory), open(true) {}
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    path = std::move(other.path);
    directory = other.directory;
    open = other.open;
    pos = other.pos;
    next = other.next;
    other.open = false;
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  explicit operator bool() const { return open; }
  bool isOpen() const { return open; }
  bool isDirectory() const { return open && directory; }

  size_t getName(char* name, const size_t length) {
    const std::string base = fakesd::baseName(path);
    if (base.size() + 1 <= length) {
      std::memcpy(name, base.c_str(), base.size() + 1);
      return base.size();
    }
    if (!fakesd::sim().getNameCuts || length == 0) return 0;
    std::memcpy(name, base.data(), length - 1);
    name[length - 1] = '\0';
    return length - 1;
  }
  size_t size() { return fileSize(); }
  size_t fileSize() {
    const fakesd::Entry* const entry = fakesd::find(path);
    return entry && !entry->isDir ? entry->bytes.size() : 0;
  }
  size_t position() const { return pos; }
  bool seek(const size_t to) {
    pos = to;
    return true;
  }
  bool seekSet(const size_t to) { return seek(to); }
  int available() const {
    const fakesd::Entry* const entry = fakesd::find(path);
    return entry && pos < entry->bytes.size() ? static_cast<int>(entry->bytes.size() - pos) : 0;
  }

  int read(void* out, const size_t count) {
    fakesd::Entry* const entry = fakesd::find(path);
    if (!open || !entry || entry->isDir) return -1;
    const auto& card = fakesd::sim();
    const auto failAt = card.failReadAt.find(path);
    if (failAt != card.failReadAt.end() && pos + count > failAt->second) return -1;
    size_t limit = entry->bytes.size();
    const auto shortAt = card.shortReadAt.find(path);
    if (shortAt != card.shortReadAt.end()) limit = std::min(limit, shortAt->second);
    const size_t n = pos >= limit ? 0 : std::min(count, limit - pos);
    if (n > 0) std::memcpy(out, entry->bytes.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  int read() {
    uint8_t byte = 0;
    return read(&byte, 1) == 1 ? byte : -1;
  }

  size_t write(const uint8_t* data, const size_t count) {
    fakesd::Entry* const entry = fakesd::find(path);
    if (!open || !entry || entry->isDir) return 0;
    fakesd::sim().ops.push_back("write " + path);
    if (fakesd::sim().failWrite.count(path) != 0) return 0;
    entry->bytes.insert(entry->bytes.end(), data, data + count);
    return count;
  }
  size_t write(const void* data, const size_t count) { return write(static_cast<const uint8_t*>(data), count); }
  void flush() {}

  bool rename(const char* newPath);

  bool close() {
    if (!open) return false;
    open = false;
    fakesd::sim().ops.push_back("close " + path);
    return fakesd::sim().failClose.count(path) == 0;
  }

  // Folder listing: the folder's direct children in creation order, each opened
  // (a folder's child is a folder or a file). An empty file at the end.
  void rewindDirectory() {
    next = 0;
    auto& card = fakesd::sim();
    card.ops.push_back("list " + path);
    const int count = ++card.rewinds[path];
    if (card.onRewind) card.onRewind(path, count);
  }
  HalFile openNextFile() {
    if (!open || !directory) return HalFile();
    const auto& entries = fakesd::sim().entries;
    while (next < entries.size()) {
      const fakesd::Entry& entry = entries[next++];
      if (entry.path != path && fakesd::parentOf(entry.path) == path) return HalFile(entry.path, entry.isDir);
    }
    return HalFile();
  }

 private:
  std::string path;
  bool directory = false;
  bool open = false;
  size_t pos = 0;
  size_t next = 0;  // the directory scan's place in the entry list
};

class HalStorage {
 public:
  bool exists(const char* path) { return fakesd::has(path); }

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY) {
    const std::string p(path);
    auto& card = fakesd::sim();
    const bool writing = (oflag & (O_WRONLY | O_RDWR)) != 0;
    card.ops.push_back("open " + p);
    if (card.failOpen.count(p) != 0 || (writing && card.failOpenWrite.count(p) != 0)) return HalFile();
    if (p == "/") return HalFile(p, true);  // the card's root always exists
    fakesd::Entry* entry = fakesd::find(p);
    if (!entry) {
      if (!writing || (oflag & O_CREAT) == 0 || !fakesd::has(fakesd::parentOf(p))) return HalFile();
      card.entries.push_back(fakesd::Entry{p, false, {}});
      entry = &card.entries.back();
    } else if (writing && (oflag & O_TRUNC) != 0 && !entry->isDir) {
      entry->bytes.clear();
    }
    return HalFile(p, entry->isDir);
  }
  bool mkdir(const char* path, const bool pFlag = true) {
    const std::string p(path);
    fakesd::sim().ops.push_back("mkdir " + p);
    if (fakesd::sim().failMkdir.count(p) != 0) return false;
    if (fakesd::has(p)) return p == "/" || fakesd::find(p)->isDir;
    if (!pFlag && !fakesd::has(fakesd::parentOf(p))) return false;
    fakesd::addDir(p);
    return true;
  }
  bool ensureDirectoryExists(const char* path) { return mkdir(path, true); }
  bool remove(const char* path) {
    const std::string p(path);
    fakesd::sim().ops.push_back("remove " + p);
    fakesd::Entry* const entry = fakesd::find(p);
    if (fakesd::sim().failRemove.count(p) != 0 || !entry || entry->isDir) return false;
    fakesd::removeEntry(p);
    return true;
  }
  bool rmdir(const char* path) {
    const std::string p(path);
    fakesd::sim().ops.push_back("remove " + p);
    fakesd::Entry* const entry = fakesd::find(p);
    if (fakesd::sim().failRemove.count(p) != 0 || !entry || !entry->isDir) return false;
    for (const auto& other : fakesd::sim().entries)
      if (fakesd::parentOf(other.path) == p) return false;  // not empty
    fakesd::removeEntry(p);
    return true;
  }
  bool rename(const char* from, const char* to) {
    const std::string a(from);
    const std::string b(to);
    auto& card = fakesd::sim();
    card.ops.push_back("rename " + a + " " + b);
    if (card.failRename.count(a) != 0 || card.failRename.count(b) != 0) return false;
    fakesd::Entry* const source = fakesd::find(a);
    if (!source || fakesd::has(b) || !fakesd::has(fakesd::parentOf(b))) return false;
    source->path = b;  // a folder's children are not moved: no game code renames a folder
    return true;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (!fakesd::has(path)) return false;
    file = open(path);
    return static_cast<bool>(file);
  }
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    return static_cast<bool>(file);
  }
  bool openFileForWrite(const char* module, const std::string& path, HalFile& file) {
    return openFileForWrite(module, path.c_str(), file);
  }

  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
};

#define Storage HalStorage::getInstance()

inline bool HalFile::rename(const char* newPath) {
  const bool moved = Storage.rename(path.c_str(), newPath);
  if (moved) path = newPath;
  return moved;
}
