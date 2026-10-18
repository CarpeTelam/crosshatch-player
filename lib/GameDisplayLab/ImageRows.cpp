#include "ImageRows.h"

#include <algorithm>

namespace GameDisplayLab {

namespace {

char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// `name` ends with `ext` (lower case, with its dot), any case.
bool endsWith(const char* name, const char* ext) {
  size_t n = 0;
  while (name[n] != '\0') ++n;
  size_t e = 0;
  while (ext[e] != '\0') ++e;
  if (n <= e) return false;  // a bare extension (".png") is a hidden name, never an image
  for (size_t i = 0; i < e; ++i) {
    if (lower(name[n - e + i]) != ext[i]) return false;
  }
  return true;
}

// The converter's readPackedSample: sample `x` of a row packed most-significant sample first.
uint8_t readPackedSample(const uint8_t* pixels, const int x, const int bitsPerSample) {
  if (bitsPerSample == 8) return pixels[x];
  const int bitOffset = x * bitsPerSample;
  const int shift = 8 - bitsPerSample - (bitOffset & 7);
  const uint8_t mask = static_cast<uint8_t>((1U << bitsPerSample) - 1);
  return static_cast<uint8_t>((pixels[bitOffset >> 3] >> shift) & mask);
}

// The converter's expandSampleToByte.
uint8_t expandSampleToByte(const uint8_t sample, const int bitsPerSample) {
  if (bitsPerSample == 8) return sample;
  const unsigned maxSample = (1U << bitsPerSample) - 1;
  return static_cast<uint8_t>((sample * 255U) / maxSample);
}

uint8_t lumaOf(const uint8_t r, const uint8_t g, const uint8_t b) {
  return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
}

// The converter's store() with no alpha line: composited on white.
uint8_t onWhite(const uint8_t gray, const uint8_t alpha) {
  return static_cast<uint8_t>((gray * alpha + 255u * (255u - alpha)) / 255u);
}

}  // namespace

ImageKind imageKindOf(const char* name) {
  if (name == nullptr || name[0] == '\0' || name[0] == '.') return ImageKind::None;
  if (endsWith(name, ".png")) return ImageKind::Png;
  if (endsWith(name, ".jpg") || endsWith(name, ".jpeg")) return ImageKind::Jpeg;
  return ImageKind::None;
}

bool nameLess(const char* a, const char* b) {
  for (size_t i = 0;; ++i) {
    const char ca = lower(a[i]);
    const char cb = lower(b[i]);
    if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb);
    if (ca == '\0') return false;
  }
}

ImageList::Add ImageList::add(const char* name, const bool isDirectory) {
  if (isDirectory || imageKindOf(name) == ImageKind::None) return Add::Ignored;
  size_t length = 0;
  while (name[length] != '\0') ++length;
  if (length >= IMAGE_NAME_BYTES) return Add::TooLong;
  size_t slot = count_;
  if (count_ >= capacity_) {
    // Full: the first `capacity_` names by nameLess are kept, so the new one replaces the greatest when it sorts before
    // it (and the greatest is dropped), or is dropped itself.
    if (capacity_ == 0) return Add::Full;
    slot = 0;
    for (size_t i = 1; i < count_; ++i) {
      if (nameLess(names_[slot].text, names_[i].text)) slot = i;
    }
    if (!nameLess(name, names_[slot].text)) return Add::Full;
    for (size_t i = 0; i <= length; ++i) names_[slot].text[i] = name[i];
    return Add::Full;
  }
  for (size_t i = 0; i <= length; ++i) names_[slot].text[i] = name[i];
  ++count_;
  return Add::Listed;
}

void ImageList::sort() {
  if (count_ == 0) return;
  std::sort(names_, names_ + count_, [](const ImageName& a, const ImageName& b) { return nameLess(a.text, b.text); });
}

size_t ImageList::nextEntry(const size_t entry, const size_t entries) {
  return entries == 0 || entry + 1 >= entries ? 0 : entry + 1;
}

bool imageSizeAccepted(const int64_t width, const int64_t height) {
  if (width <= 0 || height <= 0) return false;
  if (width > MAX_SOURCE_DIMENSION || height > MAX_SOURCE_DIMENSION) return false;
  return width * height <= MAX_SOURCE_PIXELS;
}

bool pngDepthSupported(const int pixelType, const int bitsPerSample) {
  switch (pixelType) {
    case static_cast<int>(PngPixel::Grayscale):
    case static_cast<int>(PngPixel::Truecolor):
    case static_cast<int>(PngPixel::Indexed):
    case static_cast<int>(PngPixel::GrayAlpha):
    case static_cast<int>(PngPixel::TruecolorAlpha):
      break;
    default:
      return false;  // no such colour type: refused rather than painted flat gray
  }
  if (bitsPerSample == 8) return true;
  if (bitsPerSample != 1 && bitsPerSample != 2 && bitsPerSample != 4) return false;
  return pixelType == static_cast<int>(PngPixel::Grayscale) || pixelType == static_cast<int>(PngPixel::Indexed);
}

int pngInternalBufferBytes(const int width, const int pixelType, const int bitsPerSample) {
  int bytesPerPixel = 1;
  switch (static_cast<PngPixel>(pixelType)) {
    case PngPixel::Truecolor:
      bytesPerPixel = 3;
      break;
    case PngPixel::GrayAlpha:
      bytesPerPixel = 2;
      break;
    case PngPixel::TruecolorAlpha:
      bytesPerPixel = 4;
      break;
    case PngPixel::Grayscale:
    case PngPixel::Indexed:
      break;
  }
  int pitch = width * bytesPerPixel;
  const bool packed =
      pixelType == static_cast<int>(PngPixel::Grayscale) || pixelType == static_cast<int>(PngPixel::Indexed);
  if (packed && bitsPerSample < 8) pitch = (width * bitsPerSample + 7) / 8;
  // +1 filter byte per scanline, *2 for the current and previous lines, +32 for alignment.
  return ((pitch + 1) * 2) + 32;
}

void pngRowToGray(const uint8_t* pixels, uint8_t* gray, const int width, const int pixelType, const int bitsPerSample,
                  const uint8_t* palette, const bool hasAlpha, const uint32_t transparentColor) {
  switch (pixelType) {
    case static_cast<int>(PngPixel::Grayscale):
      for (int x = 0; x < width; ++x) {
        const uint8_t sample = readPackedSample(pixels, x, bitsPerSample);
        const uint8_t g = expandSampleToByte(sample, bitsPerSample);
        gray[x] = onWhite(g, hasAlpha && sample == static_cast<uint8_t>(transparentColor) ? 0 : 255);
      }
      return;
    case static_cast<int>(PngPixel::Truecolor):
      for (int x = 0; x < width; ++x) {
        const uint8_t* p = &pixels[x * 3];
        const uint32_t color = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        gray[x] = onWhite(lumaOf(p[0], p[1], p[2]), hasAlpha && color == transparentColor ? 0 : 255);
      }
      return;
    case static_cast<int>(PngPixel::Indexed):
      if (palette != nullptr) {
        for (int x = 0; x < width; ++x) {
          const uint8_t idx = readPackedSample(pixels, x, bitsPerSample);
          const uint8_t* p = &palette[idx * 3];
          gray[x] = onWhite(lumaOf(p[0], p[1], p[2]), hasAlpha ? palette[768 + idx] : 255);
        }
      } else {
        for (int x = 0; x < width; ++x) {
          gray[x] = expandSampleToByte(readPackedSample(pixels, x, bitsPerSample), bitsPerSample);
        }
      }
      return;
    case static_cast<int>(PngPixel::GrayAlpha):
      for (int x = 0; x < width; ++x) gray[x] = onWhite(pixels[x * 2], pixels[x * 2 + 1]);
      return;
    case static_cast<int>(PngPixel::TruecolorAlpha):
      for (int x = 0; x < width; ++x) {
        const uint8_t* p = &pixels[x * 4];
        gray[x] = onWhite(lumaOf(p[0], p[1], p[2]), p[3]);
      }
      return;
    default:
      for (int x = 0; x < width; ++x) gray[x] = 128;
      return;
  }
}

int jpegScaleDenominator(const uint32_t targetPermille) {
  if (targetPermille <= 125) return 8;
  if (targetPermille <= 250) return 4;
  if (targetPermille <= 500) return 2;
  return 1;
}

JpegScale jpegScaleFor(const int srcW, const int srcH, const int fitW, const int fitH, const bool progressive) {
  JpegScale scale{1, 0, srcW, srcH};
  if (srcW <= 0 || srcH <= 0) return scale;
  const auto permille = [](const int fit, const int src) {
    return static_cast<uint32_t>(static_cast<int64_t>(fit > 0 ? fit : 0) * 1000 / src);
  };
  const uint32_t w = permille(fitW, srcW);
  const uint32_t h = permille(fitH, srcH);
  scale.denominator = progressive ? 8 : jpegScaleDenominator(w < h ? w : h);
  scale.option = scale.denominator == 1 ? 0 : scale.denominator;
  scale.scaledW = (srcW + scale.denominator - 1) / scale.denominator;
  scale.scaledH = (srcH + scale.denominator - 1) / scale.denominator;
  return scale;
}

FitRect fitToScreen(const int srcW, const int srcH, const int screenW, const int screenH) {
  if (srcW <= 0 || srcH <= 0 || screenW <= 0 || screenH <= 0) return FitRect{0, 0, 0, 0};
  int w = screenW;
  int h = screenH;
  // The picture is wider than the screen (relative to its height): the width limits; otherwise the height does.
  if (static_cast<int64_t>(srcW) * screenH >= static_cast<int64_t>(srcH) * screenW) {
    h = static_cast<int>((static_cast<int64_t>(srcH) * screenW * 2 + srcW) / (static_cast<int64_t>(srcW) * 2));
  } else {
    w = static_cast<int>((static_cast<int64_t>(srcW) * screenH * 2 + srcH) / (static_cast<int64_t>(srcH) * 2));
  }
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > screenW) w = screenW;
  if (h > screenH) h = screenH;
  return FitRect{(screenW - w) / 2, (screenH - h) / 2, w, h};
}

int FitAxis::srcOf(const int d) const {
  const int s = static_cast<int>((2 * static_cast<int64_t>(d) + 1) * src / (2 * static_cast<int64_t>(dst)));
  return s < src ? s : src - 1;
}

int FitAxis::firstDst(const int s) const {
  // The least d with (2d + 1) * src >= 2 * s * dst.
  const int64_t num = 2 * static_cast<int64_t>(s) * dst - src;
  if (num <= 0) return 0;
  const int64_t den = 2 * static_cast<int64_t>(src);
  const int64_t d = (num + den - 1) / den;
  return d > dst ? dst : static_cast<int>(d);
}

FitAxis::Tap FitAxis::tap(const int d) const {
  if (!enlarges()) {
    const int s = srcOf(d);
    return Tap{s, s, 0};
  }
  // The centre's source position in 1/65536: (2d + 1) * src / (2 * dst) - 1/2.
  const int64_t position = (2 * static_cast<int64_t>(d) + 1) * src * 65536 / (2 * static_cast<int64_t>(dst)) - 32768;
  if (position <= 0) return Tap{0, 0, 0};
  const int s0 = static_cast<int>(position >> 16);
  if (s0 >= src - 1) return Tap{src - 1, src - 1, 0};
  return Tap{s0, s0 + 1, static_cast<uint32_t>(position & 0xFFFF)};
}

Span outputsEndingIn(const FitAxis& axis, const int srcStart, const int srcEnd) {
  // tap(d).s1 is non-decreasing in d: the first d whose s1 reaches a source pixel, by binary search.
  const auto firstReaching = [&axis](const int target) {
    int lo = 0;
    int hi = axis.dst;
    while (lo < hi) {
      const int mid = lo + (hi - lo) / 2;
      if (axis.tap(mid).s1 >= target) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    return lo;
  };
  return Span{firstReaching(srcStart), firstReaching(srcEnd)};
}

Span rowsReadyAt(const FitAxis& axis, const int s) { return outputsEndingIn(axis, s, s + 1); }

uint8_t SourceBlock::at(int sx, int sy) const {
  const int xLow = left != nullptr ? x0 - 1 : x0;
  const int yLow = above != nullptr ? y0 - 1 : y0;
  if (sx < xLow) sx = xLow;
  if (sx > x1 - 1) sx = x1 - 1;
  if (sy < yLow) sy = yLow;
  if (sy > y1 - 1) sy = y1 - 1;
  // The row above spans the whole width, so it also holds the corner above the left column.
  if (sy < y0) return above[sx];
  if (sx < x0) return left[sy - y0];
  return pixels[static_cast<size_t>(sy - y0) * static_cast<size_t>(stride) + static_cast<size_t>(sx - x0)];
}

void sampleBlock(const SourceBlock& block, const FitAxis& ax, const FitAxis::Tap& rowTap, const int dxFirst,
                 const int dxEnd, uint8_t* out) {
  const auto blend = [](const uint32_t a, const uint32_t b, const uint32_t w) {
    return (a * (65536u - w) + b * w) >> 16;
  };
  for (int dx = dxFirst; dx < dxEnd; ++dx) {
    const FitAxis::Tap t = ax.tap(dx);
    const uint32_t top = blend(block.at(t.s0, rowTap.s0), block.at(t.s1, rowTap.s0), t.weight);
    const uint32_t bottom = blend(block.at(t.s0, rowTap.s1), block.at(t.s1, rowTap.s1), t.weight);
    out[dx - dxFirst] = static_cast<uint8_t>(blend(top, bottom, rowTap.weight));
  }
}

}  // namespace GameDisplayLab
