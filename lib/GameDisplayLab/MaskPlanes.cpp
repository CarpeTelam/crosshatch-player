#include "MaskPlanes.h"

#include <algorithm>
#include <cstring>

namespace GameDisplayLab {

MaskPlanes::MaskPlanes(uint8_t* black, uint8_t* white, const uint16_t width, const uint16_t height)
    : black_(black), white_(white), width_(width), height_(height) {}

uint8_t* MaskPlanes::plane(const Mask mask) const {
  switch (mask) {
    case Mask::Black:
      return black_;
    case Mask::White:
      return white_;
  }
  return nullptr;
}

void MaskPlanes::clear() {
  if (!valid()) return;
  memset(black_, 0, bytes());
  memset(white_, 0, bytes());
}

void MaskPlanes::fill(const Mask mask) { setRect(mask, 0, 0, width_, height_); }

void MaskPlanes::setRect(const Mask mask, const int x, const int y, const int w, const int h) {
  if (!valid() || w <= 0 || h <= 0) return;
  // 64-bit so x + w cannot overflow for any int arguments.
  const int64_t x0 = std::max<int64_t>(x, 0);
  const int64_t y0 = std::max<int64_t>(y, 0);
  const int64_t x1 = std::min<int64_t>(static_cast<int64_t>(x) + w, width_);
  const int64_t y1 = std::min<int64_t>(static_cast<int64_t>(y) + h, height_);
  if (x0 >= x1 || y0 >= y1) return;  // wholly outside the mask
  const size_t firstByte = static_cast<size_t>(x0 >> 3);
  const size_t lastByte = static_cast<size_t>((x1 - 1) >> 3);
  // MSB first: the first byte keeps the bits left of x0 clear, the last the bits right of x1 - 1.
  const uint8_t firstBits = static_cast<uint8_t>(0xFFu >> (x0 & 7));
  const uint8_t lastBits = static_cast<uint8_t>(0xFFu << (7 - ((x1 - 1) & 7)));
  uint8_t* const base = plane(mask);
  const size_t stride = rowBytes();
  for (int64_t row = y0; row < y1; ++row) {
    uint8_t* const line = base + static_cast<size_t>(row) * stride;
    if (firstByte == lastByte) {
      line[firstByte] |= static_cast<uint8_t>(firstBits & lastBits);
      continue;
    }
    line[firstByte] |= firstBits;
    if (lastByte > firstByte + 1) memset(line + firstByte + 1, 0xFF, lastByte - firstByte - 1);
    line[lastByte] |= lastBits;
  }
}

void MaskPlanes::setPixel(const Mask mask, const int x, const int y) { setRect(mask, x, y, 1, 1); }

bool MaskPlanes::test(const Mask mask, const int x, const int y) const {
  if (!valid() || x < 0 || y < 0 || x >= width_ || y >= height_) return false;
  const uint8_t byte = plane(mask)[static_cast<size_t>(y) * rowBytes() + static_cast<size_t>(x >> 3)];
  return (byte & (0x80u >> (x & 7))) != 0;
}

bool MaskPlanes::any() const {
  if (!valid()) return false;
  const size_t n = bytes();
  for (size_t i = 0; i < n; ++i) {
    if ((black_[i] | white_[i]) != 0) return true;
  }
  return false;
}

}  // namespace GameDisplayLab
