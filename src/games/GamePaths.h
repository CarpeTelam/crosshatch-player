#pragma once

#include <cstddef>

// Where installed games and their saved data live on the SD card, for the Games
// list (manifests), GameAssets (sources and images), GameSaveStore (store.bin), and
// GamePackageInstaller (the inbox and its scratch folder), so they never disagree.
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

// The inbox: a person puts `<name>.cpgame` files here (web file manager or USB) and
// opening Games installs them (GamePackageInstaller).
inline constexpr const char* INBOX_DIR = "/games";
// Room for a file name in the inbox, ".cpgame" included and the terminating NUL too.
inline constexpr size_t INBOX_NAME_BYTES = 64;
// Room for an inbox path with ".bad" appended: INBOX_DIR, "/", the longest name.
inline constexpr size_t INBOX_PATH_BYTES = 80;

// Where a package is extracted (as /.games-tmp/<id>/) before it replaces
// /.games/<id>/; the installer deletes the whole folder when it starts and ends.
inline constexpr const char* TMP_DIR = "/.games-tmp";

// The commit marker in an installed game's folder, written last (docs/crosshatch/formats.md).
inline constexpr const char* PKG_NAME = ".pkg";

}  // namespace GamePaths
