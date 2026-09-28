#if FREEINK_CAP_GAMES

#include "GameAssets.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
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

// The buffers one load() needs, in one heap block rather than on the loop task's
// stack: the folder's path, a file's name and stem, and an image's header bytes and
// parsed header. load() allocates it; its passes and addImage() share it.
struct LoadScratch {
  char path[GamePaths::PATH_BYTES];
  char name[NAME_BUFFER];
  Stem stem;
  uint8_t headerBytes[GameCore::IMAGE_HEADER_BYTES];
  GameCore::ImageHeader header;
};

// Reads the header at the start of `file` into `scratch.headerBytes` and adds the
// image to `budget` (GameCore::ImageBudget::add), filling `scratch.header` and
// leaving the file at its pixel rows when Ok. `readFailed` is set, and the budget
// left alone, when the read fails or comes back short of a header the file is long
// enough to hold: an SD error, not a damaged image. A file under the header's 62
// bytes is still a Truncated image.
[[gnu::noinline]] GameCore::ImageCheck addImage(HalFile& file, GameCore::ImageBudget& budget, LoadScratch& scratch,
                                                bool& readFailed) {
  const size_t fileBytes = file.fileSize();
  const int read = file.read(scratch.headerBytes, sizeof(scratch.headerBytes));
  const size_t wanted = fileBytes < sizeof(scratch.headerBytes) ? fileBytes : sizeof(scratch.headerBytes);
  readFailed = read < 0 || static_cast<size_t>(read) < wanted;
  if (readFailed) return GameCore::ImageCheck::Truncated;
  return budget.add(scratch.headerBytes, static_cast<size_t>(read), fileBytes, scratch.header);
}

// Pass 1's line for a file whose name getName could not read at all (it returned 0).
void logUnreadName(const char* path) {
  LOG_ERR("GAME", "%s holds a file whose name is %u bytes or longer, or unreadable; it is not loaded", path,
          static_cast<unsigned>(NAME_BUFFER - 1));
}

// Pass 1's line for a file whose name filled NAME_BUFFER: `prefix` is as much of it
// as fit, so the player can find the file.
void logLongName(const char* path, const char* prefix) {
  LOG_ERR("GAME", "%s/%s... has a name of %u bytes or longer; it is not loaded", path, prefix,
          static_cast<unsigned>(NAME_BUFFER - 1));
}

// What pass 1 found in a game's folder.
struct FolderScan {
  size_t count = 0;     // modules
  size_t misnamed = 0;  // .lua files no require can name
  size_t textBytes = 0;
  GameCore::ImageBudget budget;
  bool badImage = false;
};

// Pass 1: counts the modules and images and their bytes in `scratch.path` so one
// block holds them all, checking each image's header against the budget left on
// the way. Ok, or CannotRead (logged) when an image's header cannot be read. The
// passes are separate functions so that none breaks the 256 B locals rule
// (AGENTS.md); their buffers live in `scratch`, on the heap, so the deepest chain,
// load() plus a pass plus addImage(), stays small too.
[[gnu::noinline]] GameAssets::LoadResult scanFolder(HalFile& dir, LoadScratch& scratch, FolderScan& scan) {
  const char* const path = scratch.path;
  char* const name = scratch.name;
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const size_t length = file.getName(name, sizeof(scratch.name));
    if (file.isDirectory()) continue;
    if (length == 0) {
      // SdFat's getName returns 0 when the name does not fit `name` (or cannot be read).
      logUnreadName(path);
      continue;
    }
    // A name that fills `name` may be cut (the simulator's getName cuts it), so it is
    // never classified as a module or an image.
    const bool fits = length < sizeof(scratch.name) - 1;
    if (fits && moduleNameOf(name, length, scratch.stem)) {
      // A module: counted, and read in pass 2.
      ++scan.count;
      scan.textBytes += file.fileSize();
    } else if (fits && GameCore::imageNameOf(name, length, scratch.stem)) {
      // An image: checked now, within the budget the images before it left.
      bool readFailed = false;
      const GameCore::ImageCheck check = addImage(file, scan.budget, scratch, readFailed);
      if (readFailed) {
        LOG_ERR("GAME", "Cannot read %s/%s", path, name);
        return GameAssets::LoadResult::CannotRead;
      }
      if (check == GameCore::ImageCheck::OverBudget) {
        LOG_ERR("GAME", "%s/%s is not a usable image: %s (images take at most %u bytes in all)", path, name,
                GameCore::imageCheckName(check), static_cast<unsigned>(GameCore::IMAGES_BYTES));
      } else if (check == GameCore::ImageCheck::TooMany) {
        LOG_ERR("GAME", "%s/%s is one image too many: a game has at most %u", path, name,
                static_cast<unsigned>(GameCore::MAX_IMAGES));
      } else if (check != GameCore::ImageCheck::Ok) {
        LOG_ERR("GAME", "%s/%s is not a usable image: %s", path, name, GameCore::imageCheckName(check));
      }
      scan.badImage = scan.badImage || check != GameCore::ImageCheck::Ok;
    } else if (looksLikeLua(name, length)) {
      // A .lua file no require can name: said, so it never goes missing silently.
      ++scan.misnamed;
      LOG_ERR("GAME", "%s/%s is not loaded: a module name is [a-z0-9_]{1,32}.lua", path, name);
    } else if (GameCore::looksLikeImage(name, length) && strcasecmp(name, "icon.bmp") != 0) {
      // A .bmp file no ch.gfx.image can name: skipped with a log line on purpose,
      // not a load failure; the game fails later only if it draws that name.
      // icon.bmp is the launcher's, not a game image.
      LOG_ERR("GAME", "%s/%s is not loaded: an image name is [a-z0-9_]{1,32}.bmp", path, name);
    } else if (!fits) {
      // Any other name that filled the buffer: said too, so no file goes missing silently.
      logLongName(path, name);
    }
  }
  return GameAssets::LoadResult::Ok;
}

// The block pass 2 fills: [SourceSpan x count][ImageSpan x imageCount][image rows][text].
struct BlockLayout {
  GameScript::SourceSpan* spans = nullptr;
  GameCore::ImageSpan* imageSpans = nullptr;
  uint8_t* pixels = nullptr;
  char* text = nullptr;
};

// What pass 2 read: the text's and the images' bytes, for the log.
struct FolderRead {
  size_t textBytes = 0;
  size_t imageFileBytes = 0;
};

// Pass 2: reads each module and each image's rows into `block`. The folder may
// change between passes, so it re-checks each image and stays inside what pass 1
// sized: its budget as well as its bytes. Ok, or CannotRead (logged) when a read
// fails, an image changed, or the folder lost a file; the caller then releases the
// block.
[[gnu::noinline]] GameAssets::LoadResult readFolder(HalFile& dir, LoadScratch& scratch, const FolderScan& scan,
                                                    const BlockLayout& block, FolderRead& read) {
  const char* const path = scratch.path;
  char* const name = scratch.name;
  const GameCore::ImageHeader& header = scratch.header;
  const size_t imageCount = scan.budget.count;
  size_t loaded = 0;
  size_t offset = 0;
  size_t imagesLoaded = 0;
  GameCore::ImageBudget reread;
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file && (loaded < scan.count || imagesLoaded < imageCount);
       file = dir.openNextFile()) {
    const size_t length = file.getName(name, sizeof(scratch.name));
    // Pass 1 logged every name skipped here.
    if (file.isDirectory() || length == 0 || length >= sizeof(scratch.name) - 1) continue;
    if (moduleNameOf(name, length, scratch.stem)) {
      if (loaded == scan.count) continue;
      const size_t size = file.fileSize();
      if (size > scan.textBytes - offset || file.read(block.text + offset, size) != static_cast<int>(size)) {
        LOG_ERR("GAME", "Cannot read %s/%s", path, name);
        return GameAssets::LoadResult::CannotRead;
      }
      GameScript::SourceSpan& span = block.spans[loaded++];
      std::memcpy(span.name, scratch.stem, sizeof(scratch.stem));
      span.offset = static_cast<uint32_t>(offset);
      span.length = static_cast<uint32_t>(size);
      offset += size;
    } else if (GameCore::imageNameOf(name, length, scratch.stem)) {
      if (imagesLoaded == imageCount) continue;
      const size_t pixelOffset = reread.pixelBytes;
      bool readFailed = false;  // any check but Ok is CannotRead here, so it needs no branch of its own
      const bool same = addImage(file, reread, scratch, readFailed) == GameCore::ImageCheck::Ok &&
                        reread.fileBytes <= scan.budget.fileBytes && reread.pixelBytes <= scan.budget.pixelBytes;
      if (!same ||
          file.read(block.pixels + pixelOffset, header.pixelBytes()) != static_cast<int>(header.pixelBytes())) {
        LOG_ERR("GAME", "Cannot read %s/%s, or it changed since it was checked", path, name);
        return GameAssets::LoadResult::CannotRead;
      }
      GameCore::ImageSpan& image = block.imageSpans[imagesLoaded++];
      std::memcpy(image.name, scratch.stem, sizeof(scratch.stem));
      image.width = header.width;
      image.height = header.height;
      image.rowBytes = header.rowBytes;
      image.offset = static_cast<uint32_t>(pixelOffset);
    }
  }

  if (loaded != scan.count || imagesLoaded != imageCount) {
    // The folder lost a file between the passes.
    LOG_ERR("GAME", "Read %u of %u Lua files and %u of %u images from %s", static_cast<unsigned>(loaded),
            static_cast<unsigned>(scan.count), static_cast<unsigned>(imagesLoaded), static_cast<unsigned>(imageCount),
            path);
    return GameAssets::LoadResult::CannotRead;
  }
  read.textBytes = offset;
  read.imageFileBytes = reread.fileBytes;
  return GameAssets::LoadResult::Ok;
}

}  // namespace

GameAssets::LoadResult GameAssets::load(const char* gameId, GameSaveStore& saves, GameScript::StoreSlot& store) {
  release();

  // After release(), so a failed allocation leaves no block behind.
  const auto scratch = makeUniqueNoThrow<LoadScratch>();
  if (!scratch) {
    LOG_ERR("GAME", "OOM: %u bytes to load %s", static_cast<unsigned>(sizeof(LoadScratch)), gameId);
    return LoadResult::OutOfMemory;
  }
  const char* const path = scratch->path;
  snprintf(scratch->path, sizeof(scratch->path), "%s/%s", GamePaths::GAMES_DIR, gameId);
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

  FolderScan scan;
  const LoadResult scanned = scanFolder(dir, *scratch, scan);
  if (scanned != LoadResult::Ok) return scanned;
  if (scan.count == 0) {
    LOG_ERR("GAME", "%s holds no loadable Lua sources", path);
    return scan.misnamed > 0 ? LoadResult::BadSourceName : LoadResult::NoSources;
  }
  if (scan.count > MAX_SOURCES || scan.textBytes > MAX_SOURCE_BYTES) {
    LOG_ERR("GAME", "%s: %u Lua files, %u bytes; limits %u and %u", path, static_cast<unsigned>(scan.count),
            static_cast<unsigned>(scan.textBytes), static_cast<unsigned>(MAX_SOURCES),
            static_cast<unsigned>(MAX_SOURCE_BYTES));
    return LoadResult::TooLarge;
  }
  if (scan.badImage) return LoadResult::BadImage;

  // One block: [SourceSpan x count][ImageSpan x imageCount][image rows][text]. Both
  // span types are 4-byte multiples aligned to at most 4, so the image spans stay aligned.
  static_assert(alignof(GameScript::SourceSpan) <= 4 && sizeof(GameScript::SourceSpan) % 4 == 0, "span alignment");
  static_assert(alignof(GameCore::ImageSpan) <= 4 && sizeof(GameCore::ImageSpan) % 4 == 0, "span alignment");
  const size_t spanBytes = scan.count * sizeof(GameScript::SourceSpan);
  const size_t imageSpanBytes = scan.budget.count * sizeof(GameCore::ImageSpan);
  const size_t blockBytes = spanBytes + imageSpanBytes + scan.budget.pixelBytes + scan.textBytes;
  block = HalMemory::allocatePsram(blockBytes);
  if (!block) {
    LOG_ERR("GAME", "OOM: %u bytes of PSRAM for Lua sources and images", static_cast<unsigned>(blockBytes));
    return LoadResult::OutOfMemory;
  }
  // SourceSpan and ImageSpan are implicit-lifetime aggregates, so the zeroed bytes
  // are their objects.
  std::memset(block.get(), 0, spanBytes + imageSpanBytes);
  BlockLayout layout;
  layout.spans = reinterpret_cast<GameScript::SourceSpan*>(block.get());
  layout.imageSpans = reinterpret_cast<GameCore::ImageSpan*>(block.get() + spanBytes);
  layout.pixels = block.get() + spanBytes + imageSpanBytes;
  layout.text = reinterpret_cast<char*>(layout.pixels + scan.budget.pixelBytes);

  FolderRead read;
  const LoadResult readResult = readFolder(dir, *scratch, scan, layout, read);
  if (readResult != LoadResult::Ok) {
    release();
    return readResult;
  }

  view.spans = layout.spans;
  view.count = scan.count;
  view.text = layout.text;
  imageView.spans = layout.imageSpans;
  imageView.count = scan.budget.count;
  imageView.pixels = layout.pixels;
  LOG_INF("GAME", "Loaded %u Lua files (%u bytes) and %u images (%u bytes) from %s", static_cast<unsigned>(scan.count),
          static_cast<unsigned>(read.textBytes), static_cast<unsigned>(scan.budget.count),
          static_cast<unsigned>(read.imageFileBytes), path);
  saves.restoreInto(store);
  return LoadResult::Ok;
}

#endif  // FREEINK_CAP_GAMES
