#pragma once

#include <GameSources.h>
#include <HalMemory.h>

#include <cstddef>

// Loads a game's files from /.games/<id>/ before its VM starts (AD-5): every
// `[a-z0-9_]{1,32}.lua` goes into one PSRAM block, a span table followed by the
// text, and is handed to the VM as GameScript::GameSources. Runs on the loop task;
// the VM never touches Storage. Move-only, since it owns the block.
class GameAssets {
 public:
  // AD-15 package limits, applied to what a hand-placed game holds.
  static constexpr size_t MAX_SOURCES = 32;
  static constexpr size_t MAX_SOURCE_BYTES = 256 * 1024;

  // Null on success, otherwise a short reason (already logged).
  const char* load(const char* gameId);
  const GameScript::GameSources& sources() const { return view; }

 private:
  HalMemory::PsramBuffer block;
  GameScript::GameSources view;
};
