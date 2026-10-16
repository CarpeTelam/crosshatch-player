#pragma once

#include <cstddef>
#include <cstdint>

// The display lab's two full-screen drive masks (the display-lab epic, D1): pure C++, pinned by
// test/game_display_lab/test_mask_planes.cpp. A non-owning view over two caller buffers, Black (drive to black) and
// White (drive to white); a pixel set in neither holds. Each mask is a 1-bpp bitmap in the framebuffer's byte order:
// row-major, rowBytes() = (width + 7) / 8 bytes per row (100 at 800 px), the leftmost pixel of each byte in its most
// significant bit, a set bit meaning "drive this pixel that way". Bits past the width in a row's last byte are never
// set. Every write is clipped to the mask, and a default-constructed view (valid() false) ignores every write.
namespace GameDisplayLab {

class MaskPlanes {
 public:
  enum class Mask : uint8_t { Black, White };

  static constexpr size_t rowBytesFor(const uint16_t width) { return (static_cast<size_t>(width) + 7u) / 8u; }
  // The bytes each of the two buffers must hold.
  static constexpr size_t bytesFor(const uint16_t width, const uint16_t height) { return rowBytesFor(width) * height; }

  MaskPlanes() = default;
  // `black` and `white` each hold bytesFor(width, height) bytes and outlive the view; the contents are not touched.
  MaskPlanes(uint8_t* black, uint8_t* white, uint16_t width, uint16_t height);

  bool valid() const { return black_ != nullptr && white_ != nullptr && width_ > 0 && height_ > 0; }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  size_t rowBytes() const { return rowBytesFor(width_); }
  size_t bytes() const { return bytesFor(width_, height_); }
  const uint8_t* black() const { return black_; }
  const uint8_t* white() const { return white_; }

  // Both masks all clear: every pixel holds.
  void clear();
  // Every pixel set in `mask`; the other mask is unchanged.
  void fill(Mask mask);
  // Sets the pixels [x, x + w) x [y, y + h) in `mask`, clipped to the mask; w or h <= 0 sets nothing.
  void setRect(Mask mask, int x, int y, int w, int h);
  // Sets one pixel; a pixel outside the mask is ignored.
  void setPixel(Mask mask, int x, int y);
  // Whether the pixel is set in `mask`; false outside the mask.
  bool test(Mask mask, int x, int y) const;

 private:
  uint8_t* plane(Mask mask) const;

  uint8_t* black_ = nullptr;
  uint8_t* white_ = nullptr;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
};

}  // namespace GameDisplayLab
