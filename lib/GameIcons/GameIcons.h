#pragma once

// GameIcons: the curated game icon set. The names and 1-bit bitmaps come from assets/game-icons/ through
// scripts/gen_game_icons.py, which writes the committed GameIcons.generated.h (never edit it by hand; rerun the
// script). Depends on the standard library only.
//
// Games draw an icon by name with ch.gfx.icon, screens with drawGameIcon (src/games/GameIconDraw.h); both find it
// here by name and keep its index into ICONS. The names are Phosphor's own (hyphens included), and each ICONS entry
// holds a bitmap per Weight (regular, fill) at each size: Icon::small[weight] and Icon::medium[weight]. One upstream
// file uses a bitmap directly: Home's cover-grid Games tab (src/components/CoverGridHomeUi.cpp, ledger row 9)
// includes GameIcons.generated.h and draws GAME_CONTROLLER_32 (game-controller, regular) with
// GfxRenderer::drawIcon, so renaming or dropping game-controller fails that file's build; that array alone is raw, in
// drawIcon's layout, and every bitmap in ICONS is packed (below). This folder's .clang-format turns formatting off
// for the generated data, so this file is kept in the repository's style by hand.
//
// A packed bitmap (what Icon::small and Icon::medium point at) is a 2-byte little-endian length N, then N bytes of
// PackBits that decode to the drawn rows from the top, pixels / 8 bytes each, MSB first, bit 0 = ink, bit x = drawn
// column x. A run is a control byte c and its data: c 0..127 copies the next c + 1 bytes, c 129..255 repeats the next
// byte 257 - c times, and 128 is malformed (the generator never writes it). A run may cross from one row into the
// next, so the whole bitmap is one stream, and the bytes it decodes to are exactly pixels * pixels / 8. PackedReader
// decodes it a byte at a time with a few words of state, so a draw needs only a MAX_ROW_BYTES row buffer, and a
// bitmap that is not exactly its rows fails the static_assert below. GameIconBlit.h draws them.

#include <cstddef>
#include <cstdint>

#include "GameIcons.generated.h"

namespace GameIcons {

namespace detail {

// Below, at, or above zero as `length` bytes at `name` sort before, equal to, or
// after the NUL-terminated `icon`, comparing unsigned bytes. A name that runs on
// past the icon's end sorts after it, even when its next byte is a NUL.
constexpr int compare(const char* name, const size_t length, const char* icon) {
  for (size_t i = 0;; ++i) {
    if (icon[i] == '\0') return i == length ? 0 : 1;
    if (i == length) return -1;
    const auto a = static_cast<unsigned char>(name[i]);
    const auto b = static_cast<unsigned char>(icon[i]);
    if (a != b) return a < b ? -1 : 1;
  }
}

constexpr size_t nameLength(const char* name) {
  size_t length = 0;
  while (name[length] != '\0') ++length;
  return length;
}

constexpr bool strictlySorted() {
  for (size_t i = 1; i < ICON_COUNT; ++i) {
    if (compare(ICONS[i - 1].name, nameLength(ICONS[i - 1].name), ICONS[i].name) >= 0) return false;
  }
  return true;
}

}  // namespace detail

// The bytes before a packed bitmap's runs: their length, little-endian.
inline constexpr size_t PACKED_LENGTH_BYTES = 2;
// The most bytes a decoded row holds: the medium bitmap's 64 px at 1 bit each.
inline constexpr size_t MAX_ROW_BYTES = 8;
static_assert(SMALL_PIXELS % 8 == 0 && MEDIUM_PIXELS % 8 == 0 && MEDIUM_PIXELS / 8 <= MAX_ROW_BYTES &&
                  SMALL_PIXELS <= MEDIUM_PIXELS,
              "a drawn row is whole bytes and fits the row buffer");

// The length of a packed bitmap's runs, from its two-byte prefix.
constexpr size_t packedLength(const uint8_t* bitmap) {
  return static_cast<size_t>(bitmap[0]) | (static_cast<size_t>(bitmap[1]) << 8);
}

// Decodes a packed bitmap's runs a byte at a time, in order. It never reads a byte at or past the end the bitmap's
// prefix gives, and a malformed run (control 128, or a run or copy whose data is past that end) makes the reader
// fail, and go on failing, so a draw keeps the rows it decoded before the fault and draws nothing after it.
class PackedReader {
 public:
  constexpr explicit PackedReader(const uint8_t* bitmap)
      : data_(bitmap), pos_(PACKED_LENGTH_BYTES), end_(PACKED_LENGTH_BYTES + packedLength(bitmap)) {}

  // The next decoded byte; false, `out` untouched, when the runs end or are malformed.
  constexpr bool next(uint8_t& out) {
    if (failed_) return false;
    if (remaining_ == 0 && !startRun()) {
      failed_ = true;
      return false;
    }
    if (literal_) {
      if (pos_ >= end_) {
        failed_ = true;
        return false;
      }
      out = data_[pos_++];
    } else {
      out = value_;
    }
    --remaining_;
    return true;
  }

  // Fills row[0, rowBytes) with the next rowBytes bytes; false when the runs end or are malformed first.
  constexpr bool row(uint8_t (&row)[MAX_ROW_BYTES], const size_t rowBytes) {
    if (rowBytes > MAX_ROW_BYTES) return false;
    for (size_t i = 0; i < rowBytes; ++i) {
      if (!next(row[i])) return false;
    }
    return true;
  }

  // Whether every byte of the runs has been used and no run is left over.
  constexpr bool finished() const { return !failed_ && remaining_ == 0 && pos_ == end_; }

 private:
  // Reads the next run's control byte (and a repeat's byte); false at the end or on control 128.
  constexpr bool startRun() {
    if (pos_ >= end_) return false;
    const uint8_t control = data_[pos_++];
    if (control == 128) return false;
    literal_ = control < 128;
    if (literal_) {
      remaining_ = static_cast<size_t>(control) + 1;
    } else {
      if (pos_ >= end_) return false;
      remaining_ = 257 - static_cast<size_t>(control);
      value_ = data_[pos_++];
    }
    return true;
  }

  const uint8_t* data_;
  size_t pos_;
  size_t end_;
  size_t remaining_ = 0;  // bytes left in the current run
  bool literal_ = false;  // the current run copies its data, rather than repeating value_
  bool failed_ = false;
  uint8_t value_ = 0;
};

namespace detail {

// Whether a packed bitmap `pixels` square is exactly its rows: its runs (walked, not expanded, to keep the compile-time
// work small: clang stops a constant evaluation at 1,048,576 steps) are well formed, all inside the stored length, and
// decode to pixels * pixels / 8 bytes in all, with no run or byte left over. This is PackedReader's rule, checked
// without it, and GameIconBlitTest checks the two agree.
constexpr bool wellFormed(const uint8_t* bitmap, const int pixels) {
  size_t pos = PACKED_LENGTH_BYTES;
  const size_t end = PACKED_LENGTH_BYTES + packedLength(bitmap);
  size_t decoded = 0;
  while (pos < end) {
    const uint8_t control = bitmap[pos++];
    if (control == 128) return false;
    if (control < 128) {
      const size_t count = static_cast<size_t>(control) + 1;
      if (count > end - pos) return false;
      pos += count;
      decoded += count;
    } else {
      if (pos >= end) return false;
      ++pos;
      decoded += 257 - static_cast<size_t>(control);
    }
  }
  return decoded == static_cast<size_t>(pixels) * static_cast<size_t>(pixels) / 8;
}

constexpr bool allWellFormed() {
  for (size_t i = 0; i < ICON_COUNT; ++i) {
    for (size_t w = 0; w < WEIGHT_COUNT; ++w) {
      if (!wellFormed(ICONS[i].small[w], SMALL_PIXELS) || !wellFormed(ICONS[i].medium[w], MEDIUM_PIXELS)) return false;
    }
  }
  return true;
}

}  // namespace detail

// The drawn sizes in pixels, indexed like ch.gfx's size names (small, medium,
// large). Large is the medium bitmap with each pixel drawn as a 2 x 2 block.
inline constexpr int DRAWN_PIXELS[] = {SMALL_PIXELS, MEDIUM_PIXELS, 2 * MEDIUM_PIXELS};

static_assert(ICON_COUNT > 0, "the icon library has no icons");
static_assert(detail::strictlySorted(), "ICONS must be sorted by name, bytewise, with no name twice");
static_assert(detail::allWellFormed(), "every icon bitmap must be well-formed PackBits, exactly its rows");

// The index into ICONS of the icon named by `length` bytes at `name` (no NUL
// needed), or -1 when no icon has that name. A binary search over ICONS.
constexpr int find(const char* name, const size_t length) {
  size_t low = 0;
  size_t high = ICON_COUNT;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    const int order = detail::compare(name, length, ICONS[middle].name);
    if (order == 0) return static_cast<int>(middle);
    if (order < 0) {
      high = middle;
    } else {
      low = middle + 1;
    }
  }
  return -1;
}

}  // namespace GameIcons
