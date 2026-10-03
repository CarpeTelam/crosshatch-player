#if FREEINK_CAP_GAMES

#include "GameRowIcon.h"

#include <GameIcons.h>
#include <GameImages.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Manifest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "GameIconBlit.h"
#include "GameMarkBitmaps.h"
#include "GamePaths.h"

namespace {

// GAMES_DIR, "/", an id of the longest length, "/icon.bmp", and the terminating NUL fit a path.
static_assert(GamePaths::PATH_BYTES >= std::char_traits<char>::length(GamePaths::GAMES_DIR) + 1 +
                                           GameCore::Manifest::MAX_ID_BYTES + 1 + 8 + 1,
              "a path to icon.bmp fits");

static_assert(GameRowIcon::SIDE == GameIcons::MEDIUM_PIXELS, "the library's medium bitmap is the row icon");
static_assert(sizeof(GameMark::ROW_64) == GameRowIcon::BYTES, "the mark's row icon is a row icon bitmap");

void iconPath(const char* id, char (&path)[GamePaths::PATH_BYTES]) {
  snprintf(path, sizeof(path), "%s/%s/icon.bmp", GamePaths::GAMES_DIR, id);
}

}  // namespace

namespace GameRowIcon {

Choice choose(const bool packageIconRead, const char* manifestIcon, const bool manifestFill) {
  if (packageIconRead) return Choice{Source::PackageBmp, nullptr, false};
  if (manifestIcon && manifestIcon[0] != '\0' && hasLibraryIcon(manifestIcon)) {
    return Choice{Source::Library, manifestIcon, manifestFill};
  }
  return Choice{Source::Fallback, nullptr, false};
}

bool hasPackageIcon(const char* id) {
  char path[GamePaths::PATH_BYTES];
  iconPath(id, path);
  return Storage.exists(path);
}

bool readPackageIcon(const char* id, uint8_t* bits) {
  char path[GamePaths::PATH_BYTES];
  iconPath(id, path);
  auto file = Storage.open(path);
  if (!file) return false;

  uint8_t header[GameCore::IMAGE_HEADER_BYTES];
  const size_t fileBytes = file.fileSize();
  const size_t wanted = fileBytes < sizeof(header) ? fileBytes : sizeof(header);
  const int read = file.read(header, sizeof(header));
  if (read < 0 || static_cast<size_t>(read) < wanted) {
    LOG_ERR("GAME", "Cannot read %s", path);
    return false;
  }
  // The file is the whole budget: only its layout is in question.
  GameCore::ImageHeader image;
  const GameCore::ImageCheck check =
      GameCore::checkImageHeader(header, static_cast<size_t>(read), fileBytes, fileBytes, image);
  if (check != GameCore::ImageCheck::Ok) {
    LOG_ERR("GAME", "%s is not a usable icon: %s", path, GameCore::imageCheckName(check));
    return false;
  }
  if (image.width != static_cast<uint32_t>(SIDE) || image.height != static_cast<uint32_t>(SIDE) ||
      image.pixelBytes() != BYTES) {
    LOG_ERR("GAME", "%s is %ux%u, not %dx%d", path, static_cast<unsigned>(image.width),
            static_cast<unsigned>(image.height), SIDE, SIDE);
    return false;
  }
  if (file.read(bits, BYTES) != static_cast<int>(BYTES)) {
    LOG_ERR("GAME", "Cannot read the rows of %s", path);
    return false;
  }
  return true;
}

bool hasLibraryIcon(const char* name) { return GameIcons::find(name, std::strlen(name)) >= 0; }

bool renderLibraryIcon(const char* name, const bool fill, uint8_t* bits) {
  std::memset(bits, 0xFF, BYTES);  // bit 1 = no ink
  const int index = GameIcons::find(name, std::strlen(name));
  if (index < 0) return false;
  GameIconBlit::Source source;
  if (!GameIconBlit::sourceFor(static_cast<size_t>(index), SIDE,
                               fill ? GameIcons::Weight::Fill : GameIcons::Weight::Regular, source)) {
    return false;
  }
  // The runs drawGameIcon fills, cleared into the bitmap instead: the very pixels it would draw. A malformed
  // bitmap (only a generator bug makes one) ends the icon there, as it does in a draw.
  GameIconBlit::inkRuns(source, 0, 0, SIDE, SIDE, [bits](const int32_t y, const int32_t x, const int32_t width) {
    for (int32_t column = x; column < x + width; ++column) {
      bits[static_cast<size_t>(y) * ROW_BYTES + static_cast<size_t>(column) / 8] &=
          static_cast<uint8_t>(~(0x80u >> (column % 8)));
    }
  });
  return true;
}

void renderMark(uint8_t* bits) { std::memcpy(bits, GameMark::ROW_64, BYTES); }

}  // namespace GameRowIcon

#endif  // FREEINK_CAP_GAMES
