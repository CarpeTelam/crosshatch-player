#pragma once

#include <cstddef>
#include <cstdint>

namespace GameCore {

// A game's own images (ch.gfx.image): each `<name>.bmp` in its folder, except
// icon.bmp (the launcher's), as the installer writes it from a package PNG in
// PngToBmpConverter's 1-bit layout (writeBmpHeader1bit): a 62-byte header, then
// top-down rows padded to 4 bytes, MSB first, bit 1 white. Pure, so the header
// check and the table are host-tested; GameAssets loads the files through them.

// The most `.bmp` bytes (headers included, icon.bmp excluded) a game's images may take.
inline constexpr size_t IMAGES_BYTES = 128 * 1024;
// The most images a game may have (AD-15's member cap, as for Lua sources).
inline constexpr size_t MAX_IMAGES = 32;
// The converter's header: 14-byte file header, 40-byte DIB, two 4-byte palette entries.
inline constexpr size_t IMAGE_HEADER_BYTES = 62;
// The longest image name, without ".bmp".
inline constexpr size_t IMAGE_NAME_BYTES = 32;

// The reserved images (AD-15, as amended 2026-10-02), which only the runtime draws and ch.gfx.image
// never names: icon.bmp (the launcher's), title.bmp (the title screen's splash), and handoff.bmp (the
// hidden hand-off screen's splash, which falls back to title.bmp; amended 2026-10-02, owner, hand-off redesign). The
// two pages count toward IMAGES_BYTES and MAX_IMAGES at install; icon.bmp does not. The largest title.png and
// handoff.png a package may ship, in pixels: each fills the same 480 x 480 band.
inline constexpr uint32_t TITLE_IMAGE_WIDTH = 480;
inline constexpr uint32_t TITLE_IMAGE_HEIGHT = 480;
inline constexpr uint32_t HANDOFF_IMAGE_WIDTH = 480;
inline constexpr uint32_t HANDOFF_IMAGE_HEIGHT = 480;

// True when the `length` bytes at `stem` (an image name without ".bmp", any letter case) name a
// reserved image: "icon", "title", or "handoff".
bool isReservedImage(const char* stem, size_t length);

// checkImageHeader's verdict, in the order it checks (ImageBudget adds TooMany).
enum class ImageCheck : uint8_t {
  Ok,
  Truncated,   // under the header's 62 bytes, or shorter than the header says
  NotBmp,      // no "BM" signature
  WrongDepth,  // not 1 bit per pixel
  // A checked field not the converter's (pixel offset, DIB size, width, height,
  // planes, compression, image size, file size, colours used, palette), or longer
  // than the header says. The reserved bytes (6-9), the resolution fields, and
  // colours-important are not checked.
  WrongLayout,
  OverBudget,  // more bytes than the budget has left
  TooMany,     // past MAX_IMAGES images
};

// A short English phrase for logs.
const char* imageCheckName(ImageCheck check);

// What a valid header says: the size in pixels and the bytes of one padded row.
struct ImageHeader {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rowBytes = 0;

  // The pixel rows' bytes, which follow the header.
  size_t pixelBytes() const { return static_cast<size_t>(rowBytes) * height; }
};

// Checks the first `headerBytes` bytes of a `fileBytes`-byte file against the
// converter's 1-bit layout, and that the file fits in `budgetLeft` bytes (exactly
// the budget left is Ok). Fills `out` only when Ok.
ImageCheck checkImageHeader(const uint8_t* header, size_t headerBytes, size_t fileBytes, size_t budgetLeft,
                            ImageHeader& out);

// Pass 1 of loading a game's images: each image's header checked against what the
// images before it left of IMAGES_BYTES, and at most MAX_IMAGES of them. Only an
// Ok image is counted.
struct ImageBudget {
  size_t count = 0;
  size_t fileBytes = 0;   // what IMAGES_BYTES counts: whole .bmp files
  size_t pixelBytes = 0;  // what the loaded table holds: their rows

  // Checks the next image (checkImageHeader's arguments, less the budget left) and
  // counts it when Ok; TooMany once MAX_IMAGES are counted.
  ImageCheck add(const uint8_t* header, size_t headerBytes, size_t fileBytes, ImageHeader& out);
};

// Writes the image name of `fileName` ("badge" for "badge.bmp", `length` bytes, no
// NUL needed) when it matches [a-z0-9_]{1,32}.bmp and is not a reserved image
// (isReservedImage); false otherwise.
bool imageNameOf(const char* fileName, size_t length, char (&name)[IMAGE_NAME_BYTES + 1]);

// True for a name ending in ".bmp" in any case, which a player means as an image.
bool looksLikeImage(const char* fileName, size_t length);

// One loaded image: its name, size, and where its rows start in GameImages::pixels.
struct ImageSpan {
  char name[IMAGE_NAME_BYTES + 1];
  uint32_t width;
  uint32_t height;
  uint32_t rowBytes;
  uint32_t offset;
};

// A game's loaded images: a span table and the pixel rows they point into (one
// PSRAM block on the device, owned by GameAssets). Empty by default.
struct GameImages {
  const ImageSpan* spans = nullptr;
  size_t count = 0;
  const uint8_t* pixels = nullptr;

  // The index of the image named by `length` bytes at `name`, or -1.
  int find(const char* name, size_t length) const;
  // The first row of `span`'s pixels.
  const uint8_t* pixelsOf(const ImageSpan& span) const { return pixels + span.offset; }
};

// No images: what a game without any sees.
inline constexpr GameImages NO_IMAGES{};

}  // namespace GameCore
