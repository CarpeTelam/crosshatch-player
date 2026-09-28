#pragma once

#include <cstddef>

// Where installed games and their saved data live on the SD card, for the Games
// list (manifests), GameAssets (sources and images), and GameSaveStore (store.bin),
// so they never disagree.
namespace GamePaths {

// One folder per installed game, named by its id.
inline constexpr const char* GAMES_DIR = "/.games";
// Room for a path in a game's folder: GAMES_DIR, the id, and a file name such as
// "/manifest.json".
inline constexpr size_t PATH_BYTES = 96;

// One folder per game that has saved data, named by its id (GameSaveStore).
inline constexpr const char* GAMES_DATA_DIR = "/.games-data";
// Room for a path in a game's data folder: GAMES_DATA_DIR, the id, and a file name
// up to "/store.bin.tmp" (GameSaveStore.cpp checks the longest).
inline constexpr size_t DATA_PATH_BYTES = 64;

}  // namespace GamePaths
