#pragma once

#include <cstddef>

class GfxRenderer;

// Library icons (GameIcons) on the renderer. Screens draw them by name with
// drawGameIcon, so they reach the icon library only through src/games; ch.gfx.icon's
// replay (FrameReplay) draws by index with drawGameIconAt. Only ink pixels are
// drawn: the background is left as it was.

// Draws the icon named `name` with its top-left at (x, y) on the logical screen,
// `pixels` wide and high (32, 64, or 128), in black or white, clipped to the
// screen. False, logged, for an unknown name or a size other than those.
bool drawGameIcon(const GfxRenderer& renderer, const char* name, int x, int y, int pixels, bool black);

// Draws icon `index` of GameIcons::ICONS at `pixels` with its top-left at (x, y)
// on a width x height canvas whose top-left is (originX, originY) on the screen,
// clipped to that canvas. False, logged, drawing nothing, for an index or size the
// library lacks.
bool drawGameIconAt(const GfxRenderer& renderer, size_t index, int pixels, int originX, int originY, int width,
                    int height, int x, int y, bool black);
