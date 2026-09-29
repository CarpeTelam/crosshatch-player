#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>

// The hand-off of committed snapshots from the VM task to the loop task (AD-5): the
// VM never touches Storage, so it publishes each snapshot it commits here, and the
// loop task takes the latest and writes resume.bin (GameSaveStore::flushResume).
// Latest wins: a snapshot published before the last one was taken replaces it, so
// the loop writes every snapshot a pass sees, not every one committed. Thread-safe;
// each call holds the mutex only to compare or copy. Over caller storage (a block
// the VM owns), so it holds no allocation of its own.
class SnapshotMailbox {
 public:
  // What take() reports about the copy it made.
  struct Taken {
    size_t length = 0;
    uint32_t ver = 0;
    // The snapshot's status is over: the round has ended, so it is never saved.
    bool over = false;
  };

  explicit SnapshotMailbox(const std::span<uint8_t> storage) : bytes(storage) {}
  SnapshotMailbox(const SnapshotMailbox&) = delete;
  SnapshotMailbox& operator=(const SnapshotMailbox&) = delete;

  // VM task: replaces the held snapshot and marks it pending. False, changing
  // nothing, when `snapshot` is empty or does not fit.
  bool publish(const std::span<const uint8_t> snapshot, const uint32_t ver, const bool over) {
    if (snapshot.empty() || snapshot.size() > bytes.size()) return false;
    std::lock_guard<std::mutex> lock(mutex);
    std::memcpy(bytes.data(), snapshot.data(), snapshot.size());
    held.length = snapshot.size();
    held.ver = ver;
    held.over = over;
    waiting = true;
    return true;
  }

  // Loop task: when a snapshot is pending, copies it into `out`, clears pending, and
  // returns true. False, changing nothing, when none is pending or `out` is too small.
  bool take(const std::span<uint8_t> out, Taken& taken) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!waiting || out.size() < held.length) return false;
    std::memcpy(out.data(), bytes.data(), held.length);
    taken = held;
    waiting = false;
    return true;
  }

  // A snapshot has been published and not taken (since markPending).
  bool pending() const {
    std::lock_guard<std::mutex> lock(mutex);
    return waiting;
  }

  // Marks the held snapshot pending again after a taken copy failed to reach the SD
  // card, so a later flush retries with the latest one. No-op when nothing was published.
  void markPending() {
    std::lock_guard<std::mutex> lock(mutex);
    if (held.length != 0) waiting = true;
  }

  // The bytes the snapshots are copied into. The owner may seed them before the VM
  // task starts (GameVM::setResume); nothing else reads them.
  std::span<uint8_t> storage() { return bytes; }

 private:
  mutable std::mutex mutex;
  std::span<uint8_t> bytes;
  Taken held;
  bool waiting = false;
};
