#pragma once

#include <GameSources.h>
#include <HalMemory.h>

#include <cstddef>

class GameSaveStore;
namespace GameScript {
class StoreSlot;
}

// Loads what a game needs before its VM starts (AD-5): every `[a-z0-9_]{1,32}.lua`
// in /.games/<id>/ goes into one PSRAM block, a span table followed by the text,
// handed to the VM as GameScript::GameSources; and ch.store's saved table goes into
// the match's slot, read by GameSaveStore (store.bin's only reader). Runs on the
// loop task; the VM never touches Storage. Move-only, since it owns the block.
class GameAssets {
 public:
  // AD-15 package limits, applied to what a hand-placed game holds.
  static constexpr size_t MAX_SOURCES = 32;
  static constexpr size_t MAX_SOURCE_BYTES = 256 * 1024;

  // Null on success, otherwise a short reason (already logged). Once the sources
  // are loaded, restores the saved store into `store` (empty when there is none or
  // it was discarded).
  const char* load(const char* gameId, GameSaveStore& saves, GameScript::StoreSlot& store);
  const GameScript::GameSources& sources() const { return view; }
  // Frees the block (GameVM::abandon); sources() is empty afterwards.
  void release() {
    view = GameScript::GameSources{};
    block.reset();
  }

 private:
  HalMemory::PsramBuffer block;
  GameScript::GameSources view;
};
