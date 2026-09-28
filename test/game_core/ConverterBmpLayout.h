#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// PngToBmpConverter's 1-bit output, rebuilt on the host: the converter itself needs
// HalStorage, Arduino Print, FreeRTOS, and the display HAL, which no host suite
// builds, so the image tests check GameCore's reader and the committed fixture
// .bmp files against this mirror instead. Keep it in step with
// lib/PngToBmpConverter/PngToBmpConverter.cpp.
namespace ConverterBmpLayout {

inline void put16(std::vector<uint8_t>& out, const uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

inline void put32(std::vector<uint8_t>& out, const uint32_t v) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
}

// writeBmpHeader1bit(bmpOut, width, height), field by field.
inline std::vector<uint8_t> converterHeader1bit(const int width, const int height) {
  const int bytesPerRow = (width + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;
  std::vector<uint8_t> out;
  out.push_back('B');
  out.push_back('M');
  put32(out, fileSize);
  put32(out, 0);
  put32(out, 62);

  put32(out, 40);
  put32(out, static_cast<uint32_t>(width));    // write32Signed
  put32(out, static_cast<uint32_t>(-height));  // write32Signed: negative, top-down
  put16(out, 1);
  put16(out, 1);
  put32(out, 0);
  put32(out, static_cast<uint32_t>(imageSize));
  put32(out, 2835);
  put32(out, 2835);
  put32(out, 2);
  put32(out, 2);

  const uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t i : palette) out.push_back(i);
  return out;
}

// The converter's 1-bit rows for a width x height grid (its oneBit loop): each row
// zeroed to its padded length, then bit 1 (white) set at 7 - x % 8 of byte x / 8.
inline std::vector<uint8_t> converterRows1bit(const int width, const int height,
                                              const std::function<bool(int x, int y)>& white) {
  const int bytesPerRow = (width + 31) / 32 * 4;
  std::vector<uint8_t> out(static_cast<size_t>(bytesPerRow) * height, 0);
  for (int y = 0; y < height; ++y) {
    uint8_t* rowBuffer = out.data() + static_cast<size_t>(y) * bytesPerRow;
    for (int x = 0; x < width; ++x) {
      const uint8_t bit = white(x, y) ? 1 : 0;
      const int byteIndex = x / 8;
      const int bitOffset = 7 - (x % 8);
      rowBuffer[byteIndex] |= (bit << bitOffset);
    }
  }
  return out;
}

// A whole converted file: the header and the rows.
inline std::vector<uint8_t> converterBmp1bit(const int width, const int height,
                                             const std::function<bool(int x, int y)>& white) {
  std::vector<uint8_t> file = converterHeader1bit(width, height);
  const std::vector<uint8_t> rows = converterRows1bit(width, height, white);
  file.insert(file.end(), rows.begin(), rows.end());
  return file;
}

}  // namespace ConverterBmpLayout
