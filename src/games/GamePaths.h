#pragma once

#include <cstddef>

// Where installed games live on the SD card, for the Games list (manifests) and
// GameAssets (sources and images), so the two never disagree.
namespace GamePaths {

// One folder per installed game, named by its id.
inline constexpr const char* GAMES_DIR = "/.games";
// Room for a path in a game's folder: GAMES_DIR, the id, and a file name such as
// "/manifest.json".
inline constexpr size_t PATH_BYTES = 96;

}  // namespace GamePaths
