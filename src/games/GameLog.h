#pragma once

#include <IGameLog.h>
#include <Manifest.h>

// The IGameLog provider: each ch.log or print line as LOG_INF tagged with the
// game id (spine Consistency Conventions, Logging).
class GameLog final : public GameCore::IGameLog {
 public:
  explicit GameLog(const char* gameId);
  void write(const char* line) override;

 private:
  char id[GameCore::Manifest::MAX_ID_BYTES + 1] = {};
};
