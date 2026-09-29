#pragma once

#include <GameImages.h>
#include <GameSources.h>
#include <HalMemory.h>
#include <PackageLimits.h>

#include <cstddef>
#include <cstdint>

class GameSaveStore;
namespace GameScript {
class StoreSlot;
}

// Loads what a game needs before its VM starts (AD-5): every `[a-z0-9_]{1,32}.lua`
// in /.games/<id>/ goes into one PSRAM block, a span table followed by the text,
// handed to the VM as GameScript::GameSources; every `[a-z0-9_]{1,32}.bmp` except
// icon.bmp goes into the same block as GameCore::GameImages (ch.gfx.image), each
// checked against the converter's 1-bit layout; and ch.store's saved table goes into
// the match's slot, read by GameSaveStore (store.bin's only reader). Runs on the
// loop task; the VM never touches Storage. Move-only, since it owns the block.
class GameAssets {
 public:
  // AD-15 package limits, applied to what a hand-placed game holds.
  static constexpr size_t MAX_SOURCES = 32;
  static constexpr size_t MAX_SOURCE_BYTES = GameCore::LUA_SOURCES_BYTES;

  // Why a load failed; the match shows each as a translated reason (AD-14).
  // BadSourceName: the only .lua files have names no module can have. BadImage: an
  // image is not a converted 1-bit .bmp, or the images pass GameCore's limits.
  enum class LoadResult : uint8_t {
    Ok,
    FolderMissing,
    NoSources,
    BadSourceName,
    TooLarge,
    OutOfMemory,
    CannotRead,
    BadImage
  };

  // Ok, or why not (already logged). Once the sources are loaded, restores the
  // saved store into `store` (empty when there is none or it was discarded).
  LoadResult load(const char* gameId, GameSaveStore& saves, GameScript::StoreSlot& store);
  const GameScript::GameSources& sources() const { return view; }
  const GameCore::GameImages& images() const { return imageView; }
  // Frees the block (GameVM::abandon); sources() and images() are empty afterwards.
  void release() {
    view = GameScript::GameSources{};
    imageView = GameCore::GameImages{};
    block.reset();
  }

 private:
  HalMemory::PsramBuffer block;
  GameScript::GameSources view;
  GameCore::GameImages imageView;
};
