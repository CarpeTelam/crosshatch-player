#if FREEINK_CAP_GAMES

#include "GameIconDraw.h"

#include <GameIcons.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdint>
#include <cstring>

#include "GameIconBlit.h"

bool drawGameIcon(const GfxRenderer& renderer, const char* name, const int x, const int y, const int pixels,
                  const bool black) {
  const int index = GameIcons::find(name, std::strlen(name));
  if (index < 0) {
    LOG_ERR("GAME", "No game icon named \"%s\"", name);
    return false;
  }
  return drawGameIconAt(renderer, static_cast<size_t>(index), pixels, 0, 0, renderer.getScreenWidth(),
                        renderer.getScreenHeight(), x, y, black);
}

bool drawGameIconAt(const GfxRenderer& renderer, const size_t index, const int pixels, const int originX,
                    const int originY, const int width, const int height, const int x, const int y, const bool black) {
  GameIconBlit::Source source;
  if (!GameIconBlit::sourceFor(index, pixels, source)) {
    LOG_ERR("GAME", "No game icon %u at %d px (icons come in 32, 64, and 128 px)", static_cast<unsigned>(index),
            pixels);
    return false;
  }
  GameIconBlit::inkRuns(source, x, y, width, height, [&](const int32_t runY, const int32_t runX, const int32_t w) {
    renderer.fillRect(originX + runX, originY + runY, w, 1, black);
  });
  return true;
}

#endif  // FREEINK_CAP_GAMES
