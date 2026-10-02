#pragma once

#include <GameImages.h>
#include <HalMemory.h>

#include <cstdint>

#include "games/GameRowIcon.h"

class GfxRenderer;

namespace GameCore {
struct Manifest;
}

// A game's own art outside its canvas (DESIGN.md, Components; AD-15 as amended 2026-10-02): one reserved page (the
// title screen's title.bmp; the hidden hand-off's handoff.bmp, else its title.bmp), drawn centred and clipped in the
// splash band (GameSplashLayout), and the game's icon at 128 px, drawn when there is no page. The loads read the card,
// so they run on the loop task (a screen's onEnter), never in render(); the draws only read what the loads left, so
// they run on the render task.
//
// The page is held in PSRAM (title.bmp and handoff.bmp each at most 28,862 B, 480 x 480); a load that finds no file
// leaves no page and says nothing, and one that cannot use the file (it will not read, its header is not the
// converter's 1-bit layout, or it is larger than the page allows) logs why ("...; skipped") and leaves no page, so the
// screen tries its next page or falls back to its icon. The icon is the launcher row's (GameRowIcon::choose): the
// package's icon.bmp, read into a 512 B member and drawn with each of its pixels as 2 x 2, else the manifest's library
// icon in its weight, else game-controller, both drawn by the library at 128 px.
class GamePicture {
 public:
  // The icon's side when it is drawn, in pixels.
  static constexpr int ICON_PIXELS = 128;

  GamePicture() = default;
  GamePicture(const GamePicture&) = delete;
  GamePicture& operator=(const GamePicture&) = delete;

  // Loop task. Reads /.games/<gameId>/<stem>.bmp (stem "title" or "handoff") into PSRAM when it is at most
  // maxWidth x maxHeight pixels; true when it did. Any page loaded before is released first.
  bool loadPage(const char* gameId, const char* stem, uint32_t maxWidth, uint32_t maxHeight);
  // Loop task. Chooses the icon for `manifest` and reads its icon.bmp when it has one. `manifest` must outlive this
  // picture's draws: a library icon is drawn by the name it holds.
  void loadIcon(const GameCore::Manifest& manifest);
  // Frees the page (a draw then draws the icon instead).
  void releasePage() { page.reset(); }

  bool hasPage() const { return page != nullptr; }
  // Where the icon comes from, as loadIcon chose it.
  GameRowIcon::Source iconSource() const { return icon.source; }

  // Render task. Draws the page's ink centred in the rect (left, top) width x height of the logical screen, clipped
  // to it, onto a paper background (its white pixels are left as they are). Draws nothing when there is no page.
  void drawPage(const GfxRenderer& renderer, int left, int top, int width, int height) const;
  // Render task. Draws the icon at ICON_PIXELS in ink with its centre at (centreX, centreY).
  void drawIcon(const GfxRenderer& renderer, int centreX, int centreY) const;

 private:
  HalMemory::PsramBuffer page;
  GameCore::ImageHeader pageSize;  // the page's width, height, and row bytes, while there is one
  GameRowIcon::Choice icon;
  // The package's icon.bmp (GameRowIcon::BYTES, Mask1: bit 0 ink), read when icon.source is PackageBmp.
  uint8_t iconBits[GameRowIcon::BYTES] = {};
};
