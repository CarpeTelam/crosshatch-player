#include "GameImages.h"

#include <cstring>
#include <initializer_list>

namespace GameCore {

namespace {

constexpr size_t BMP_EXT_BYTES = 4;  // ".bmp"

// writeBmpHeader1bit's fields: offsets into the header and the values it writes.
constexpr size_t FILE_SIZE_AT = 2;
constexpr size_t PIXEL_OFFSET_AT = 10;
constexpr size_t DIB_SIZE_AT = 14;
constexpr size_t WIDTH_AT = 18;
constexpr size_t HEIGHT_AT = 22;
constexpr size_t PLANES_AT = 26;
constexpr size_t BITS_AT = 28;
constexpr size_t COMPRESSION_AT = 30;
constexpr size_t IMAGE_SIZE_AT = 34;
constexpr size_t COLORS_USED_AT = 46;
constexpr size_t PALETTE_AT = 54;
constexpr uint32_t DIB_BYTES = 40;
// Index 0 black, index 1 white, each blue, green, red, reserved.
constexpr uint8_t PALETTE[] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
static_assert(PALETTE_AT + sizeof(PALETTE) == IMAGE_HEADER_BYTES, "the palette ends the header");

// Little-endian fields, whatever the host's byte order.
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

int64_t le32Signed(const uint8_t* p) { return static_cast<int32_t>(le32(p)); }

char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

}  // namespace

const char* imageCheckName(const ImageCheck check) {
  switch (check) {
    case ImageCheck::Ok:
      return "ok";
    case ImageCheck::Truncated:
      return "truncated";
    case ImageCheck::NotBmp:
      return "not a BMP file";
    case ImageCheck::WrongDepth:
      return "not 1 bit per pixel";
    case ImageCheck::WrongLayout:
      return "not the converter's 1-bit layout";
    case ImageCheck::OverBudget:
      return "over the images budget";
    case ImageCheck::TooMany:
      return "one image too many";
  }
  return "?";
}

ImageCheck checkImageHeader(const uint8_t* header, const size_t headerBytes, const size_t fileBytes,
                            const size_t budgetLeft, ImageHeader& out) {
  if (!header || headerBytes < IMAGE_HEADER_BYTES || fileBytes < IMAGE_HEADER_BYTES) return ImageCheck::Truncated;
  if (header[0] != 'B' || header[1] != 'M') return ImageCheck::NotBmp;
  if (le16(header + BITS_AT) != 1) return ImageCheck::WrongDepth;

  const int64_t width = le32Signed(header + WIDTH_AT);
  const int64_t height = -le32Signed(header + HEIGHT_AT);  // stored negative: top-down
  if (le32(header + PIXEL_OFFSET_AT) != IMAGE_HEADER_BYTES || le32(header + DIB_SIZE_AT) != DIB_BYTES || width <= 0 ||
      height <= 0 || le16(header + PLANES_AT) != 1 || le32(header + COMPRESSION_AT) != 0 ||
      le32(header + COLORS_USED_AT) != 2 || std::memcmp(header + PALETTE_AT, PALETTE, sizeof(PALETTE)) != 0) {
    return ImageCheck::WrongLayout;
  }
  // 64-bit: a 2^31 - 1 px wide, 2^31 px tall header must not wrap.
  const uint64_t rowBytes = (static_cast<uint64_t>(width) + 31) / 32 * 4;
  const uint64_t pixelBytes = rowBytes * static_cast<uint64_t>(height);
  const uint64_t layoutBytes = IMAGE_HEADER_BYTES + pixelBytes;
  if (le32(header + IMAGE_SIZE_AT) != pixelBytes || le32(header + FILE_SIZE_AT) != layoutBytes) {
    return ImageCheck::WrongLayout;
  }
  if (fileBytes < layoutBytes) return ImageCheck::Truncated;
  if (fileBytes > layoutBytes) return ImageCheck::WrongLayout;
  if (fileBytes > budgetLeft) return ImageCheck::OverBudget;

  out.width = static_cast<uint32_t>(width);
  out.height = static_cast<uint32_t>(height);
  out.rowBytes = static_cast<uint32_t>(rowBytes);
  return ImageCheck::Ok;
}

ImageCheck ImageBudget::add(const uint8_t* header, const size_t headerBytes, const size_t fileBytes, ImageHeader& out) {
  if (count == MAX_IMAGES) return ImageCheck::TooMany;
  const ImageCheck check = checkImageHeader(header, headerBytes, fileBytes, IMAGES_BYTES - this->fileBytes, out);
  if (check != ImageCheck::Ok) return check;
  ++count;
  this->fileBytes += fileBytes;
  pixelBytes += out.pixelBytes();
  return ImageCheck::Ok;
}

bool isReservedImage(const char* stem, const size_t length) {
  if (!stem) return false;
  for (const char* reserved : {"icon", "title", "handoff"}) {
    const size_t reservedLength = std::strlen(reserved);
    if (length != reservedLength) continue;
    size_t i = 0;
    while (i < length && lower(stem[i]) == reserved[i]) ++i;
    if (i == length) return true;
  }
  return false;
}

bool imageNameOf(const char* fileName, const size_t length, char (&name)[IMAGE_NAME_BYTES + 1]) {
  if (!fileName || length <= BMP_EXT_BYTES || length - BMP_EXT_BYTES > IMAGE_NAME_BYTES) return false;
  const size_t stem = length - BMP_EXT_BYTES;
  if (std::memcmp(fileName + stem, ".bmp", BMP_EXT_BYTES) != 0) return false;
  // The launcher's icon and the runtime's two pages are drawn by the runtime alone (AD-15).
  if (isReservedImage(fileName, stem)) return false;
  for (size_t i = 0; i < stem; ++i) {
    const char c = fileName[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  std::memcpy(name, fileName, stem);
  name[stem] = '\0';
  return true;
}

bool looksLikeImage(const char* fileName, const size_t length) {
  if (!fileName || length <= BMP_EXT_BYTES) return false;
  const char* ext = fileName + length - BMP_EXT_BYTES;
  return ext[0] == '.' && lower(ext[1]) == 'b' && lower(ext[2]) == 'm' && lower(ext[3]) == 'p';
}

int GameImages::find(const char* name, const size_t length) const {
  // A name with a NUL in it matches none (a span's name ends at its first NUL).
  if (!name || length > IMAGE_NAME_BYTES || std::memchr(name, '\0', length)) return -1;
  for (size_t i = 0; i < count; ++i) {
    if (std::memcmp(spans[i].name, name, length) == 0 && spans[i].name[length] == '\0') return static_cast<int>(i);
  }
  return -1;
}

}  // namespace GameCore
