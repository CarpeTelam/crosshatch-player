#pragma once

// The hostile link every nearby-play suite shares (gtest-free and header-only, so the harness suites reach it).
//
// Adapted from crossplay 0a1d17f host-tests/link/FakeLink.h (MIT).
// Copyright (c) 2025 CrossPoint Reader organization; Copyright (c) 2026 Mario Ruiz (CrossPlay).
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Stands in for: EspNowLink (device) and UdpLink (simulator) under the GameCore::ILink contract. LinkContract.h
// pins that this double and the contract agree; EspNowLink sits outside the host harness, so its agreement is
// checked on the device (entry 11).
//
// More permissive than the device: a send queue that fills only when a test says so (FakeLink::refuseSends refuses
// the next n calls by count; the device's queue fills and drains over time), no peer-table limit, no 250 B v1
// truncation, no radio time (delay is virtual, set by the test's ManualClock), no channel or range (every endpoint of
// the net hears every other), and a unicast works to any endpoint of the net once the sender has a handle for it
// (FakeLink::handleOf). Stricter than the device: handles are tagged with their owning endpoint, so a handle used on a
// link other than the one that gave it is caught and counted.
//
// What it adds over a kind link: a seeded xorshift32 that drops, duplicates, delays, and reorders per copy, a
// broadcast expanded to one copy per other endpoint (each with its own rolls, in endpoint order), a counter of
// every copy carried (a packet storm is invisible to correctness assertions and shows up only as traffic),
// and a blackhole for one-way range.
//
// Repeatability: a submitted frame draws one duplicate roll, then a loss, a jitter, and a reorder roll for each
// copy, all always drawn. So the loss, jitter, delay, and reorder knobs never change the number of rolls, and
// changing one does not reshuffle the others. Three things do change it: the duplicate roll (a duplicate is a
// second copy, so a second set of rolls), a blackholed source (it draws none), and ended receivers (a broadcast
// skips them). A seed repeats exactly when one thread drives the net.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "IClock.h"
#include "ILink.h"
#include "Protocol.h"

namespace GameCore::LinkTest {

// A clock the test advances by hand; shared by the link and the code under test.
class ManualClock final : public IClock {
 public:
  uint64_t nowMs() const override { return ms.load(); }
  void advance(const uint64_t deltaMs) { ms.fetch_add(deltaMs); }
  void set(const uint64_t value) { ms.store(value); }

 private:
  std::atomic<uint64_t> ms{0};
};

// Percentages are 0..100. A reordered copy is held back by reorderExtraMs, so frames sent after it overtake it.
struct Faults {
  int lossPercent = 0;
  int duplicatePercent = 0;
  int reorderPercent = 0;
  uint32_t delayMs = 0;
  uint32_t jitterMs = 0;  // each copy adds 0..jitterMs
  uint32_t reorderExtraMs = 0;
};

// A tag for each net, so a handle from one net is never valid on another (ILink.h: handle values are unique
// across link instances). Test-only state; wraps after 1023 nets, which only matters to two nets alive together.
inline uint32_t nextNetId() {
  static std::atomic<uint32_t> counter{0};
  return counter.fetch_add(1) % 1023 + 1;
}

class FakeLinkNet {
 public:
  explicit FakeLinkNet(const IClock& clockRef, const uint32_t seed = 1)
      : clock(clockRef), netId(nextNetId()), random(seed != 0 ? seed : FALLBACK_SEED) {}
  FakeLinkNet(const FakeLinkNet&) = delete;
  FakeLinkNet& operator=(const FakeLinkNet&) = delete;

  // Faults for the whole net.
  void setFaults(const Faults& faults) {
    std::lock_guard<std::mutex> lock(mutex);
    everywhere = faults;
  }
  // Faults for the frames endpoint `from` sends to endpoint `to` (one direction); they replace the net's.
  void setFaults(const size_t from, const size_t to, const Faults& faults) {
    std::lock_guard<std::mutex> lock(mutex);
    for (DirectionalFaults& d : directional) {
      if (d.from == from && d.to == to) {
        d.faults = faults;
        return;
      }
    }
    directional.push_back({from, to, faults});
  }
  // Drops everything endpoint `source` sends; its send() still says true (one-way range).
  void blackhole(const size_t source, const bool on = true) {
    std::lock_guard<std::mutex> lock(mutex);
    if (blackholed.size() <= source) blackholed.resize(source + 1, false);
    blackholed[source] = on;
  }

  // Copies queued, duplicates and per-receiver broadcast copies each counted.
  uint64_t carried() const { return read(carriedCount); }
  // Copies the net lost (loss roll, blackhole, or a receiver that had ended).
  uint64_t dropped() const { return read(droppedCount); }
  // send() and broadcast() calls that returned false.
  uint64_t refusedSends() const { return read(refusedCount); }
  // The subset of refusedSends() made with a handle this link never gave.
  uint64_t foreignHandleSends() const { return read(foreignCount); }
  // Frames a receiver dropped because its buffer was too small.
  uint64_t droppedForCapacity() const { return read(capacityCount); }
  // Copies queued and not yet taken.
  size_t waiting() const {
    std::lock_guard<std::mutex> lock(mutex);
    return queue.size();
  }
  // A packet storm becomes a failing assertion: overBudget() is true once carried() passes the budget.
  void setCarriedBudget(const uint64_t budget) {
    std::lock_guard<std::mutex> lock(mutex);
    carriedBudget = budget;
    hasBudget = true;
  }
  bool overBudget() const {
    std::lock_guard<std::mutex> lock(mutex);
    return hasBudget && carriedCount > carriedBudget;
  }

 private:
  friend class FakeLink;

  static constexpr uint32_t FALLBACK_SEED = 0x9E3779B9u;

  struct DirectionalFaults {
    size_t from;
    size_t to;
    Faults faults;
  };
  struct Copy {
    size_t from = 0;
    size_t to = 0;
    std::vector<uint8_t> data;
    uint64_t dueMs = 0;
    uint64_t order = 0;
  };

  uint64_t read(const uint64_t& counter) const {
    std::lock_guard<std::mutex> lock(mutex);
    return counter;
  }

  uint32_t roll() {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  }
  bool percentRoll(const int percent) { return static_cast<int>(roll() % 100) < percent; }

  size_t addEndpoint() {
    std::lock_guard<std::mutex> lock(mutex);
    endedFlags.push_back(false);
    return endedFlags.size() - 1;
  }
  bool isEnded(const size_t endpoint) const {
    std::lock_guard<std::mutex> lock(mutex);
    return endedFlags[endpoint];
  }
  void endEndpoint(const size_t endpoint) {
    std::lock_guard<std::mutex> lock(mutex);
    endedFlags[endpoint] = true;
    for (size_t i = 0; i < queue.size();) {
      if (queue[i].to == endpoint) {
        queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        i++;
      }
    }
  }
  void countRefused(const bool foreign) {
    std::lock_guard<std::mutex> lock(mutex);
    refusedCount++;
    if (foreign) foreignCount++;
  }

  // Caller holds the mutex.
  const Faults& faultsFor(const size_t from, const size_t to) const {
    for (const DirectionalFaults& d : directional) {
      if (d.from == from && d.to == to) return d.faults;
    }
    return everywhere;
  }

  // Caller holds the mutex. Rolls and queues the copies of one frame from `from` to `to`.
  void submitLocked(const size_t from, const size_t to, const std::span<const uint8_t> frame) {
    if (from < blackholed.size() && blackholed[from]) {
      droppedCount++;
      return;
    }
    const Faults faults = faultsFor(from, to);
    const int copies = percentRoll(faults.duplicatePercent) ? 2 : 1;
    for (int i = 0; i < copies; i++) {
      const bool lost = percentRoll(faults.lossPercent);
      const uint32_t jitterRoll = roll();  // always drawn, so a zero jitterMs does not shift later rolls
      const uint32_t jitter = faults.jitterMs > 0 ? jitterRoll % (uint64_t{faults.jitterMs} + 1) : 0;
      const bool reordered = percentRoll(faults.reorderPercent);
      if (lost || endedFlags[to]) {
        droppedCount++;
        continue;
      }
      Copy copy;
      copy.from = from;
      copy.to = to;
      copy.data.assign(frame.begin(), frame.end());
      copy.dueMs = clock.nowMs() + faults.delayMs + jitter + (reordered ? faults.reorderExtraMs : 0);
      copy.order = nextOrder++;
      queue.push_back(std::move(copy));
      carriedCount++;
    }
  }

  void submit(const size_t from, const size_t to, const std::span<const uint8_t> frame) {
    std::lock_guard<std::mutex> lock(mutex);
    submitLocked(from, to, frame);
  }

  void submitBroadcast(const size_t from, const std::span<const uint8_t> frame) {
    std::lock_guard<std::mutex> lock(mutex);
    for (size_t to = 0; to < endedFlags.size(); to++) {
      if (to == from || endedFlags[to]) continue;
      submitLocked(from, to, frame);
    }
  }

  // Takes the oldest due copy for `to` that fits `buffer`; copies too long for it are dropped, never truncated.
  bool take(const size_t to, size_t& from, const std::span<uint8_t> buffer, size_t& length) {
    std::lock_guard<std::mutex> lock(mutex);
    const uint64_t now = clock.nowMs();
    for (;;) {
      size_t best = queue.size();
      for (size_t i = 0; i < queue.size(); i++) {
        const Copy& c = queue[i];
        if (c.to != to || c.dueMs > now) continue;
        if (best == queue.size() || c.dueMs < queue[best].dueMs ||
            (c.dueMs == queue[best].dueMs && c.order < queue[best].order)) {
          best = i;
        }
      }
      if (best == queue.size()) return false;
      Copy copy = std::move(queue[best]);
      queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(best));
      if (copy.data.size() > buffer.size()) {
        capacityCount++;
        continue;
      }
      for (size_t i = 0; i < copy.data.size(); i++) buffer[i] = copy.data[i];
      from = copy.from;
      length = copy.data.size();
      return true;
    }
  }

  const IClock& clock;
  const uint32_t netId;  // 1..1023, unique among the nets alive in the process; tags every handle
  mutable std::mutex mutex;
  uint32_t random;
  Faults everywhere;
  std::vector<DirectionalFaults> directional;
  std::vector<bool> blackholed;
  std::vector<bool> endedFlags;
  std::vector<Copy> queue;
  uint64_t nextOrder = 0;
  uint64_t carriedCount = 0;
  uint64_t droppedCount = 0;
  uint64_t refusedCount = 0;
  uint64_t foreignCount = 0;
  uint64_t capacityCount = 0;
  uint64_t carriedBudget = 0;
  bool hasBudget = false;
};

// One endpoint of a FakeLinkNet. Not thread-safe, as ILink says; the net is.
class FakeLink final : public ILink {
 public:
  explicit FakeLink(FakeLinkNet& netRef) : net(netRef), self(netRef.addEndpoint()) {}
  // The net must outlive its links: a destroyed link ends, so copies sent to it are not queued for nobody.
  ~FakeLink() override { end(); }
  FakeLink(const FakeLink&) = delete;
  FakeLink& operator=(const FakeLink&) = delete;

  size_t index() const { return self; }

  // Test-only: the handle this link uses for `peer`, made at once, as if a frame from it had been received.
  // The same peer always gives the same handle. NO_PEER once this link has ended, or for itself.
  PeerHandle handleOf(const FakeLink& peer) {
    if (done || &peer == this || &peer.net != &net) return NO_PEER;
    return handleForPeer(peer.self);
  }

  // Test-only: stands in for the device's full send queue (ILink.h: send and broadcast return false, nothing sent,
  // "its send queue is full"; esp_now_send failing, or the adapter's queue full). Refuses the next `count` calls
  // that would otherwise succeed, send and broadcast alike; a call the checks below already refuse does not use
  // one up. More permissive than the device: the refusal is by count, not by a queue that fills and drains over
  // time. A refused call counts in the net's refusedSends(), queues nothing, and draws no roll. FakeLinkTest pins
  // that the two agree.
  void refuseSends(const size_t count) { refusalsLeft = count; }

  bool send(const PeerHandle to, const std::span<const uint8_t> frame) override {
    if (done || !to.valid() || !sizeOk(frame)) return refuse(false);
    const Entry* const entry = entryFor(to);
    if (entry == nullptr) return refuse(true);  // not a handle this link gave: another link's, or made up
    if (queueFull()) return refuse(false);
    net.submit(self, entry->peer, frame);
    return true;
  }

  bool broadcast(const std::span<const uint8_t> frame) override {
    if (done || !sizeOk(frame)) return refuse(false);
    if (queueFull()) return refuse(false);
    net.submitBroadcast(self, frame);
    return true;
  }

  bool receive(PeerHandle& from, const std::span<uint8_t> buffer, size_t& length) override {
    if (done) return false;
    size_t peer = 0;
    size_t got = 0;
    if (!net.take(self, peer, buffer, got)) return false;
    from = handleForPeer(peer);
    length = got;
    return true;
  }

  void end() override {
    if (done) return;
    done = true;
    entries.clear();
    net.endEndpoint(self);
  }

  bool ended() const { return done; }

 private:
  struct Entry {
    size_t peer;
    uint32_t value;
  };

  static bool sizeOk(const std::span<const uint8_t> frame) {
    return !frame.empty() && frame.size() <= Protocol::MAX_FRAME_BYTES;
  }
  // True while refuseSends has calls left; uses one up.
  bool queueFull() {
    if (refusalsLeft == 0) return false;
    refusalsLeft--;
    return true;
  }
  bool refuse(const bool foreign) {
    net.countRefused(foreign);
    return false;
  }

  // Handle value: the net's tag in bits 22..31, (endpoint + 1) in bits 16..21, a per-link counter (never reused)
  // in the low half.
  PeerHandle handleForPeer(const size_t peer) {
    for (const Entry& e : entries) {
      if (e.peer == peer) return PeerHandle::fromValue(e.value);
    }
    const uint32_t value = (net.netId << 22) | (static_cast<uint32_t>(self + 1) << 16) | ++counter;
    entries.push_back({peer, value});
    return PeerHandle::fromValue(value);
  }
  const Entry* entryFor(const PeerHandle handle) const {
    for (const Entry& e : entries) {
      if (e.value == handle.value()) return &e;
    }
    return nullptr;
  }

  FakeLinkNet& net;
  const size_t self;
  bool done = false;
  uint32_t counter = 0;
  size_t refusalsLeft = 0;
  std::vector<Entry> entries;
};

}  // namespace GameCore::LinkTest
