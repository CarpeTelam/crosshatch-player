#pragma once

#include <PackageLimits.h>

#include <cstddef>
#include <cstdint>

// The icons of the launcher's rows (spine AD-22, AD-24), as bitmaps the SDK's list draws: 64 x 64,
// top-down rows of 8 bytes, MSB first, bit 0 = ink. That is FreeInkUI's BitmapFormat::Mask1, and it
// is how both sources already lay out their pixels: icon.bmp (the converter's 1-bit layout, where bit
// 1 is white) and a library icon (GameIcons.h). A row draws, in this order, the package's icon.bmp,
// else the manifest's library icon by name and weight, else the Crosshatch mark (choose; GameMarkBitmaps.h).
//
// Screens reach lib/GameIcons only through this header. drawGameIcon paints on a GfxRenderer, which a
// list row is not; renderLibraryIcon decodes the same bitmaps (GameIconBlit.h) into memory instead,
// so the pixels are the ones drawGameIcon would draw.
namespace GameRowIcon {

// The side of a row icon in pixels: icon.bmp's, and the library's medium size.
inline constexpr int SIDE = GameCore::ICON_PIXELS;
inline constexpr size_t ROW_BYTES = SIDE / 8;
// One icon's bitmap.
inline constexpr size_t BYTES = ROW_BYTES * SIDE;

// Where a row's icon comes from.
enum class Source : uint8_t { PackageBmp, Library, Fallback };

// The library icon a row without any other icon showed before the Crosshatch mark replaced it (UX spine 2026-10-03).
// The name stays for library lookups; a Fallback choice no longer names it.
inline constexpr const char* FALLBACK_NAME = "game-controller";

// What a row draws: where its icon comes from and, for a library icon, which one.
struct Choice {
  Source source = Source::Fallback;
  const char* name = nullptr;  // the library icon to render; null for PackageBmp and Fallback (the Crosshatch mark)
  bool fill = false;           // its fill weight
};

// The choice for a row: the package's icon.bmp when it was read, else the manifest's icon (in the fill weight when
// `manifestFill`) when the library has it, else the fallback, the Crosshatch mark. An `icon` the library lacks (a
// hand-copied /.games/<id>/ can name one; the installer refuses it) is the fallback like no icon. `manifestIcon`
// outlives the Choice, which points at it.
Choice choose(bool packageIconRead, const char* manifestIcon, bool manifestFill);

// Whether /.games/<id>/icon.bmp exists.
bool hasPackageIcon(const char* id);

// Reads /.games/<id>/icon.bmp into `bits` (BYTES bytes) when it is a 64 x 64 file in the layout
// GameCore::checkImageHeader accepts. False, logged, otherwise; `bits` is then unspecified.
bool readPackageIcon(const char* id, uint8_t* bits);

// Whether the library has an icon of that name.
bool hasLibraryIcon(const char* name);

// Renders library icon `name` in its regular or (`fill`) fill weight into `bits` (BYTES bytes) from the runs of ink
// GameIconBlit::inkRuns gives drawGameIcon, so the pixels are the ones a draw makes. `bits` starts blank (no ink); it
// stays blank, and this returns false, for a name the library lacks.
bool renderLibraryIcon(const char* name, bool fill, uint8_t* bits);

// Copies the Crosshatch mark's 64 px row icon (GameMark::ROW_64, the same layout) into `bits` (BYTES bytes): what a
// Fallback row draws.
void renderMark(uint8_t* bits);

}  // namespace GameRowIcon
