#pragma once

// makeEspNowLink() for the screens that open the Nearby lobby, over a stand-in for the device's EspIdfDriver
// (src/games/EspNowLink.cpp: WiFi, esp_now.h, and esp_wifi.h), which is device code and outside the host harness. By
// default the stand-in is a host with no radio: its first bring-up step (enterStation) fails, so the real
// EspNowLinkT::create() takes its failure path and returns the null the device returns when a step or PSRAM fails, and
// a lobby opened on the host's own NearbyRadio shows "Nearby could not start" exactly as a device whose radio would
// not start does. With `succeed` set every step works and readMac gives `mac`, so the real create() hands out a
// running link with its label ("X4 Pro" and the MAC's last two bytes): the success path of NearbyRadio::open(), which
// copies that label and the link. More permissive than the device in both modes: a step never fails part way, never
// blocks, and no frame is ever heard or sent (sendTo reports false); the link's logic is EspNowLinkTest's (EspNowLinkT
// over a fake driver); the radio itself, the label from a real MAC, and the teardown are entry 11's device run.
// `attempts` counts the bring-ups since reset(), `shutdowns` and `stationsLeft` the teardown calls the link made.
//
// The definitions are in the test executable's library (EspNowLinkDouble.cpp), where the real EspNowLink.cpp is
// excluded by name (esp_now_link.sources.cmake).

#include <cstdint>

namespace espnowlink {

struct Script {
  bool succeed = false;
  uint8_t mac[6] = {0x24, 0x0A, 0xC4, 0x00, 0x3F, 0x2A};
  int attempts = 0;
  int shutdowns = 0;
  int stationsLeft = 0;
};

Script& script();
inline void reset() { script() = Script{}; }

}  // namespace espnowlink
