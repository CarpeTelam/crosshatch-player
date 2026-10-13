#pragma once

// GameCore::ILink over UDP on the loopback interface: the simulator's stand-in for EspNowLink (entry 16 of
// epic-play-nearby), so two simulator windows of one worktree play a nearby round with no radio. The implementation
// (UdpLink.cpp) is one whole-file #if on FREEINK_CAP_GAMES && SIMULATOR and compiles to nothing on the device; this
// header names no socket type and includes only lib/GameCore and the standard library.
//
// Every instance binds the first free port of an 8-port range (SLOTS ports from a base) on 127.0.0.1. A broadcast is a
// send to every other port of the range, since loopback has no broadcast worth relying on. A peer is its port: the
// handle is (instance tag << 16) | port, and a port is named when a datagram from it is first received, so a send needs
// a handle from receive() (ILink.h: no connect). The label carried in the host's ADVERT is "Simulator <port>".
//
// The base is 45700, or the value of CROSSHATCH_UDP_PORT_BASE when it is a number in 1024..65535-SLOTS, read each time
// a link starts (a function call, never a static initializer: AD-2). sim.sh derives the variable from the repository
// root, so simulators of two worktrees do not hear each other.
//
// A datagram longer than the caller's buffer, empty, from a port outside the range, or from the link's own port is
// dropped (ILink.h: a frame is delivered whole or not at all).
//
// Adapted from crossplay 0a1d17f src/apps_local/link/LinkRadio.cpp, its SIMULATOR branch (MIT).
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

#include <ILink.h>
#include <Protocol.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

class UdpLink final : public GameCore::ILink {
 public:
  static constexpr size_t SLOTS = 8;
  static constexpr uint16_t DEFAULT_BASE_PORT = 45700;
  static constexpr const char* BASE_PORT_ENV = "CROSSHATCH_UDP_PORT_BASE";

  // The base port from the environment: DEFAULT_BASE_PORT unless BASE_PORT_ENV holds a whole number in
  // 1024..65535-SLOTS. Reads the environment on every call.
  static uint16_t basePortFromEnvironment();

  // A running link on the first free port of basePort..basePort+SLOTS-1. Null, with a LOG_ERR, when the base leaves no
  // room for SLOTS ports, there is no socket, it cannot be made non-blocking, or all SLOTS binds fail.
  static std::unique_ptr<UdpLink> create(uint16_t basePort);

  ~UdpLink() override;
  UdpLink(const UdpLink&) = delete;
  UdpLink& operator=(const UdpLink&) = delete;

  bool send(GameCore::PeerHandle to, std::span<const uint8_t> frame) override;
  bool broadcast(std::span<const uint8_t> frame) override;
  bool receive(GameCore::PeerHandle& from, std::span<uint8_t> buffer, size_t& length) override;
  void end() override;

  uint16_t port() const { return port_; }
  // "Simulator <port>", as the bytes the host's ADVERT carries (at most Protocol::MAX_HOST_LABEL_BYTES).
  std::span<const uint8_t> hostLabel() const { return std::span<const uint8_t>(label_, labelLength_); }

 private:
  UdpLink(int fd, uint16_t basePort, uint16_t port, uint32_t instanceTag);

  // The slot of a port in the range, or SLOTS when the port is outside it.
  size_t slotOf(uint16_t port) const;

  int socket_;
  uint16_t base_;
  uint16_t port_;
  uint32_t tag_;
  uint8_t heard_ = 0;  // bit i: a datagram from port base_ + i has been received, so a handle names it
  static_assert(SLOTS <= 8 * sizeof(heard_), "heard_ holds one bit per slot");
  uint8_t label_[GameCore::Protocol::MAX_HOST_LABEL_BYTES] = {};
  size_t labelLength_ = 0;
};

// A link on the environment's base (UdpLink::basePortFromEnvironment()); null when none starts.
std::unique_ptr<UdpLink> makeUdpLink();
