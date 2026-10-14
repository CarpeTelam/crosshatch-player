// The host's EspIdfDriver (EspNowLinkScript.h), standing in for the device's (src/games/EspNowLink.cpp), and the
// makeEspNowLink() that runs the real EspNowLinkT bring-up over it.

#if FREEINK_CAP_GAMES

#include "EspNowLink.h"
#include "EspNowLinkScript.h"

namespace espnowlink {

Script& script() {
  static Script instance;
  return instance;
}

}  // namespace espnowlink

// The real link logic, instantiated here as EspNowLink.cpp does for the device.
template class EspNowLinkT<EspIdfDriver>;

// Without `succeed`, a host with no radio: the first step fails, so every bring-up ends in the null the device returns
// for a failed step, through the real create() and its failure path (end() hands the station back). With it every step
// works. Either way nothing is ever sent.
bool EspIdfDriver::enterStation() {
  ++espnowlink::script().attempts;
  return espnowlink::script().succeed;
}
bool EspIdfDriver::leaveAccessPoint() { return espnowlink::script().succeed; }
bool EspIdfDriver::setChannel(uint8_t) { return espnowlink::script().succeed; }
bool EspIdfDriver::initEspNow() { return espnowlink::script().succeed; }
bool EspIdfDriver::registerReceive(FrameRing&) { return espnowlink::script().succeed; }
bool EspIdfDriver::addBroadcastPeer(uint8_t) { return espnowlink::script().succeed; }
void EspIdfDriver::enablePowerSave(uint16_t) {}
bool EspIdfDriver::readMac(EspNow::Mac& mac) {
  for (size_t i = 0; i < EspNow::MAC_BYTES; ++i) mac.bytes[i] = espnowlink::script().mac[i];
  return espnowlink::script().succeed;
}
bool EspIdfDriver::sendTo(const uint8_t*, std::span<const uint8_t>) { return false; }
bool EspIdfDriver::shutdownEspNow() {
  ++espnowlink::script().shutdowns;
  return true;
}
void EspIdfDriver::leaveStation() { ++espnowlink::script().stationsLeft; }

std::unique_ptr<EspNowLink> makeEspNowLink() { return EspNowLink::create(EspIdfDriver{}, "X4 Pro"); }

#endif  // FREEINK_CAP_GAMES
