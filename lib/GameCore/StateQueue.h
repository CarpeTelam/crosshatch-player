#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "IGameRules.h"

// The nearby match's queue of the peer's STATEs between the GameLink task (which pushes) and the VM task (which takes
// and adopts them), spine AD-18 and R5. Pure policy over an injected IStateStore: no allocator call, no log, no static
// state; every snapshot byte it holds is the store's.
namespace GameCore {

using StateHandle = uint8_t;                          // an index into the store: 0..capacity() - 1
inline constexpr StateHandle NO_STATE_HANDLE = 0xFF;  // capacity() is at most 255

// Port: a fixed pool of snapshot buffers for the state queue. The device adapter (entry 8) allocates it once at
// bring-up in PSRAM (never on the Wi-Fi task, never per frame); the host suites pass an array. Not thread-safe:
// whoever shares a StateQueue serializes every call on it and on its store.
class IStateStore {
 public:
  virtual ~IStateStore() = default;
  virtual size_t capacity() const = 0;      // buffers in the pool; fixed for the store's life
  virtual StateHandle acquire() = 0;        // a free buffer, or NO_STATE_HANDLE when none is free
  virtual void release(StateHandle h) = 0;  // returns a buffer; NO_STATE_HANDLE and a free one are no-ops
  // The buffer's Protocol::MAX_SNAPSHOT_BYTES (1,400) bytes; empty for NO_STATE_HANDLE or a free handle.
  virtual std::span<uint8_t> bytes(StateHandle h) = 0;
};

// One queued STATE; its snapshot is in the store under `handle`.
struct QueuedState {
  StateHandle handle = NO_STATE_HANDLE;
  uint8_t fromSeat = 0;  // the sender's seat, from its link identity (MatchSink)
  uint16_t ver = 0;
  Status status;
  uint16_t length = 0;
};

// Threading: the GameLink task calls push() and the VM task calls take(), bytes(), release(), and size(), but the queue
// takes no lock and its fields are plain: whoever shares one serializes every call on it and on its store (the
// cross-task handoff is entry 8's). EndSlot, by contrast, is lock-free.
class StateQueue {
 public:
  static constexpr size_t DEPTH = 4;  // R5
  enum class Push : uint8_t { Queued, Replaced, Full, Invalid };

  explicit StateQueue(IStateStore& store);
  ~StateQueue();  // releases every handle it holds
  StateQueue(const StateQueue&) = delete;
  StateQueue& operator=(const StateQueue&) = delete;

  bool ok() const;  // store.capacity() >= DEPTH + 1 (four queued, one being adopted)
  // GameLink task. Queued: appended. Replaced: the newest queued STATE did not end a round and came from the same
  // seat, so its buffer takes the new one (latest wins). Full: DEPTH entries and the newest ends a round (or is
  // another seat's), or the store had no buffer: nothing changed, overflows() + 1. Invalid: empty or over 1,400 bytes.
  Push push(uint8_t fromSeat, uint16_t ver, const Status& status, std::span<const uint8_t> snapshot);
  // VM task. The oldest entry, removed: the queue forgets it and the caller owns out.handle until release().
  bool take(QueuedState& out);
  std::span<const uint8_t> bytes(const QueuedState& taken);  // the snapshot, `length` bytes
  void release(QueuedState& taken);                          // returns the buffer; taken.handle = NO_STATE_HANDLE
  size_t size() const { return count; }                      // queued, not counting a taken entry
  uint32_t overflows() const { return overflowCount; }       // pushes that found it full, since construction
  void clear();                                              // teardown: releases every queued handle

 private:
  QueuedState& at(size_t index) { return entries[(first + index) % DEPTH]; }

  IStateStore& store;
  QueuedState entries[DEPTH];
  size_t first = 0;
  size_t count = 0;
  uint32_t overflowCount = 0;
};

}  // namespace GameCore
