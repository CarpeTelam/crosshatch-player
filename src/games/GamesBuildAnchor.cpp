#if FREEINK_CAP_GAMES

// Build anchor for the game libraries (architecture AD-2). PlatformIO's default
// `chain` dependency finder follows these includes without evaluating the #if, so
// every env, C3 included, compiles lib/GameCore, lib/GameScript, and lib/GameIcons;
// the linker drops them where nothing references them. Envs using `deep+` (the
// simulator) evaluate the #if and build them only where FREEINK_CAP_GAMES is set.
// Keep this file free of definitions; it exists only for its includes.
#include <GameCore.h>
#include <GameIcons.h>
#include <GameScript.h>

#endif  // FREEINK_CAP_GAMES
