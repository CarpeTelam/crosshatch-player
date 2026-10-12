#pragma once

// GameCore::ILink over ESP-NOW v2: fixed channel 1, no encryption, brought up in AD-18's order on the station
// interface. Everything radio-specific lives here and in EspNowLink.cpp (a MAC address, the channel, the peer table,
// the Wi-Fi mode); GameCore sees opaque frames and PeerHandles.
//
// The link's logic is a template over a driver (the EspNowDriver concept below), so the host can inject a failure at
// each bring-up step and run the contract over two links on a fake air (EspNowLinkTest.cpp). EspNowLink.cpp holds
// the one real driver, EspIdfDriver, over WiFi, esp_now.h, and esp_wifi.h, and is compiled out of the host harness
// and the simulator; this header includes no radio header.
//
// The bring-up and teardown order follows crossplay's LinkRadio::begin and end (the notice below):
// auto-reconnect off before the disconnect, the disconnect waited out before the channel is set, teardown flagged
// the moment esp_now_init succeeds so every failure path tears down, end() safe to call twice.
//
// Adapted from crossplay 0a1d17f src/apps_local/link/LinkRadio.cpp (MIT).
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

#include <HalMemory.h>
#include <ILink.h>
#include <Logging.h>
#include <Protocol.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "FrameRing.h"

namespace EspNow {

inline constexpr uint8_t CHANNEL = 1;
inline constexpr uint16_t WAKE_WINDOW_MS = 50;
inline constexpr size_t MAC_BYTES = 6;
// 8 outruns the GameLink task's drain at ADVERT and STATE rates, and a full ring costs a resend, not state (R2, R5).
// An estimate until the device run (entry 11).
inline constexpr size_t RING_DEPTH = 8;
inline constexpr size_t MAX_PEERS = 16;  // plus the broadcast peer: under ESP-NOW's 20

struct Mac {
  uint8_t bytes[MAC_BYTES] = {};
};

inline constexpr Mac BROADCAST{{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

// Handle values are unique across every link instance in the process (ILink.h): each instance takes a tag from
// here, 1..0xFFFFFF, and puts it above the peer's slot. A 4 B mutable static, constant-initialized (AD-2).
inline constinit std::atomic<uint32_t> nextInstanceTag{0};

inline uint32_t takeInstanceTag() { return nextInstanceTag.fetch_add(1, std::memory_order_relaxed) % 0xFFFFFFu + 1; }

// The peers a link has heard, over caller memory (MAC_BYTES per entry). It never evicts, so a handle is never
// reused for another peer; when it is full a new peer is not named (counted). Handle value =
// (instance tag << 8) | (slot + 1). Used by the one task that calls the link, never by the receive callback.
class PeerTable {
 public:
  static constexpr size_t MAX_CAPACITY = 255;  // the slot lives in 8 bits, 0 meaning none

  static constexpr size_t storageBytes(const size_t capacity) { return capacity * MAC_BYTES; }

  // False, and the table stays as it was, unless capacity is 1..255, the storage holds it, and the tag is
  // 1..0xFFFFFF. Empties the table and its counter.
  bool attach(uint8_t* storage, const size_t storageSize, const size_t capacity, const uint32_t instanceTag) {
    if (storage == nullptr || capacity < 1 || capacity > MAX_CAPACITY || storageSize < storageBytes(capacity) ||
        instanceTag < 1 || instanceTag > 0xFFFFFFu) {
      return false;
    }
    storage_ = storage;
    capacity_ = capacity;
    tag_ = instanceTag;
    count_ = 0;
    fullDrops_ = 0;
    return true;
  }
  void detach() {
    storage_ = nullptr;
    count_ = 0;
  }

  // The handle of the peer with this MAC, made on first sight. NO_PEER (counted in fullDrops) when the table is
  // full, and NO_PEER, uncounted, when it is not attached or the MAC is null.
  GameCore::PeerHandle findOrAdd(const uint8_t* mac) {
    if (storage_ == nullptr || mac == nullptr) return GameCore::NO_PEER;
    for (size_t slot = 0; slot < count_; slot++) {
      if (std::memcmp(storage_ + slot * MAC_BYTES, mac, MAC_BYTES) == 0) return handleOf(slot);
    }
    if (count_ >= capacity_) {
      fullDrops_++;
      return GameCore::NO_PEER;
    }
    std::memcpy(storage_ + count_ * MAC_BYTES, mac, MAC_BYTES);
    return handleOf(count_++);
  }

  // The MAC this table gave `handle` for; null for NO_PEER, another instance's handle, a slot not used yet, or a
  // table that is not attached.
  const uint8_t* macOf(const GameCore::PeerHandle handle) const {
    if (storage_ == nullptr || !handle.valid()) return nullptr;
    const uint32_t value = handle.value();
    const size_t slotPlusOne = value & 0xFFu;
    if ((value >> 8) != tag_ || slotPlusOne == 0 || slotPlusOne > count_) return nullptr;
    return storage_ + (slotPlusOne - 1) * MAC_BYTES;
  }

  size_t count() const { return count_; }
  // Frames from a new peer that the full table could not name.
  uint32_t fullDrops() const { return fullDrops_; }

 private:
  GameCore::PeerHandle handleOf(const size_t slot) const {
    return GameCore::PeerHandle::fromValue((tag_ << 8) | static_cast<uint32_t>(slot + 1));
  }

  uint8_t* storage_ = nullptr;
  size_t capacity_ = 0;
  uint32_t tag_ = 0;
  size_t count_ = 0;
  uint32_t fullDrops_ = 0;
};

// The host's label, built in the radio layer and carried in ADVERT as opaque bytes: the model text, a space, and the
// MAC's last two bytes as 4 upper-case hex digits ("X4 Pro 3F2A"). The model is cut (on a UTF-8 boundary) so the
// whole is at most Protocol::MAX_HOST_LABEL_BYTES; an empty model gives the hex alone.
struct Label {
  uint8_t bytes[GameCore::Protocol::MAX_HOST_LABEL_BYTES] = {};
  size_t length = 0;
  std::span<const uint8_t> view() const { return std::span<const uint8_t>(bytes, length); }
};

inline Label makeLabel(const std::string_view model, const Mac& mac) {
  constexpr size_t SUFFIX_BYTES = 4;
  constexpr size_t MAX_MODEL_BYTES = GameCore::Protocol::MAX_HOST_LABEL_BYTES - SUFFIX_BYTES - 1;  // minus the space
  size_t modelBytes = model.size() < MAX_MODEL_BYTES ? model.size() : MAX_MODEL_BYTES;
  while (modelBytes > 0 && modelBytes < model.size() && (static_cast<uint8_t>(model[modelBytes]) & 0xC0u) == 0x80u) {
    modelBytes--;  // do not end on half of a multi-byte character
  }
  Label label;
  for (size_t i = 0; i < modelBytes; i++) label.bytes[label.length++] = static_cast<uint8_t>(model[i]);
  if (modelBytes > 0) label.bytes[label.length++] = ' ';
  constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  label.bytes[label.length++] = static_cast<uint8_t>(HEX_DIGITS[mac.bytes[MAC_BYTES - 2] >> 4]);
  label.bytes[label.length++] = static_cast<uint8_t>(HEX_DIGITS[mac.bytes[MAC_BYTES - 2] & 0xF]);
  label.bytes[label.length++] = static_cast<uint8_t>(HEX_DIGITS[mac.bytes[MAC_BYTES - 1] >> 4]);
  label.bytes[label.length++] = static_cast<uint8_t>(HEX_DIGITS[mac.bytes[MAC_BYTES - 1] & 0xF]);
  return label;
}

// One PSRAM block holds all the link's cross-task state, so it can outlive the link when a receive callback does
// (see EspNowLinkT::~EspNowLinkT): the FrameRing object itself (the callback's pointer goes to it), then its slots,
// then the peer table's MACs.
inline constexpr size_t RING_CONTROL_BYTES = (sizeof(FrameRing) + 15) & ~size_t{15};
inline constexpr size_t RING_SLOTS_BYTES = FrameRing::storageBytes(RING_DEPTH, GameCore::Protocol::MAX_FRAME_BYTES);
inline constexpr size_t TABLE_BYTES = PeerTable::storageBytes(MAX_PEERS);
inline constexpr size_t BLOCK_BYTES = RING_CONTROL_BYTES + RING_SLOTS_BYTES + TABLE_BYTES;
static_assert(std::is_trivially_destructible_v<FrameRing>, "the block is freed without running ~FrameRing");

}  // namespace EspNow

// What the link needs of a radio, one call per bring-up step. Each bool is "it worked"; the real driver's
// EspNowLink.cpp says what each does to Wi-Fi and ESP-NOW.
template <typename D>
concept EspNowDriver = requires(D driver, EspNow::Mac& mac, FrameRing& ring, const uint8_t* bytes,
                                std::span<const uint8_t> frame, uint8_t channel, uint16_t windowMs) {
  // Persistent settings off, station mode, auto-reconnect off (the previous value remembered).
  { driver.enterStation() } -> std::same_as<bool>;
  // Disconnect from any access point and wait it out: a channel set while associated is silently reverted.
  { driver.leaveAccessPoint() } -> std::same_as<bool>;
  { driver.setChannel(channel) } -> std::same_as<bool>;
  { driver.initEspNow() } -> std::same_as<bool>;
  // The callback copies each frame into `ring` and does nothing else.
  { driver.registerReceive(ring) } -> std::same_as<bool>;
  { driver.addBroadcastPeer(channel) } -> std::same_as<bool>;
  // Modem power save with a wake window; a failure is logged by the driver and is not fatal.
  { driver.enablePowerSave(windowMs) } -> std::same_as<void>;
  { driver.readMac(mac) } -> std::same_as<bool>;
  // Registers the unicast peer on first use, then sends. False for anything but ok (a full send queue included).
  { driver.sendTo(bytes, frame) } -> std::same_as<bool>;
  // Unregisters the callback, deinitializes ESP-NOW, power save off, then waits out a callback still running.
  // False when one still is: the ring may then still be written.
  { driver.shutdownEspNow() } -> std::same_as<bool>;
  // Wi-Fi off, the previous auto-reconnect restored.
  { driver.leaveStation() } -> std::same_as<void>;
};

// The real driver (EspNowLink.cpp). Declared here so the header names no radio type.
struct EspIdfDriver {
  bool enterStation();
  bool leaveAccessPoint();
  bool setChannel(uint8_t channel);
  bool initEspNow();
  bool registerReceive(FrameRing& ring);
  bool addBroadcastPeer(uint8_t channel);
  void enablePowerSave(uint16_t wakeWindowMs);
  bool readMac(EspNow::Mac& mac);
  bool sendTo(const uint8_t* mac, std::span<const uint8_t> frame);
  bool shutdownEspNow();
  void leaveStation();

  // The auto-reconnect value that was set before enterStation, to restore in leaveStation.
  bool autoReconnectSaved = false;
  bool previousAutoReconnect = true;
};

// An ILink over a driver. Brought up by create() (the link interface has no begin(): a failed bring-up is a null,
// and a link handed out is already running). Not thread-safe, as ILink says: one task calls it at a time, and the
// receive callback shares only the FrameRing.
template <EspNowDriver D>
class EspNowLinkT final : public GameCore::ILink {
 public:
  // Blocks: it can wait up to 500 ms in the disconnect and for the Wi-Fi mode switch. Call it from a task that may
  // wait (the lobby's side or the GameLink task's), never from the radio callback.
  //
  // Allocates the link and its PSRAM block, then brings the radio up in AD-18's order. Null (the radio down, Wi-Fi
  // handed back, nothing allocated left behind) when memory runs out or any step fails, so a following create can
  // succeed. `model` is the device's name for its host label.
  static std::unique_ptr<EspNowLinkT> create(D driver, const std::string_view model) {
    std::unique_ptr<EspNowLinkT> link(new (std::nothrow) EspNowLinkT(std::move(driver)));
    if (!link) {
      LOG_ERR("LINK", "out of memory for the link");
      return nullptr;
    }
    // Before any driver call: the ring and peer table are written by another task and live as long as the radio,
    // so they are PSRAM (11.9 KB is too much for the stack or the internal heap).
    link->block_ = HalMemory::allocatePsram(EspNow::BLOCK_BYTES);
    if (!link->block_) {
      LOG_ERR("LINK", "no PSRAM for the receive ring (%u B)", static_cast<unsigned>(EspNow::BLOCK_BYTES));
      return nullptr;
    }
    if (!link->attachBlock() || !link->bringUp(model)) return nullptr;  // the destructor ends it
    return link;
  }

  // Ends the link. When the receive callback could not be waited out, the PSRAM block (which holds the ring the
  // callback may still write) is deliberately leaked instead of freed.
  ~EspNowLinkT() override {
    end();
    if (storageAbandoned_) {
      LOG_ERR("LINK", "receive callback still running at shutdown: PSRAM block abandoned");
      (void)block_.release();
    }
  }
  EspNowLinkT(const EspNowLinkT&) = delete;
  EspNowLinkT& operator=(const EspNowLinkT&) = delete;

  bool send(const GameCore::PeerHandle to, const std::span<const uint8_t> frame) override {
    if (!running_ || !to.valid() || !sizeOk(frame)) return false;
    const uint8_t* const mac = peers_.macOf(to);
    if (mac == nullptr) return false;  // NO_PEER, another instance's handle, or one this link never gave
    return driver_.sendTo(mac, frame);
  }

  bool broadcast(const std::span<const uint8_t> frame) override {
    if (!running_ || !sizeOk(frame)) return false;
    return driver_.sendTo(EspNow::BROADCAST.bytes, frame);
  }

  bool receive(GameCore::PeerHandle& from, const std::span<uint8_t> buffer, size_t& length) override {
    if (!running_) return false;
    uint8_t mac[EspNow::MAC_BYTES];
    for (;;) {
      size_t got = 0;
      if (!ring_->pop(mac, buffer, got)) return false;  // pop drops a frame longer than `buffer` itself
      const GameCore::PeerHandle handle = peers_.findOrAdd(mac);
      if (!handle.valid()) continue;  // no room to name the sender: dropped (counted by the table)
      from = handle;
      length = got;
      return true;
    }
  }

  // Blocks: up to 100 ms waiting out a running receive callback, plus the Wi-Fi mode switch. Call it from a task that
  // may wait, never from the radio callback; it takes no RenderLock (AD-18).
  //
  // Idempotent. ESP-NOW down first (iff init succeeded), then the station handed back (iff entered): the radio is
  // off afterwards. Waiting frames are discarded and every earlier handle is dead.
  void end() override {
    running_ = false;
    if (espNowUp_) {
      espNowUp_ = false;
      if (!driver_.shutdownEspNow()) storageAbandoned_ = true;
    }
    if (stationHeld_) {
      stationHeld_ = false;
      driver_.leaveStation();
    }
    // The ring and table go with the link, unless a callback may still write the ring.
    if (!storageAbandoned_) {
      if (ring_ != nullptr) ring_->detach();
      peers_.detach();
    }
  }

  // The label this host advertises (ADVERT's hostLabel), valid for the life of the link. On this class, not ILink.
  std::span<const uint8_t> hostLabel() const { return label_.view(); }
  // Frames the receive callback dropped because the ring was full, and frames from a new peer the full peer table
  // could not name: the LINK figures of entry 8.
  uint32_t ringOverflows() const { return ring_ != nullptr ? ring_->droppedFull() : 0; }
  uint32_t unnamedPeerDrops() const { return peers_.fullDrops(); }

 private:
  explicit EspNowLinkT(D driver) : driver_(std::move(driver)) {}

  static bool sizeOk(const std::span<const uint8_t> frame) {
    return !frame.empty() && frame.size() <= GameCore::Protocol::MAX_FRAME_BYTES;
  }

  // Lays the ring object, its slots, and the peer table out in the PSRAM block.
  bool attachBlock() {
    uint8_t* const base = block_.get();
    if (reinterpret_cast<uintptr_t>(base) % alignof(FrameRing) != 0) {
      LOG_ERR("LINK", "PSRAM block is misaligned");
      return false;
    }
    ring_ = new (base) FrameRing();
    uint8_t* const slots = base + EspNow::RING_CONTROL_BYTES;
    if (!ring_->attach(slots, EspNow::RING_SLOTS_BYTES, EspNow::RING_DEPTH, GameCore::Protocol::MAX_FRAME_BYTES) ||
        !peers_.attach(slots + EspNow::RING_SLOTS_BYTES, EspNow::TABLE_BYTES, EspNow::MAX_PEERS,
                       EspNow::takeInstanceTag())) {
      LOG_ERR("LINK", "receive ring does not fit its block");
      return false;
    }
    return true;
  }

  bool failed([[maybe_unused]] const char* step) {
    LOG_ERR("LINK", "bring-up failed at %s", step);
    end();
    return false;
  }

  // AD-18's order, amended. stationHeld_ is set before the first call so a failed mode switch still hands Wi-Fi back;
  // espNowUp_ the moment esp_now_init succeeds, so a later failure does not leave ESP-NOW up and fail every later
  // bring-up on esp_now_init; auto-reconnect is off before the disconnect, and the disconnect is waited out before
  // the channel is set (a channel set while associated is silently reverted).
  bool bringUp(const std::string_view model) {
    stationHeld_ = true;
    if (!driver_.enterStation()) return failed("enterStation");
    if (!driver_.leaveAccessPoint()) return failed("leaveAccessPoint");
    if (!driver_.setChannel(EspNow::CHANNEL)) return failed("setChannel");
    if (!driver_.initEspNow()) return failed("initEspNow");
    espNowUp_ = true;
    if (!driver_.registerReceive(*ring_)) return failed("registerReceive");
    if (!driver_.addBroadcastPeer(EspNow::CHANNEL)) return failed("addBroadcastPeer");
    driver_.enablePowerSave(EspNow::WAKE_WINDOW_MS);
    EspNow::Mac mac;
    if (!driver_.readMac(mac)) return failed("readMac");
    label_ = EspNow::makeLabel(model, mac);
    running_ = true;
    LOG_INF("LINK", "radio up on channel %u as %02X:%02X:%02X:%02X:%02X:%02X", static_cast<unsigned>(EspNow::CHANNEL),
            mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3], mac.bytes[4], mac.bytes[5]);
    return true;
  }

  D driver_;
  HalMemory::PsramBuffer block_;
  FrameRing* ring_ = nullptr;  // inside block_
  EspNow::PeerTable peers_;
  EspNow::Label label_;
  bool running_ = false;
  bool stationHeld_ = false;
  bool espNowUp_ = false;
  bool storageAbandoned_ = false;
};

using EspNowLink = EspNowLinkT<EspIdfDriver>;
extern template class EspNowLinkT<EspIdfDriver>;

// The device's link, or null when PSRAM or any bring-up step fails (EspNowLink.cpp). Nothing calls it yet: the lobby
// and the GameLink task are later entries.
std::unique_ptr<EspNowLink> makeEspNowLink();
