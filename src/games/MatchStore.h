#pragma once

#include <HalMemory.h>
#include <StoreSlot.h>

#include <cstdint>
#include <memory>

#include "GameSaveStore.h"

// ch.store for one match (AD-17), so the match's screen names no GameScript type:
// one PSRAM block holding the latest-wins slot (Codec::STORE_LIMIT bytes) the VM
// posts to, then the buffer of the GameSaveStore that restores the slot from
// store.bin and writes it back. The slot must outlive the VM that posts to it, so
// the match declares this before its GameVM. Loop task; the VM task reaches the
// slot only through the reference GameVM::create takes.
class MatchStore {
 public:
  // Allocates the block, the slot, and the GameSaveStore of `gameId` (a valid
  // manifest id); `startMs` (millis()) starts the flush interval. False when memory
  // runs out; the caller logs it.
  bool allocate(const char* gameId, uint32_t startMs);
  // allocate() succeeded; every call below needs it.
  bool ready() const { return slotPtr && saver; }

  GameScript::StoreSlot& slot() { return *slotPtr; }
  GameSaveStore& saves() { return *saver; }
  // Each loop pass while the VM runs (GameSaveStore::flushIfDue).
  void flushIfDue(uint32_t nowMs) { saver->flushIfDue(*slotPtr, nowMs); }
  // At round end, on Leave, and in onExit (GameSaveStore::flush).
  void flush(uint32_t nowMs) { saver->flush(*slotPtr, nowMs); }
  // For a VM task that may still post to the slot (GameVM::abandon returned
  // false): the slot and the block stay allocated for good, and only the
  // GameSaveStore is freed with this object.
  void leak();

 private:
  HalMemory::PsramBuffer storage;
  std::unique_ptr<GameScript::StoreSlot> slotPtr;
  std::unique_ptr<GameSaveStore> saver;
};
