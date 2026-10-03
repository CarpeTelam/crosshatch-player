#if FREEINK_CAP_GAMES

#include "GameRegistry.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "GameHostCaps.h"
#include "GamePaths.h"

namespace {

constexpr size_t DIR_NAME_BUFFER = 64;
constexpr size_t CHUNK_BYTES = 96;

bool nameLess(const GameRegistry::Entry& a, const GameRegistry::Entry& b) {
  const int byName = strcasecmp(a.manifest.name, b.manifest.name);
  return byName < 0 || (byName == 0 && std::strcmp(a.manifest.id, b.manifest.id) < 0);
}

// Parses /.games/<dirName>/manifest.json into `entry`; false (logged) when it is missing,
// invalid, or names another id.
bool readManifest(const char* dirName, GameCore::ManifestReader& reader, GameRegistry::Entry& entry) {
  char path[GamePaths::PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s/manifest.json", GamePaths::GAMES_DIR, dirName);
  auto file = Storage.open(path);
  if (!file) {
    LOG_INF("GAME", "Skipping %s: no manifest.json", dirName);
    return false;
  }
  char chunk[CHUNK_BYTES];
  reader.begin();
  for (int n = file.read(chunk, sizeof(chunk)); n > 0; n = file.read(chunk, sizeof(chunk))) {
    reader.feed(chunk, static_cast<size_t>(n));
  }
  file.close();
  const GameCore::ManifestError error = reader.finish(entry.manifest);
  if (error != GameCore::ManifestError::None) {
    LOG_INF("GAME", "Skipping %s: %s", dirName, GameCore::describe(error));
    return false;
  }
  if (std::strcmp(entry.manifest.id, dirName) != 0) {
    LOG_INF("GAME", "Skipping %s: manifest id is %s", dirName, entry.manifest.id);
    return false;
  }
  return true;
}

}  // namespace

bool GameRegistry::readPackageHash(const char* id, uint8_t (&hash)[GamePkg::HASH_BYTES]) {
  char path[GamePaths::PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s/%s", GamePaths::GAMES_DIR, id, GamePaths::PKG_NAME);
  auto file = Storage.open(path);
  if (!file) return false;
  // One byte more than a .pkg holds, so a longer file is told from a complete one.
  char bytes[GamePkg::FILE_BYTES + 1];
  const int length = file.read(bytes, sizeof(bytes));
  file.close();
  return length > 0 && GamePkg::parsePkg(bytes, static_cast<size_t>(length), hash);
}

bool GameRegistry::readGame(const char* dirName, GameCore::ManifestReader& reader, Entry& out) {
  if (!readPackageHash(dirName, out.pkgHash)) {
    LOG_INF("GAME", "Skipping %s: no valid .pkg", dirName);
    return false;
  }
  return readManifest(dirName, reader, out);
}

bool GameRegistry::load(Listing& out) {
  out.entries.reset();
  out.count = 0;
  auto dir = Storage.open(GamePaths::GAMES_DIR);
  if (!dir || !dir.isDirectory()) return true;

  size_t folders = 0;
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) ++folders;
  }
  const size_t capacity = std::min(folders, MAX_GAMES);
  if (capacity == 0) return true;

  // Holds the JSON token buffer; reused for every manifest.
  auto reader = makeUniqueNoThrow<GameCore::ManifestReader>();
  // One entry per folder, at most MAX_GAMES (about 200 B each), sized once here.
  auto entries = makeUniqueNoThrow<Entry[]>(capacity);
  if (!reader || !entries) {
    LOG_ERR("GAME", "OOM: manifest reader (%u B) or %u games (%u B)",
            static_cast<unsigned>(sizeof(GameCore::ManifestReader)), static_cast<unsigned>(capacity),
            static_cast<unsigned>(capacity * sizeof(Entry)));
    return false;
  }

  const GameCore::HostCaps host = gameHostCaps();
  size_t count = 0;
  char dirName[DIR_NAME_BUFFER];
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const size_t length = entry.getName(dirName, sizeof(dirName));
    const bool isGameFolder = entry.isDirectory() && length > 0 && length < sizeof(dirName) - 1 && dirName[0] != '.';
    entry.close();
    if (!isGameFolder) continue;
    if (count >= capacity) {
      LOG_INF("GAME", "Listing the first %u games only", static_cast<unsigned>(capacity));
      break;
    }
    Entry& game = entries[count];
    if (!readGame(dirName, *reader, game)) continue;
    game.check = game.manifest.check(host);
    if (game.check.ok() && game.check.reason != GameCore::CheckReason::None) {
      LOG_INF("GAME", "%s: %s; its other modes still work", game.manifest.id, GameCore::describe(game.check.reason));
    }
    ++count;
  }
  std::sort(entries.get(), entries.get() + count, nameLess);
  LOG_INF("GAME", "Found %u games", static_cast<unsigned>(count));
  out.entries = std::move(entries);
  out.count = count;
  return true;
}

#endif  // FREEINK_CAP_GAMES
