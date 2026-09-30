#pragma once

#include <Manifest.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "GameHash.h"

// The installed games (spine AD-16): the /.games/<id>/ folders that hold a valid .pkg and
// a manifest.json that parses and names the same id. There is no separate index, so a
// folder that loses its .pkg (or is removed) is no longer a game.
class GameRegistry {
 public:
  // Games listed at most: a fixed cap, so the listing is sized once.
  static constexpr size_t MAX_GAMES = 64;

  struct Entry {
    GameCore::Manifest manifest;
    // Manifest::check against this host: Unavailable games are listed so the launcher can mark them.
    GameCore::CheckResult check;
    // The package hash from .pkg, which resume.bin records to tell a changed package.
    uint8_t pkgHash[GamePkg::HASH_BYTES] = {};
  };

  struct Listing {
    // One fixed array sized to the folders found, `count` of them filled.
    std::unique_ptr<Entry[]> entries;
    size_t count = 0;
  };

  // Fills `out` with the installed games, sorted by name (then id); false when memory ran out.
  // A card with no /.games is an empty listing.
  static bool load(Listing& out);

  // The package hash in /.games/<id>/.pkg; false when the file is missing or not a valid .pkg.
  static bool readPackageHash(const char* id, uint8_t (&hash)[GamePkg::HASH_BYTES]);
};
