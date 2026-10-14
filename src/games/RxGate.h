#pragma once

// The handshake between the radio's receive callback (the Wi-Fi task) and the shutdown that frees the PSRAM block
// holding the receive ring (EspNowLink.cpp's EspIdfDriver). Header-only and radio-free, with the tick source and the
// wait injected, so the host runs it on two threads (RxGateTest). The device keeps one constant-initialized instance at
// file scope: the receive callback is a bare C function pointer with nowhere to hang user data, and only one link runs
// at a time.
//
// Every atomic access uses the default (sequentially consistent) order, which the handshake needs: either a callback
// sees the null ring, or quiesce sees its count above 0.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "FrameRing.h"

// `Ring` is FrameRing on the device; a test may hold a push to keep a callback running.
template <class Ring>
class RxGateT {
 public:
  // Bring-up, before the callback is registered: the ring callbacks push into.
  void open(Ring& ring) { activeRing.store(&ring); }
  // A registration that failed: no callback pushes into the ring.
  void close() { activeRing.store(nullptr); }

  // The receive callback's whole body, on the Wi-Fi task: copies a frame into the open ring and does nothing else (no
  // lock, no allocation, no log). Counted from before the ring is read to after the push, so quiesce sees it. `Info`
  // is esp_now_recv_info_t on the device (its src_addr is the sender's MAC); a null info, address, or data, a length
  // of 0 or less, or a closed ring pushes nothing.
  template <class Info>
  void receive(const Info* info, const uint8_t* data, const int length) {
    inCallback.fetch_add(1);
    Ring* const ring = activeRing.load();
    if (ring != nullptr && info != nullptr && info->src_addr != nullptr && data != nullptr && length > 0) {
      ring->push(info->src_addr, data, static_cast<size_t>(length));
    }
    inCallback.fetch_sub(1);
  }

  // Shutdown, in this order: the ring is unhooked first, so a callback that starts now sees none; then `stopCallbacks`
  // runs (the device unregisters the callback and deinitializes ESP-NOW, so no more start); then a callback that
  // began before is waited out, reading `now()` (ticks) and calling `wait()` between polls. True once none runs. False
  // once `limitTicks` have passed with one still running: it may still write into the ring, so the caller abandons
  // the ring's block rather than free it.
  template <class StopCallbacks, class Now, class Wait>
  bool quiesce(StopCallbacks&& stopCallbacks, Now&& now, Wait&& wait, const uint32_t limitTicks) {
    activeRing.store(nullptr);
    stopCallbacks();
    const auto start = now();
    while (inCallback.load() != 0) {
      if (static_cast<uint32_t>(now() - start) >= limitTicks) return false;
      wait();
    }
    return true;
  }

 private:
  std::atomic<Ring*> activeRing{nullptr};
  std::atomic<uint32_t> inCallback{0};  // callbacks running now
};

using RxGate = RxGateT<FrameRing>;
