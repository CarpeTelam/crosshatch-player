// GameArena (src/games/GameArena.cpp) with one difference: the reserve's size is
// fakearena::reserveBytes, so a test can starve the Session or the codec scratch (and the OOM
// line reports the size actually asked for). The rest is the real file's, line for line; the
// real one is not built in this suite.

#if FREEINK_CAP_GAMES

#include <Logging.h>

#include "ArenaSize.h"
#include "GameArena.h"

bool GameArena::allocate() {
  const size_t bytes = GameScript::LUA_REGION_BYTES + fakearena::reserveBytes;
  block = HalMemory::allocatePsram(bytes);
  if (!block) {
    LOG_ERR("GAME", "OOM: %u byte PSRAM arena", static_cast<unsigned>(bytes));
    arena.reset(nullptr, 0);
    return false;
  }
  arena.split(block.get(), GameScript::LUA_REGION_BYTES, fakearena::reserveBytes);
  return true;
}

void GameArena::release() {
  arena.reset(nullptr, 0);
  block.reset();
}

#endif  // FREEINK_CAP_GAMES
