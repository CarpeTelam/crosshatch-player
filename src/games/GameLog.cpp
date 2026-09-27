#if FREEINK_CAP_GAMES

#include "GameLog.h"

#include <Logging.h>

#include <cstdio>

GameLog::GameLog(const char* gameId) { snprintf(id, sizeof(id), "%s", gameId); }

// LOG_INF compiles to nothing below LOG_LEVEL 1.
void GameLog::write([[maybe_unused]] const char* line) { LOG_INF(id, "%s", line); }

#endif  // FREEINK_CAP_GAMES
