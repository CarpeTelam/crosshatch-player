#if FREEINK_CAP_GAMES

#include "GameArena.h"

#include <Logging.h>

bool GameArena::allocate() {
  block = HalMemory::allocatePsram(GameScript::ARENA_BYTES);
  if (!block) {
    LOG_ERR("GAME", "OOM: %u byte PSRAM arena", static_cast<unsigned>(GameScript::ARENA_BYTES));
    arena.reset(nullptr, 0);
    return false;
  }
  arena.reset(block.get(), GameScript::ARENA_BYTES);
  return true;
}

void GameArena::release() {
  arena.reset(nullptr, 0);
  block.reset();
}

#endif  // FREEINK_CAP_GAMES
