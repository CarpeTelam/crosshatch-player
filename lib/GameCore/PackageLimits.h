#pragma once

#include <cstddef>

namespace GameCore {

// A .cpgame package's limits (spine AD-15, docs/crosshatch/formats.md, docs/crosshatch/api-level-1.txt).
// scripts/pack_game.py refuses a package over any of them with the same numbers, and the installer
// (src/games/GamePackageInstaller) rejects one: the package and member byte limits before it extracts
// (a member's declared size) and while it streams (output past that size), the member count and the
// name rules, the images' budget, and the icon size. test/game_core/PackageLimitsTest.cpp checks each
// constant against package_vectors.json. The converted-image limits, IMAGES_BYTES and MAX_IMAGES, are in
// GameImages.h: they count the .bmp files the installer writes.

// The whole .cpgame file.
inline constexpr size_t PACKAGE_BYTES = 256 * 1024;
// Members of the zip, folders included.
inline constexpr size_t PACKAGE_MEMBERS = 32;
// One member, uncompressed.
inline constexpr size_t MEMBER_BYTES = 128 * 1024;
// The stem of a .lua or .png member name: [a-z0-9_]{1,32}.
inline constexpr size_t MEMBER_STEM_BYTES = 32;
// Room for a member name as stored, ".lua" or ".png" included and the terminating NUL too;
// "manifest.json" (13 bytes) is shorter than the longest stem plus an extension.
inline constexpr size_t MEMBER_NAME_BYTES = MEMBER_STEM_BYTES + 4 + 1;
// The widest and tallest .png member, in pixels: the converter's own limits, which the installer
// checks before it converts, so that a picture it cannot convert is invalid and not a card fault.
inline constexpr size_t IMAGE_MAX_WIDTH = 2048;
inline constexpr size_t IMAGE_MAX_HEIGHT = 3072;
// The side, in pixels, of the icon.bmp the installer writes from icon.png.
inline constexpr int ICON_PIXELS = 64;

}  // namespace GameCore
