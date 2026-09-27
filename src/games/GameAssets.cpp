#if FREEINK_CAP_GAMES

#include "GameAssets.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr size_t NAME_BUFFER = 48;  // longer names cannot be modules and are skipped
constexpr size_t PATH_BUFFER = 96;

// Writes the module name of `fileName` ("main" for "main.lua") when it matches
// [a-z0-9_]{1,32}.lua; false otherwise.
bool moduleNameOf(const char* fileName, const size_t length,
                  char (&module)[GameScript::SourceSpan::MAX_NAME_BYTES + 1]) {
  constexpr size_t EXT = 4;  // ".lua"
  if (length <= EXT || length - EXT > GameScript::SourceSpan::MAX_NAME_BYTES) return false;
  if (std::strcmp(fileName + length - EXT, ".lua") != 0) return false;
  for (size_t i = 0; i < length - EXT; ++i) {
    const char c = fileName[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    module[i] = c;
  }
  module[length - EXT] = '\0';
  return true;
}

}  // namespace

const char* GameAssets::load(const char* gameId) {
  block.reset();
  view = GameScript::GameSources{};

  char path[PATH_BUFFER];
  snprintf(path, sizeof(path), "/.games/%s", gameId);
  auto dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("GAME", "No game folder %s", path);
    return "game folder missing";
  }

  // Pass 1: count the modules and their bytes so one block holds them all.
  char name[NAME_BUFFER];
  char module[GameScript::SourceSpan::MAX_NAME_BYTES + 1];
  size_t count = 0;
  size_t textBytes = 0;
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const size_t length = file.getName(name, sizeof(name));
    if (file.isDirectory() || length == 0 || length >= sizeof(name) - 1 || !moduleNameOf(name, length, module)) {
      continue;
    }
    ++count;
    textBytes += file.fileSize();
  }
  if (count == 0) {
    LOG_ERR("GAME", "%s holds no Lua sources", path);
    return "no Lua sources";
  }
  if (count > MAX_SOURCES || textBytes > MAX_SOURCE_BYTES) {
    LOG_ERR("GAME", "%s: %u Lua files, %u bytes; limits %u and %u", path, static_cast<unsigned>(count),
            static_cast<unsigned>(textBytes), static_cast<unsigned>(MAX_SOURCES),
            static_cast<unsigned>(MAX_SOURCE_BYTES));
    return "Lua sources too large";
  }

  const size_t spanBytes = count * sizeof(GameScript::SourceSpan);
  block = HalMemory::allocatePsram(spanBytes + textBytes);
  if (!block) {
    LOG_ERR("GAME", "OOM: %u bytes of PSRAM for Lua sources", static_cast<unsigned>(spanBytes + textBytes));
    return "out of memory";
  }
  // SourceSpan is an implicit-lifetime aggregate, so the zeroed bytes are its objects.
  std::memset(block.get(), 0, spanBytes);
  auto* spans = reinterpret_cast<GameScript::SourceSpan*>(block.get());
  char* text = reinterpret_cast<char*>(block.get() + spanBytes);

  // Pass 2: read each module into place. The folder may change between passes, so
  // stay inside what pass 1 sized.
  size_t loaded = 0;
  size_t offset = 0;
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file && loaded < count; file = dir.openNextFile()) {
    const size_t length = file.getName(name, sizeof(name));
    if (file.isDirectory() || length == 0 || length >= sizeof(name) - 1 || !moduleNameOf(name, length, module)) {
      continue;
    }
    const size_t size = file.fileSize();
    if (size > textBytes - offset || file.read(text + offset, size) != static_cast<int>(size)) {
      LOG_ERR("GAME", "Cannot read %s/%s", path, name);
      block.reset();
      return "cannot read Lua sources";
    }
    GameScript::SourceSpan& span = spans[loaded++];
    std::memcpy(span.name, module, sizeof(module));
    span.offset = static_cast<uint32_t>(offset);
    span.length = static_cast<uint32_t>(size);
    offset += size;
  }

  view.spans = spans;
  view.count = loaded;
  view.text = text;
  LOG_INF("GAME", "Loaded %u Lua files (%u bytes) from %s", static_cast<unsigned>(loaded),
          static_cast<unsigned>(offset), path);
  return nullptr;
}

#endif  // FREEINK_CAP_GAMES
