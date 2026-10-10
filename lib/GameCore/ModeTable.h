#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Manifest.h"
#include "Roster.h"

namespace GameCore {

// One mode of a game, written once: its Mode, its Manifest::Mode bit, and its name (the spelling manifests, ctx.mode,
// and the log use).
struct ModeRow {
  Mode mode;
  uint8_t bit;
  const char* name;
};

// Every mode, in the order each list of them follows: solo, pass, nearby. A new mode is a Mode enumerator (Roster.h), a
// Manifest::Mode bit, and one row here; one string on the screen (GameModeActivity's MODE_TEXTS, whose static_assert
// says so) with its key in english.yaml; and the per-mode behaviour the compiler's switch warnings point at.
inline constexpr ModeRow MODE_TABLE[] = {
    {Mode::Solo, Manifest::MODE_SOLO, "solo"},
    {Mode::Pass, Manifest::MODE_PASS, "pass"},
    {Mode::Nearby, Manifest::MODE_NEARBY, "nearby"},
};
inline constexpr size_t MODE_COUNT = sizeof(MODE_TABLE) / sizeof(MODE_TABLE[0]);

// The row of `mode`; nullptr for a value that is no Mode.
constexpr const ModeRow* modeRow(const Mode mode) {
  for (const ModeRow& row : MODE_TABLE)
    if (row.mode == mode) return &row;
  return nullptr;
}

// The row whose bit is exactly `bit`; nullptr for 0, several bits, or a bit no mode has.
constexpr const ModeRow* modeRowForBit(const uint8_t bit) {
  for (const ModeRow& row : MODE_TABLE)
    if (row.bit == bit) return &row;
  return nullptr;
}

// The row named exactly `name`; nullptr for any other text.
constexpr const ModeRow* modeRowForName(const std::string_view name) {
  for (const ModeRow& row : MODE_TABLE)
    if (name == row.name) return &row;
  return nullptr;
}

namespace detail {
constexpr uint8_t allModeBits() {
  uint8_t bits = 0;
  for (const ModeRow& row : MODE_TABLE) bits = static_cast<uint8_t>(bits | row.bit);
  return bits;
}
constexpr bool bitsAreDistinctSingles() {
  uint8_t seen = 0;
  for (const ModeRow& row : MODE_TABLE) {
    if (row.bit == 0 || (row.bit & (row.bit - 1)) != 0 || (seen & row.bit) != 0) return false;
    seen = static_cast<uint8_t>(seen | row.bit);
  }
  return true;
}
}  // namespace detail

// Every Manifest::Mode bit set: what a manifest's modes may hold.
inline constexpr uint8_t ALL_MODE_BITS = detail::allModeBits();

static_assert(detail::bitsAreDistinctSingles(), "each mode has its own single Manifest::Mode bit");
// Tied to the enum's last enumerator: a mode added after it without a row here fails the build, as the old modeName
// switch's -Wswitch warning flagged it.
static_assert(static_cast<size_t>(Mode::Nearby) + 1 == MODE_COUNT, "MODE_TABLE has a row for each Mode enumerator");

}  // namespace GameCore
