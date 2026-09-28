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
// GfxRenderer::drawIcon, so renaming or dropping game-controller fails that file's build. This folder's
// .clang-format turns formatting off for the generated data, so this file is kept in the repository's style by hand.

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

static_assert(ICON_COUNT > 0, "the icon library has no icons");
static_assert(detail::strictlySorted(), "ICONS must be sorted by name, bytewise, with no name twice");

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
