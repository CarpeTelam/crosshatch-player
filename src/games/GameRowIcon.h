#pragma once

#include <PackageLimits.h>

#include <cstddef>
#include <cstdint>

// The icons of the launcher's rows (spine AD-22, AD-24), as bitmaps the SDK's list draws: 64 x 64,
// top-down rows of 8 bytes, MSB first, bit 0 = ink. That is FreeInkUI's BitmapFormat::Mask1, and it
// is how both sources already lay out their pixels: icon.bmp (the converter's 1-bit layout, where bit
// 1 is white) and a library icon (GameIcons.h). A row draws, in this order, the package's icon.bmp,
// else the manifest's library icon by name and weight, else `game-controller` (choose).
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

// The library icon a row without any other icon shows.
inline constexpr const char* FALLBACK_NAME = "game-controller";

// What a row draws: where its icon comes from and, for a library icon, which one.
struct Choice {
  Source source = Source::Fallback;
  const char* name = FALLBACK_NAME;  // the library icon to render; null for PackageBmp
  bool fill = false;                 // its fill weight
};

// The choice for a row: the package's icon.bmp when it was read, else the manifest's icon (in the fill weight when
// `manifestFill`) when the library has it, else the fallback. An `icon` the library lacks (a hand-copied
// /.games/<id>/ can name one; the installer refuses it) is the fallback like no icon. `manifestIcon` outlives the
// Choice, which points at it.
Choice choose(bool packageIconRead, const char* manifestIcon, bool manifestFill);

// Whether /.games/<id>/icon.bmp exists.
bool hasPackageIcon(const char* id);

// Reads /.games/<id>/icon.bmp into `bits` (BYTES bytes) when it is a 64 x 64 file in the layout
// GameCore::checkImageHeader accepts. False, logged, otherwise; `bits` is then unspecified.
bool readPackageIcon(const char* id, uint8_t* bits);

// Whether the library has an icon of that name.
bool hasLibraryIcon(const char* name);

// Decodes library icon `name` in its regular or (`fill`) fill weight into `bits` (BYTES bytes). `bits` starts blank
// (no ink) and keeps whatever rows decoded; false says it is not the whole icon: an unknown name, or a malformed
// bitmap, which only a generator bug makes (GameIcons.h's static_assert rejects it at build time).
bool renderLibraryIcon(const char* name, bool fill, uint8_t* bits);

}  // namespace GameRowIcon
