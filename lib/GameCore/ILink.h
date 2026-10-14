#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameCore {

// Names one peer of one link instance. Opaque: session code stores it, compares it, and passes it back to
// the link that gave it, and does nothing else with it. It is never a MAC address, a port, or an index it may
// interpret; value() is for adapters, tests, and log lines, and means nothing on another link or device.
//
// Rules:
// - Created only by a link, only when it receives a frame from a peer it has not named yet (and, for tests,
//   FakeLink::handleOf, which stands for "has heard"). There is no connect(address): a session reaches a peer
//   it has not heard from only by broadcasting, so the first handle of a guest comes from the host's ADVERT
//   and the host's from the guest's JOIN.
// - Stable and unique for the life of one link instance, from first receipt to end(): the same peer always
//   gives an equal handle, two peers never share one, and a handle is never reused for a different peer, even
//   if the link forgets the first. Valid values are nonzero and chosen by the adapter. An adapter makes its
//   handle values unique across every link instance in the process (for example with an instance tag in the
//   high bits), since a handle from another instance must make send() return false.
// - Invalid is the default-constructed handle, NO_PEER. send to it returns false. receive never returns it. A
//   valid handle from another link instance, or a dead one after end(), names no peer: send returns false.
// - Compared with == and != only. A table keyed by handle searches linearly (v1 has at most a few peers).
// - Capacity is the adapter's: when its table is full it drops a frame from a new peer (counted by the
//   adapter), and the contract says only that receive never returns an unnamed sender.
class PeerHandle {
 public:
  constexpr PeerHandle() = default;  // invalid
  // For ILink implementations only: session code never makes a valid handle, it receives one.
  static constexpr PeerHandle fromValue(const uint32_t value) { return PeerHandle(value); }

  constexpr bool valid() const { return id != 0; }
  constexpr uint32_t value() const { return id; }
  friend constexpr bool operator==(PeerHandle, PeerHandle) = default;  // equality only: no ordering, no hash

 private:
  explicit constexpr PeerHandle(const uint32_t value) : id(value) {}
  uint32_t id = 0;
};

inline constexpr PeerHandle NO_PEER{};

// Port (spine AD-1): a datagram link to the other devices in range. It moves opaque frames and names a peer
// only by a PeerHandle. It knows nothing of ESP-NOW, a MAC address, a channel, or a socket; adapters
// (EspNowLink, UdpLink in the simulator, FakeLink in tests) hold every such fact.
//
// Frames: one call carries one whole frame, 1..Protocol::MAX_FRAME_BYTES bytes. A frame is delivered whole
// or not at all (never truncated, split, or merged with another). Nothing else is promised: frames may be
// lost, duplicated, delayed, or reordered, which is what ReliableLink exists for.
//
// Ownership: the caller owns every buffer it passes, and the link never keeps a pointer to one past the call.
// send() and broadcast() copy the frame before returning, so the caller may reuse its buffer at once.
// receive() copies the frame into the caller's buffer, which should be Protocol::MAX_FRAME_BYTES long.
//
// Threads: an ILink is not thread-safe. One task calls it at a time (the GameLink task while it runs; the
// thread that stopped the task afterwards, for end()). An adapter synchronizes whatever its own receive
// callback or socket thread shares with receive(). No call blocks on the air: each hands off and returns.
class ILink {
 public:
  virtual ~ILink() = default;

  // Hands one frame for `to`. True: the link took the frame for transmission (says nothing about arrival).
  // False, nothing sent: `to` is invalid or names no peer this link has received from; the frame is empty
  // or longer than Protocol::MAX_FRAME_BYTES; the link has ended; or it cannot take a frame now (its send
  // queue is full). A false is an ordinary loss: the caller does not retry in a loop, ReliableLink resends.
  virtual bool send(PeerHandle to, std::span<const uint8_t> frame) = 0;

  // Hands one frame for every other device in range that listens. The sender never receives its own
  // broadcast. Same true/false rules as send() minus the peer. Only ADVERT is broadcast (AD-13, by
  // convention of the lobby; the link does not check).
  virtual bool broadcast(std::span<const uint8_t> frame) = 0;

  // Takes the oldest waiting frame. True: `from` is the sender's handle (never invalid), the frame is at the
  // start of `buffer`, and `length` is its size. False: nothing waiting; `from` and `length` are untouched.
  // A waiting frame longer than `buffer.size()` is dropped by the link (never truncated) and receive()
  // goes on to the next one, so false means the queue is empty, not that a frame was dropped. A frame
  // whose sender the link has no room to name is dropped the same way.
  virtual bool receive(PeerHandle& from, std::span<uint8_t> buffer, size_t& length) = 0;

  // Brings the link down. Idempotent. Afterwards send() and broadcast() return false, receive() returns
  // false (waiting frames are discarded), and every earlier handle is dead. There is no begin(): an adapter
  // is brought up by its own factory, which is where a radio fact (a failed bring-up, the Wi-Fi state)
  // belongs, and is handed up already running.
  virtual void end() = 0;
};

}  // namespace GameCore
