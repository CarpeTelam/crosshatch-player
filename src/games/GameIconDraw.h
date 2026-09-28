#pragma once

#include <cstddef>

class GfxRenderer;

// Library icons (GameIcons) on the renderer. Screens draw them by name with
// drawGameIcon, so they reach the icon library only through src/games, with one
// exception: Home's cover-grid Games tab (CoverGridHomeUi.cpp, ledger row 9)
// includes GameIcons.generated.h and draws GAME_CONTROLLER_32 itself (GameIcons.h
// says why). ch.gfx.icon's replay (FrameReplay) draws by index with drawGameIconAt.
// Only ink pixels are drawn: the background is left as it was.

// Draws the icon named `name` with its top-left at (x, y) on the logical screen,
// `pixels` wide and high (32, 64, or 128), in black or white, in its regular
// weight or (`fill`) its fill weight, clipped to the screen. False, logged, for an
// unknown name or a size other than those.
bool drawGameIcon(const GfxRenderer& renderer, const char* name, int x, int y, int pixels, bool black,
                  bool fill = false);

// Draws icon `index` of GameIcons::ICONS at `pixels`, in its regular or (`fill`)
// fill weight, with its top-left at (x, y) on a width x height canvas whose top-left
// is (originX, originY) on the screen, clipped to that canvas. False, logged,
// drawing nothing, for an index or size the library lacks.
bool drawGameIconAt(const GfxRenderer& renderer, size_t index, int pixels, int originX, int originY, int width,
                    int height, int x, int y, bool black, bool fill);
