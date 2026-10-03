#if FREEINK_CAP_GAMES

#include "GamePicture.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Manifest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "games/GameIconDraw.h"
#include "games/GameImageBlit.h"
#include "games/GameMarkBitmaps.h"
#include "games/GamePaths.h"

namespace {

// GAMES_DIR, "/", an id of the longest length, "/handoff.bmp" (the longest page name), and the NUL fit a path.
static_assert(GamePaths::PATH_BYTES >= std::char_traits<char>::length(GamePaths::GAMES_DIR) + 1 +
                                           GameCore::Manifest::MAX_ID_BYTES + sizeof("/handoff.bmp"),
              "a path to a reserved page fits");
// Each pixel of icon.bmp is drawn as SCALE x SCALE.
constexpr int SCALE = GamePicture::ICON_PIXELS / GameRowIcon::SIDE;
static_assert(SCALE * GameRowIcon::SIDE == GamePicture::ICON_PIXELS, "icon.bmp scales to the drawn icon whole");

// A Mask1 bitmap's ink (bit 0, MSB first, rows of side / 8 bytes), each run of a row drawn `scale` pixels high and
// `scale` times as wide, from (left, top).
void drawMask1(const GfxRenderer& renderer, const uint8_t* bits, const int side, const int scale, const int left,
               const int top) {
  const int rowBytes = side / 8;
  for (int y = 0; y < side; ++y) {
    const uint8_t* row = bits + static_cast<size_t>(y) * rowBytes;
    int x = 0;
    while (x < side) {
      const auto ink = [row](const int column) { return ((row[column / 8] >> (7 - column % 8)) & 1) == 0; };
      if (!ink(x)) {
        ++x;
        continue;
      }
      const int start = x;
      while (x < side && ink(x)) ++x;
      renderer.fillRect(left + start * scale, top + y * scale, (x - start) * scale, scale, true);
    }
  }
}

}  // namespace

bool GamePicture::loadPage(const char* gameId, const char* stem, const uint32_t maxWidth, const uint32_t maxHeight) {
  page.reset();
  char path[GamePaths::PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s/%s.bmp", GamePaths::GAMES_DIR, gameId, stem);
  if (!Storage.exists(path)) return false;  // the package ships none: the screen's default, nothing to say
  auto file = Storage.open(path);
  if (!file) {
    LOG_ERR("GAME", "Cannot open %s; skipped", path);
    return false;
  }
  uint8_t header[GameCore::IMAGE_HEADER_BYTES];
  const size_t fileBytes = file.fileSize();
  const size_t wanted = fileBytes < sizeof(header) ? fileBytes : sizeof(header);
  const int read = file.read(header, sizeof(header));
  if (read < 0 || static_cast<size_t>(read) < wanted) {
    LOG_ERR("GAME", "Cannot read %s; skipped", path);
    return false;
  }
  // The file is the whole budget: only its layout and its size are in question.
  GameCore::ImageHeader size;
  const GameCore::ImageCheck check =
      GameCore::checkImageHeader(header, static_cast<size_t>(read), fileBytes, fileBytes, size);
  if (check != GameCore::ImageCheck::Ok) {
    LOG_ERR("GAME", "%s is not a usable page: %s; skipped", path, GameCore::imageCheckName(check));
    return false;
  }
  if (size.width > maxWidth || size.height > maxHeight) {
    LOG_ERR("GAME", "%s is %ux%u, over %ux%u; skipped", path, static_cast<unsigned>(size.width),
            static_cast<unsigned>(size.height), static_cast<unsigned>(maxWidth), static_cast<unsigned>(maxHeight));
    return false;
  }
  HalMemory::PsramBuffer pixels = HalMemory::allocatePsram(size.pixelBytes());
  if (!pixels) {
    LOG_ERR("GAME", "OOM: %u B for %s; skipped", static_cast<unsigned>(size.pixelBytes()), path);
    return false;
  }
  if (file.read(pixels.get(), size.pixelBytes()) != static_cast<int>(size.pixelBytes())) {
    LOG_ERR("GAME", "Cannot read the rows of %s; skipped", path);
    return false;
  }
  page = std::move(pixels);
  pageSize = size;
  LOG_DBG("GAME", "Page %s: %ux%u", path, static_cast<unsigned>(size.width), static_cast<unsigned>(size.height));
  return true;
}

void GamePicture::loadIcon(const GameCore::Manifest& manifest) {
  const bool read = GameRowIcon::hasPackageIcon(manifest.id) && GameRowIcon::readPackageIcon(manifest.id, iconBits);
  icon = GameRowIcon::choose(read, manifest.icon, manifest.iconWeight == GameCore::Manifest::ICON_FILL);
}

void GamePicture::drawPage(const GfxRenderer& renderer, const int left, const int top, const int width,
                           const int height) const {
  if (!page) return;
  GameCore::ImageSpan span{};
  span.width = pageSize.width;
  span.height = pageSize.height;
  span.rowBytes = pageSize.rowBytes;
  // Centred in the rect; runs() clips to it, so a page larger than the rect shows its middle.
  const int32_t imageLeft = (width - static_cast<int32_t>(pageSize.width)) / 2;
  const int32_t imageTop = (height - static_cast<int32_t>(pageSize.height)) / 2;
  GameImageBlit::runs(span, page.get(), imageLeft, imageTop, width, height, true,
                      [&](const int32_t y, const int32_t x, const int32_t w, const bool black) {
                        if (black) renderer.fillRect(left + x, top + y, w, 1, true);
                      });
}

void GamePicture::drawIcon(const GfxRenderer& renderer, const int centreX, const int centreY) const {
  const int left = centreX - ICON_PIXELS / 2;
  const int top = centreY - ICON_PIXELS / 2;
  if (icon.source == GameRowIcon::Source::Fallback) {
    // The Crosshatch mark's native 128 px drawing, not the 64 px bitmap doubled.
    static_assert(sizeof(GameMark::HERO_128) == ICON_PIXELS / 8 * ICON_PIXELS, "the mark's hero bitmap is ICON_PIXELS");
    drawMask1(renderer, GameMark::HERO_128, ICON_PIXELS, 1, left, top);
  } else if (icon.source == GameRowIcon::Source::Library) {
    drawGameIcon(renderer, icon.name, left, top, ICON_PIXELS, true, icon.fill);
  } else {
    drawMask1(renderer, iconBits, GameRowIcon::SIDE, SCALE, left, top);  // icon.bmp, each pixel SCALE x SCALE
  }
}

#endif  // FREEINK_CAP_GAMES
