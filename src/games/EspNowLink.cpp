#if FREEINK_CAP_GAMES && !defined(SIMULATOR)

// The device's ESP-NOW driver for EspNowLink.h: every call below mirrors crossplay's LinkRadio::begin, end, and send.
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

#include "EspNowLink.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>

// ESP-NOW v2 carries a frame up to 1,470 B, which is the largest Protocol frame.
static_assert(GameCore::Protocol::MAX_FRAME_BYTES <= ESP_NOW_MAX_DATA_LEN_V2);

// The 50 ms wake window works while no access point is joined only with this option (esp_now.h documents it; it
// reaches this file through esp_wifi.h's sdkconfig.h). The build fails without it instead of silently losing the
// window; platformio.ini and sdkconfig are not this file's to change, so a board without it stops for the owner.
// cppcheck (pio check) never sees the generated sdkconfig.h, so it is told to expect the error.
#ifndef CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE
// cppcheck-suppress preprocessorErrorDirective
#error "EspNowLink needs CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y for esp_now_set_wake_window while disconnected"
#endif

namespace {

// The receive callback is a bare C function pointer with nowhere to hang user data, so the ring it fills is
// reachable from file scope. Only one link runs at a time. Both are constant-initialized: no static initializer.
constinit std::atomic<FrameRing*> activeRing{nullptr};
constinit std::atomic<uint32_t> inCallback{0};  // callbacks running now: shutdownEspNow waits for it to reach 0

constexpr TickType_t QUIESCE_TICKS = pdMS_TO_TICKS(100);

// Runs on the Wi-Fi task. It copies a frame into the ring and does nothing else: no GameCore code, no lock, no
// allocation, no log. The default (sequentially consistent) orders pair with shutdownEspNow's: either the callback
// sees the null ring or shutdownEspNow sees inCallback above 0.
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, const int length) {
  inCallback.fetch_add(1);
  FrameRing* const ring = activeRing.load();
  if (ring != nullptr && info != nullptr && info->src_addr != nullptr && data != nullptr && length > 0) {
    ring->push(info->src_addr, data, static_cast<size_t>(length));
  }
  inCallback.fetch_sub(1);
}

}  // namespace

bool EspIdfDriver::enterStation() {
  // No NVS writes for a radio we use for a few minutes.
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA)) return false;
  // Disarm the core's auto-reconnect BEFORE disconnecting. Events are delivered on the Wi-Fi task, so a NO_AP_FOUND
  // queued before we got here would otherwise re-issue a scanning association after the channel is pinned and drag
  // the radio back to the router's channel with a match running.
  previousAutoReconnect = WiFi.getAutoReconnect();
  autoReconnectSaved = true;
  WiFi.setAutoReconnect(false);
  return true;
}

bool EspIdfDriver::leaveAccessPoint() {
  // esp_wifi_disconnect() is asynchronous, and while the station is still associated the channel belongs to the AP:
  // a set_channel issued into that window is reverted and still returns ESP_OK. The third argument is a timeout and
  // the result is "it really went down" (the core waits on the L2 association, which WiFi.status() cannot see).
  // False also means esp_wifi_disconnect() itself refused (not initialized or not started).
  if (!WiFi.disconnect(false, false, 500)) {
    LOG_ERR("LINK", "radio would not leave the AP (still associated, or never started)");
    return false;
  }
  return true;
}

bool EspIdfDriver::setChannel(const uint8_t channel) {
  if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    LOG_ERR("LINK", "could not set channel %u", static_cast<unsigned>(channel));
    return false;
  }
  return true;
}

bool EspIdfDriver::initEspNow() {
  // ESP-NOW has one global receive callback, so only one thing on the device may own the radio at a time.
  if (esp_now_init() != ESP_OK) {
    LOG_ERR("LINK", "esp_now_init failed");
    return false;
  }
  return true;
}

bool EspIdfDriver::registerReceive(FrameRing& ring) {
  activeRing.store(&ring);
  if (esp_now_register_recv_cb(&onReceive) != ESP_OK) {
    activeRing.store(nullptr);
    LOG_ERR("LINK", "could not register receive callback");
    return false;
  }
  return true;
}

bool EspIdfDriver::addBroadcastPeer(const uint8_t channel) {
  // ESP-NOW requires even the broadcast address to be a registered peer before anything goes out.
  esp_now_peer_info_t peer = {};
  std::memcpy(peer.peer_addr, EspNow::BROADCAST.bytes, EspNow::MAC_BYTES);
  peer.channel = channel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  const esp_err_t added = esp_now_add_peer(&peer);
  if (added != ESP_OK && added != ESP_ERR_ESPNOW_EXIST) {
    LOG_ERR("LINK", "could not add broadcast peer (%d)", static_cast<int>(added));
    return false;
  }
  return true;
}

void EspIdfDriver::enablePowerSave(const uint16_t wakeWindowMs) {
  // Sleep between wake windows rather than listening flat out. Neither failure ends the link: it only costs power.
  const esp_err_t ps = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  if (ps != ESP_OK) {
    LOG_ERR("LINK", "could not enable modem power save (%d)", static_cast<int>(ps));
  }
  const esp_err_t wake = esp_now_set_wake_window(wakeWindowMs);
  if (wake != ESP_OK) {
    LOG_ERR("LINK", "could not set the wake window (%d)", static_cast<int>(wake));
  }
}

bool EspIdfDriver::readMac(EspNow::Mac& mac) {
  if (esp_wifi_get_mac(WIFI_IF_STA, mac.bytes) != ESP_OK) {
    LOG_ERR("LINK", "could not read local MAC");
    return false;
  }
  return true;
}

bool EspIdfDriver::sendTo(const uint8_t* mac, const std::span<const uint8_t> frame) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    std::memcpy(peer.peer_addr, mac, EspNow::MAC_BYTES);
    peer.channel = EspNow::CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    const esp_err_t added = esp_now_add_peer(&peer);
    // A full peer table is a crowded room: failing the send is right, the caller resends.
    if (added != ESP_OK && added != ESP_ERR_ESPNOW_EXIST) return false;
  }
  // esp_now_send copies the frame (esp_now.h: the buffer need not stay valid), which is ILink's copy contract.
  return esp_now_send(mac, frame.data(), frame.size()) == ESP_OK;
}

bool EspIdfDriver::shutdownEspNow() {
  // Null first, so a callback that starts now sees no ring; then no more callbacks; then wait out one that began
  // before. This closes the one race between the Wi-Fi task and the release of the PSRAM block holding the ring.
  activeRing.store(nullptr);
  // A failure is logged and does not change the result: the ring pointer is already null, so a late callback
  // pushes nothing. A failed deinit leaves ESP-NOW up, which the next bring-up would fail on at init.
  const esp_err_t unregistered = esp_now_unregister_recv_cb();
  if (unregistered != ESP_OK) {
    LOG_ERR("LINK", "could not unregister the receive callback (%d)", static_cast<int>(unregistered));
  }
  const esp_err_t deinitialized = esp_now_deinit();
  if (deinitialized != ESP_OK) {
    LOG_ERR("LINK", "esp_now_deinit failed (%d)", static_cast<int>(deinitialized));
  }
  // Forces power save off rather than restoring what was there; leaveStation follows and switches Wi-Fi off, and the
  // core re-applies its own setting on the next station start.
  const esp_err_t powerSaveOff = esp_wifi_set_ps(WIFI_PS_NONE);
  if (powerSaveOff != ESP_OK) {
    LOG_ERR("LINK", "could not turn power save off (%d)", static_cast<int>(powerSaveOff));
  }
  const TickType_t start = xTaskGetTickCount();
  while (inCallback.load() != 0) {
    if (xTaskGetTickCount() - start >= QUIESCE_TICKS) return false;
    vTaskDelay(1);
  }
  return true;
}

void EspIdfDriver::leaveStation() {
  if (!WiFi.mode(WIFI_OFF)) {
    LOG_ERR("LINK", "could not switch Wi-Fi off");
  }
  // Restored AFTER the radio is down, mirroring the order enterStation takes care over.
  if (autoReconnectSaved) {
    WiFi.setAutoReconnect(previousAutoReconnect);
    autoReconnectSaved = false;
  }
}

// Instantiated here, the one translation unit that includes the radio headers.
template class EspNowLinkT<EspIdfDriver>;

namespace {

#if FREEINK_DEVICE_X4PRO
constexpr const char* DEVICE_MODEL = "X4 Pro";
#elif FREEINK_DEVICE_STICKY
constexpr const char* DEVICE_MODEL = "Sticky";
#else
constexpr const char* DEVICE_MODEL = "Reader";
#endif

}  // namespace

std::unique_ptr<EspNowLink> makeEspNowLink() { return EspNowLink::create(EspIdfDriver{}, DEVICE_MODEL); }

#endif  // FREEINK_CAP_GAMES && !SIMULATOR
