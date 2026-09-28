#if FREEINK_CAP_GAMES

#include "GameAssets.h"

#include <HalStorage.h>
#include <Logging.h>
#include <strings.h>

#include <cstdio>
#include <cstring>

#include "GamePaths.h"
#include "GameSaveStore.h"

namespace {

constexpr size_t NAME_BUFFER = 48;   // longer names cannot be modules or images and are skipped
constexpr size_t LUA_EXT_BYTES = 4;  // ".lua"

// One buffer holds a module name or an image name, whichever the file is.
static_assert(GameScript::SourceSpan::MAX_NAME_BYTES == GameCore::IMAGE_NAME_BYTES, "one stem buffer for both");
using Stem = char[GameCore::IMAGE_NAME_BYTES + 1];

// Writes the module name of `fileName` ("main" for "main.lua") when it matches
// [a-z0-9_]{1,32}.lua; false otherwise.
bool moduleNameOf(const char* fileName, const size_t length,
                  char (&module)[GameScript::SourceSpan::MAX_NAME_BYTES + 1]) {
  if (length <= LUA_EXT_BYTES || length - LUA_EXT_BYTES > GameScript::SourceSpan::MAX_NAME_BYTES) return false;
  if (std::strcmp(fileName + length - LUA_EXT_BYTES, ".lua") != 0) return false;
  for (size_t i = 0; i < length - LUA_EXT_BYTES; ++i) {
    const char c = fileName[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    module[i] = c;
  }
  module[length - LUA_EXT_BYTES] = '\0';
  return true;
}

// True for a name ending in ".lua" in any case, which a player means as a source.
bool looksLikeLua(const char* fileName, const size_t length) {
  return length > LUA_EXT_BYTES && strcasecmp(fileName + length - LUA_EXT_BYTES, ".lua") == 0;
}

// Reads the header at the start of `file` and adds the image to `budget`
// (GameCore::ImageBudget::add), leaving the file at its pixel rows when Ok. Its own
// frame holds the header, so load()'s stays small.
[[gnu::noinline]] GameCore::ImageCheck addImage(HalFile& file, GameCore::ImageBudget& budget,
                                                GameCore::ImageHeader& out) {
  uint8_t header[GameCore::IMAGE_HEADER_BYTES];
  const int read = file.read(header, sizeof(header));
  return budget.add(header, read > 0 ? static_cast<size_t>(read) : 0, file.fileSize(), out);
}

}  // namespace

GameAssets::LoadResult GameAssets::load(const char* gameId, GameSaveStore& saves, GameScript::StoreSlot& store) {
  release();

  char path[GamePaths::PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s", GamePaths::GAMES_DIR, gameId);
  if (!Storage.exists(path)) {
    LOG_ERR("GAME", "No game folder %s", path);
    return LoadResult::FolderMissing;
  }
  auto dir = Storage.open(path);
  if (!dir) {
    LOG_ERR("GAME", "Cannot open %s", path);
    return LoadResult::CannotRead;
  }
  if (!dir.isDirectory()) {
    LOG_ERR("GAME", "%s is not a folder", path);
    return LoadResult::FolderMissing;
  }

  // Pass 1: count the modules and images and their bytes so one block holds them
  // all, checking each image's header against the budget left on the way.
  char name[NAME_BUFFER];
  Stem stem;
  size_t count = 0;
  size_t misnamed = 0;
  size_t textBytes = 0;
  GameCore::ImageBudget budget;
  bool badImage = false;
  GameCore::ImageHeader header;
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const size_t length = file.getName(name, sizeof(name));
    if (file.isDirectory() || length == 0) continue;
    const bool fits = length < sizeof(name) - 1;
    if (fits && moduleNameOf(name, length, stem)) {
      // A module: counted, and read in pass 2.
      ++count;
      textBytes += file.fileSize();
    } else if (fits && GameCore::imageNameOf(name, length, stem)) {
      // An image: checked now, within the budget the images before it left.
      const GameCore::ImageCheck check = addImage(file, budget, header);
      if (check == GameCore::ImageCheck::OverBudget) {
        LOG_ERR("GAME", "%s/%s is not a usable image: %s (images take at most %u bytes in all)", path, name,
                GameCore::imageCheckName(check), static_cast<unsigned>(GameCore::IMAGES_BYTES));
      } else if (check == GameCore::ImageCheck::TooMany) {
        LOG_ERR("GAME", "%s/%s is one image too many: a game has at most %u", path, name,
                static_cast<unsigned>(GameCore::MAX_IMAGES));
      } else if (check != GameCore::ImageCheck::Ok) {
        LOG_ERR("GAME", "%s/%s is not a usable image: %s", path, name, GameCore::imageCheckName(check));
      }
      badImage = badImage || check != GameCore::ImageCheck::Ok;
    } else if (looksLikeLua(name, length)) {
      // A .lua file no require can name: said, so it never goes missing silently.
      ++misnamed;
      LOG_ERR("GAME", "%s/%s is not loaded: a module name is [a-z0-9_]{1,32}.lua", path, name);
    } else if (GameCore::looksLikeImage(name, length) && strcasecmp(name, "icon.bmp") != 0) {
      // A .bmp file no ch.gfx.image can name: skipped with a log line on purpose,
      // not a load failure; the game fails later only if it draws that name.
      // icon.bmp is the launcher's, not a game image.
      LOG_ERR("GAME", "%s/%s is not loaded: an image name is [a-z0-9_]{1,32}.bmp", path, name);
    }
  }
  if (count == 0) {
    LOG_ERR("GAME", "%s holds no loadable Lua sources", path);
    return misnamed > 0 ? LoadResult::BadSourceName : LoadResult::NoSources;
  }
  if (count > MAX_SOURCES || textBytes > MAX_SOURCE_BYTES) {
    LOG_ERR("GAME", "%s: %u Lua files, %u bytes; limits %u and %u", path, static_cast<unsigned>(count),
            static_cast<unsigned>(textBytes), static_cast<unsigned>(MAX_SOURCES),
            static_cast<unsigned>(MAX_SOURCE_BYTES));
    return LoadResult::TooLarge;
  }
  if (badImage) return LoadResult::BadImage;
  const size_t imageCount = budget.count;
  const size_t pixelBytes = budget.pixelBytes;

  // One block: [SourceSpan x count][ImageSpan x imageCount][image rows][text]. Both
  // span types are 4-byte multiples aligned to at most 4, so the image spans stay aligned.
  static_assert(alignof(GameScript::SourceSpan) <= 4 && sizeof(GameScript::SourceSpan) % 4 == 0, "span alignment");
  static_assert(alignof(GameCore::ImageSpan) <= 4 && sizeof(GameCore::ImageSpan) % 4 == 0, "span alignment");
  const size_t spanBytes = count * sizeof(GameScript::SourceSpan);
  const size_t imageSpanBytes = imageCount * sizeof(GameCore::ImageSpan);
  const size_t blockBytes = spanBytes + imageSpanBytes + pixelBytes + textBytes;
  block = HalMemory::allocatePsram(blockBytes);
  if (!block) {
    LOG_ERR("GAME", "OOM: %u bytes of PSRAM for Lua sources and images", static_cast<unsigned>(blockBytes));
    return LoadResult::OutOfMemory;
  }
  // SourceSpan and ImageSpan are implicit-lifetime aggregates, so the zeroed bytes
  // are their objects.
  std::memset(block.get(), 0, spanBytes + imageSpanBytes);
  auto* spans = reinterpret_cast<GameScript::SourceSpan*>(block.get());
  auto* imageSpans = reinterpret_cast<GameCore::ImageSpan*>(block.get() + spanBytes);
  uint8_t* pixels = block.get() + spanBytes + imageSpanBytes;
  char* text = reinterpret_cast<char*>(pixels + pixelBytes);

  // Pass 2: read each module and each image's rows into place. The folder may
  // change between passes, so re-check each image and stay inside what pass 1
  // sized: its budget as well as its bytes.
  size_t loaded = 0;
  size_t offset = 0;
  size_t imagesLoaded = 0;
  GameCore::ImageBudget reread;
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file && (loaded < count || imagesLoaded < imageCount);
       file = dir.openNextFile()) {
    const size_t length = file.getName(name, sizeof(name));
    if (file.isDirectory() || length == 0 || length >= sizeof(name) - 1) continue;
    if (moduleNameOf(name, length, stem)) {
      if (loaded == count) continue;
      const size_t size = file.fileSize();
      if (size > textBytes - offset || file.read(text + offset, size) != static_cast<int>(size)) {
        LOG_ERR("GAME", "Cannot read %s/%s", path, name);
        release();
        return LoadResult::CannotRead;
      }
      GameScript::SourceSpan& span = spans[loaded++];
      std::memcpy(span.name, stem, sizeof(stem));
      span.offset = static_cast<uint32_t>(offset);
      span.length = static_cast<uint32_t>(size);
      offset += size;
    } else if (GameCore::imageNameOf(name, length, stem)) {
      if (imagesLoaded == imageCount) continue;
      const size_t pixelOffset = reread.pixelBytes;
      const bool same = addImage(file, reread, header) == GameCore::ImageCheck::Ok &&
                        reread.fileBytes <= budget.fileBytes && reread.pixelBytes <= pixelBytes;
      if (!same || file.read(pixels + pixelOffset, header.pixelBytes()) != static_cast<int>(header.pixelBytes())) {
        LOG_ERR("GAME", "Cannot read %s/%s, or it changed since it was checked", path, name);
        release();
        return LoadResult::CannotRead;
      }
      GameCore::ImageSpan& image = imageSpans[imagesLoaded++];
      std::memcpy(image.name, stem, sizeof(stem));
      image.width = header.width;
      image.height = header.height;
      image.rowBytes = header.rowBytes;
      image.offset = static_cast<uint32_t>(pixelOffset);
    }
  }

  if (loaded != count || imagesLoaded != imageCount) {
    // The folder lost a file between the passes.
    LOG_ERR("GAME", "Read %u of %u Lua files and %u of %u images from %s", static_cast<unsigned>(loaded),
            static_cast<unsigned>(count), static_cast<unsigned>(imagesLoaded), static_cast<unsigned>(imageCount), path);
    release();
    return LoadResult::CannotRead;
  }

  view.spans = spans;
  view.count = loaded;
  view.text = text;
  imageView.spans = imageSpans;
  imageView.count = imagesLoaded;
  imageView.pixels = pixels;
  LOG_INF("GAME", "Loaded %u Lua files (%u bytes) and %u images (%u bytes) from %s", static_cast<unsigned>(loaded),
          static_cast<unsigned>(offset), static_cast<unsigned>(imagesLoaded), static_cast<unsigned>(reread.fileBytes),
          path);
  saves.restoreInto(store);
  return LoadResult::Ok;
}

#endif  // FREEINK_CAP_GAMES
