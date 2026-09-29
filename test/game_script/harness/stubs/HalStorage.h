#pragma once

// A fake SD card for the src/games harness, grown from save_store_stubs/HalStorage.h:
// files and folders in creation order (so a folder lists as a FAT one does), the
// calls the game code makes (open, read, write, rename, remove, removeDir, mkdir,
// folder listing), and a failure injected per path. It follows SdFat where the game
// code can tell:
//   - rename refuses an existing target and a target inside the source, and moves a
//     folder's whole subtree; mkdir fails on a path that exists (O_EXCL);
//   - a file opens read-only, write-only, or read-write, and a read or write the mode
//     forbids fails, as does opening a folder for writing;
//   - removing while a folder is listed does not skip the entries after it;
//   - an open file closes when its HalFile is destroyed or assigned over;
//   - getName returns 0 for a name that does not fit (fakesd::sim().getNameCuts = true
//     switches to the simulator's, which cuts the name instead).
// Paths are exact strings and must be normalised (a leading "/", no trailing "/", no
// "//"): the fake aborts on one that is not, because SdFat would read it another way.
// It is case-sensitive, where FAT is not.
//
// Failures stay set until fakesd::reset(); each is keyed by the exact path the code
// passes. A test that must fail one pass of a two-pass reader keys a read failure by
// offset (failReadAt) or changes the card between the passes (onRewind).

#include <fcntl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Print.h"

using oflag_t = int;

namespace fakesd {

struct Entry {
  std::string path;
  bool isDir = false;
  bool dead = false;  // removed: kept in place so a folder being listed does not shift
  std::vector<uint8_t> bytes;
};

struct Card {
  std::vector<Entry> entries;  // creation order, removed ones marked dead
  std::vector<std::string>
      ops;  // "open <p>", "close <p>", "write <p>", "rename <a> <b>", "remove <p>", "mkdir <p>", "list <p>"

  // Per-path failures.
  std::set<std::string> failOpen;       // open of the path returns an empty file
  std::set<std::string> failOpenWrite;  // opening the path for writing fails
  std::set<std::string> failWrite;      // write to the path stores nothing and returns 0
  std::set<std::string> failClose;      // close() of the path returns false (a destructor's close cannot fail)
  std::set<std::string> failRemove;     // remove or rmdir of the path fails
  std::set<std::string> failRename;     // as source or as target
  std::set<std::string> failMkdir;
  // A read that would reach byte `n` or later of the path fails (returns -1, as an SD error does).
  std::map<std::string, size_t> failReadAt;
  // Reads of the path return no bytes from offset `n` on (a short read, not an error).
  std::map<std::string, size_t> shortReadAt;
  // A folder's listing ends after `n` entries (0: it lists nothing), as a failing read of it would.
  std::map<std::string, size_t> failListAfter;

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

// Aborts on a path the fake does not model (see the header comment).
inline void checkPath(const std::string& path) {
  const bool ok =
      !path.empty() && path[0] == '/' && (path == "/" || path.back() != '/') && path.find("//") == std::string::npos;
  if (ok) return;
  std::fprintf(stderr, "fakesd: path \"%s\" is not normalised (leading \"/\", no trailing \"/\", no \"//\")\n",
               path.c_str());
  std::abort();
}

inline Entry* find(const std::string& path) {
  checkPath(path);
  for (auto& entry : sim().entries)
    if (!entry.dead && entry.path == path) return &entry;
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

// The card's root always exists.
inline bool has(const std::string& path) { return path == "/" || find(path) != nullptr; }

inline bool isDir(const std::string& path) {
  if (path == "/") return true;
  const Entry* const entry = find(path);
  return entry && entry->isDir;
}

// Test set-up: a folder (and the folders above it), or a file (whose folder must exist
// or is made). Replaces a file's bytes when it exists.
inline void addDir(const std::string& path) {
  checkPath(path);
  if (path == "/" || find(path)) return;
  addDir(parentOf(path));
  sim().entries.push_back(Entry{path, true, false, {}});
}

inline void addFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  addDir(parentOf(path));
  if (Entry* const existing = find(path)) {
    existing->bytes = bytes;
    return;
  }
  sim().entries.push_back(Entry{path, false, false, bytes});
}

inline void addFile(const std::string& path, const std::string& text) {
  addFile(path, std::vector<uint8_t>(text.begin(), text.end()));
}

// Removes one entry (a folder's children are not touched).
inline void removeEntry(const std::string& path) {
  if (Entry* const entry = find(path)) entry->dead = true;
}

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

inline bool inSubtree(const std::string& path, const std::string& root) {
  return path == root ||
         (path.size() > root.size() && path.compare(0, root.size(), root) == 0 && path[root.size()] == '/');
}

}  // namespace fakesd

class HalFile : public Print {
 public:
  HalFile() = default;
  HalFile(std::string path, const bool directory, const oflag_t oflag)
      : path(std::move(path)),
        directory(directory),
        open(true),
        readable((oflag & O_ACCMODE) != O_WRONLY),
        writable((oflag & O_ACCMODE) != O_RDONLY) {}
  ~HalFile() override { closeQuietly(); }
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this == &other) return *this;
    closeQuietly();  // an assigned-over file closes, as the real one's Impl does
    path = std::move(other.path);
    directory = other.directory;
    open = other.open;
    readable = other.readable;
    writable = other.writable;
    pos = other.pos;
    next = other.next;
    listed = other.listed;
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
    if (!open || !readable || !entry || entry->isDir) return -1;
    const auto& card = fakesd::sim();
    // The bytes this read would return, less any failure: only the bytes that exist can fail it.
    const size_t reach = pos >= entry->bytes.size() ? 0 : std::min(count, entry->bytes.size() - pos);
    const auto failAt = card.failReadAt.find(path);
    if (failAt != card.failReadAt.end() && reach > 0 && pos + reach > failAt->second) return -1;
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

  size_t write(const uint8_t* data, const size_t count) override {
    fakesd::Entry* const entry = fakesd::find(path);
    if (!open || !writable || !entry || entry->isDir) return 0;
    fakesd::sim().ops.push_back("write " + path);
    if (fakesd::sim().failWrite.count(path) != 0) return 0;
    entry->bytes.insert(entry->bytes.end(), data, data + count);
    return count;
  }
  size_t write(const void* data, const size_t count) { return write(static_cast<const uint8_t*>(data), count); }
  size_t write(const uint8_t byte) override { return write(&byte, 1); }
  void flush() override {}

  bool rename(const char* newPath);

  bool close() {
    if (!open) return false;
    open = false;
    fakesd::sim().ops.push_back("close " + path);
    return fakesd::sim().failClose.count(path) == 0;
  }

  // Folder listing: the folder's direct children in creation order, each opened as it
  // was created (a file read-only). An empty file at the end, or where failListAfter says.
  void rewindDirectory() {
    next = 0;
    listed = 0;
    auto& card = fakesd::sim();
    card.ops.push_back("list " + path);
    const int count = ++card.rewinds[path];
    if (card.onRewind) card.onRewind(path, count);
  }
  HalFile openNextFile() {
    if (!open || !directory) return HalFile();
    const auto& card = fakesd::sim();
    const auto cap = card.failListAfter.find(path);
    if (cap != card.failListAfter.end() && listed >= cap->second) return HalFile();
    const auto& entries = card.entries;
    while (next < entries.size()) {
      const fakesd::Entry& entry = entries[next++];
      if (!entry.dead && entry.path != path && fakesd::parentOf(entry.path) == path) {
        ++listed;
        return HalFile(entry.path, entry.isDir, O_RDONLY);
      }
    }
    return HalFile();
  }

 private:
  // What the real HalFile's destructor does: close, and nothing a caller could act on.
  void closeQuietly() {
    if (!open) return;
    open = false;
    fakesd::sim().ops.push_back("close " + path);
  }

  std::string path;
  bool directory = false;
  bool open = false;
  bool readable = true;
  bool writable = false;
  size_t pos = 0;
  size_t next = 0;    // the directory scan's place in the entry list
  size_t listed = 0;  // entries the scan has returned
};

class HalStorage {
 public:
  bool exists(const char* path) { return fakesd::has(path); }

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY) {
    const std::string p(path);
    auto& card = fakesd::sim();
    const bool writing = (oflag & O_ACCMODE) != O_RDONLY;
    card.ops.push_back("open " + p);
    if (card.failOpen.count(p) != 0 || (writing && card.failOpenWrite.count(p) != 0)) return HalFile();
    if (p == "/") return writing ? HalFile() : HalFile(p, true, oflag);  // the card's root always exists
    fakesd::Entry* entry = fakesd::find(p);
    if (!entry) {
      if (!writing || (oflag & O_CREAT) == 0 || !fakesd::isDir(fakesd::parentOf(p))) return HalFile();
      card.entries.push_back(fakesd::Entry{p, false, false, {}});
      entry = &card.entries.back();
    } else if (entry->isDir) {
      if (writing) return HalFile();  // a folder opens read-only
    } else if (writing && (oflag & O_TRUNC) != 0) {
      entry->bytes.clear();
    }
    return HalFile(p, entry->isDir, oflag);
  }
  // Fails for a path that exists (SdFat creates with O_EXCL), for a missing parent unless
  // `pFlag`, and for a parent that is a file.
  bool mkdir(const char* path, const bool pFlag = true) {
    const std::string p(path);
    fakesd::sim().ops.push_back("mkdir " + p);
    if (fakesd::sim().failMkdir.count(p) != 0 || fakesd::has(p)) return false;
    const std::string parent = fakesd::parentOf(p);
    if (fakesd::has(parent) ? !fakesd::isDir(parent) : !pFlag) return false;
    fakesd::addDir(p);
    return true;
  }
  // As SDCardManager::ensureDirectoryExists: true for a folder that is there, else mkdir.
  bool ensureDirectoryExists(const char* path) {
    if (fakesd::isDir(path)) return true;
    return mkdir(path, true);
  }
  bool remove(const char* path) {
    const std::string p(path);
    fakesd::sim().ops.push_back("remove " + p);
    fakesd::Entry* const entry = fakesd::find(p);
    if (fakesd::sim().failRemove.count(p) != 0 || !entry || entry->isDir) return false;
    entry->dead = true;
    return true;
  }
  bool rmdir(const char* path) {
    const std::string p(path);
    fakesd::sim().ops.push_back("remove " + p);
    fakesd::Entry* const entry = fakesd::find(p);
    if (fakesd::sim().failRemove.count(p) != 0 || !entry || !entry->isDir) return false;
    for (const auto& other : fakesd::sim().entries)
      if (!other.dead && fakesd::parentOf(other.path) == p) return false;  // not empty
    entry->dead = true;
    return true;
  }
  // As SDCardManager::removeDir: removes what the folder holds, folders first-to-last as
  // they list, then the folder; false at the first thing that will not go.
  bool removeDir(const char* path) {
    const std::string p(path);
    if (!fakesd::isDir(p) || p == "/") return false;
    HalFile dir = open(path);
    if (!dir) return false;
    for (HalFile child = dir.openNextFile(); child; child = dir.openNextFile()) {
      char name[128];
      child.getName(name, sizeof(name));
      const std::string childPath = p + "/" + name;
      const bool isFolder = child.isDirectory();
      child.close();
      if (isFolder ? !removeDir(childPath.c_str()) : !remove(childPath.c_str())) return false;
    }
    return rmdir(path);
  }
  // Moves the file, or the folder and everything under it. Refuses an existing target, a
  // target inside the source, and a target whose folder is missing.
  bool rename(const char* from, const char* to) {
    const std::string a(from);
    const std::string b(to);
    auto& card = fakesd::sim();
    card.ops.push_back("rename " + a + " " + b);
    if (card.failRename.count(a) != 0 || card.failRename.count(b) != 0) return false;
    if (!fakesd::find(a) || fakesd::has(b) || !fakesd::isDir(fakesd::parentOf(b)) || fakesd::inSubtree(b, a))
      return false;
    for (auto& entry : card.entries) {
      if (!entry.dead && fakesd::inSubtree(entry.path, a)) entry.path = b + entry.path.substr(a.size());
    }
    return true;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file = open(path, O_RDONLY);
    return static_cast<bool>(file);
  }
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file = open(path, O_RDWR | O_CREAT | O_TRUNC);  // as SDCardManager
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
