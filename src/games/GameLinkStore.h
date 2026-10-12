#pragma once

#include <HalMemory.h>
#include <Protocol.h>
#include <StateQueue.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// The nearby match's pool of snapshot buffers (spine AD-18, R5; GameCore::IStateStore): one PSRAM block, allocated once
// at bring-up and never per frame, of POOL_BUFFERS buffers for the StateQueue (four queued and one being adopted) and
// one more, the outbound buffer, that NearbySession::ship stages the snapshot to send in. 8,400 B of PSRAM and none of
// internal RAM.
//
// Not thread-safe, as IStateStore says: NearbySession's lock serializes every call (the free mask and the counters are
// plain). The buffer a taken queue entry holds is touched only by the consumer until it is released.
class GameLinkStore final : public GameCore::IStateStore {
 public:
  static constexpr size_t BUFFER_BYTES = GameCore::Protocol::MAX_SNAPSHOT_BYTES;
  static constexpr size_t POOL_BUFFERS = GameCore::StateQueue::DEPTH + 1;
  static constexpr size_t BLOCK_BYTES = (POOL_BUFFERS + 1) * BUFFER_BYTES;

  // Null when the PSRAM block or the object cannot be allocated (the caller logs: it knows the session).
  static std::unique_ptr<GameLinkStore> create();

  explicit GameLinkStore(HalMemory::PsramBuffer block);
  GameLinkStore(const GameLinkStore&) = delete;
  GameLinkStore& operator=(const GameLinkStore&) = delete;

  bool ok() const { return block != nullptr; }
  size_t capacity() const override { return POOL_BUFFERS; }
  GameCore::StateHandle acquire() override;
  // A handle that is free, out of range, or NO_STATE_HANDLE is a no-op; one that is free or out of range is counted.
  void release(GameCore::StateHandle handle) override;
  std::span<uint8_t> bytes(GameCore::StateHandle handle) override;
  // The sixth buffer, BUFFER_BYTES long, never handed out by acquire(); empty when !ok().
  std::span<uint8_t> outbound();

  size_t inUse() const;
  uint32_t badReleases() const { return badReleaseCount; }

 private:
  HalMemory::PsramBuffer block;
  std::array<bool, POOL_BUFFERS> used{};
  uint32_t badReleaseCount = 0;
};
