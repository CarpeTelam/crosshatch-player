#pragma once

// The checks every GameCore::ILink must pass (ILink.h, sections "Frames", "Ownership", and the PeerHandle rules).
// Run over FakeLink here (FakeLinkTest.cpp) and over UdpLink in entry 16; EspNowLink is outside the host
// harness, so its agreement is checked on the device (entry 11). This is what pins a test double to the
// interface the device adapters also implement.
//
// `a` and `b` are two connected, running links with no faults between them: in range of each other, with
// nothing waiting. `pump` makes frames sent so far receivable (a no-op for FakeLink without delay; the socket
// read for UdpLink). Both links are ended on return.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "ILink.h"
#include "Protocol.h"

namespace GameCore::LinkTest {

namespace contract {

inline std::vector<uint8_t> pattern(const size_t size, const uint8_t seed) {
  std::vector<uint8_t> bytes(size);
  for (size_t i = 0; i < size; i++) bytes[i] = static_cast<uint8_t>(seed + i * 7);
  return bytes;
}

// Takes one frame from `link` into a MAX_FRAME_BYTES buffer.
inline bool take(ILink& link, PeerHandle& from, std::vector<uint8_t>& frame) {
  std::vector<uint8_t> buffer(Protocol::MAX_FRAME_BYTES);
  size_t length = 0;
  if (!link.receive(from, buffer, length)) return false;
  frame.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(length));
  return true;
}

}  // namespace contract

inline void expectLinkContract(ILink& a, ILink& b, const std::function<void()>& pump) {
  using contract::pattern;
  using contract::take;
  SCOPED_TRACE("expectLinkContract");

  // Before any handle exists: the invalid handle names nobody, and nothing is waiting.
  const std::vector<uint8_t> hello = pattern(40, 1);
  EXPECT_FALSE(a.send(NO_PEER, hello));
  EXPECT_FALSE(b.send(NO_PEER, hello));
  PeerHandle from = NO_PEER;
  std::vector<uint8_t> got;
  EXPECT_FALSE(take(a, from, got));

  // A broadcast reaches the other link and never the sender; its sender gets a valid handle.
  ASSERT_TRUE(b.broadcast(hello));
  pump();
  ASSERT_TRUE(take(a, from, got));
  EXPECT_TRUE(from.valid());
  EXPECT_EQ(got, hello);
  const PeerHandle aNamesB = from;
  EXPECT_FALSE(take(b, from, got)) << "the sender received its own broadcast";
  EXPECT_FALSE(take(a, from, got)) << "a broadcast arrived twice";

  // A false receive leaves `from` and `length` untouched.
  PeerHandle sentinel = PeerHandle::fromValue(0x7777);
  size_t length = 99;
  std::vector<uint8_t> buffer(Protocol::MAX_FRAME_BYTES);
  EXPECT_FALSE(a.receive(sentinel, buffer, length));
  EXPECT_EQ(sentinel.value(), 0x7777u);
  EXPECT_EQ(length, 99u);

  // A received handle sends to its peer, and the peer names the sender with a stable handle.
  ASSERT_TRUE(a.send(aNamesB, hello));
  pump();
  ASSERT_TRUE(take(b, from, got));
  EXPECT_TRUE(from.valid());
  EXPECT_EQ(got, hello);
  const PeerHandle bNamesA = from;
  ASSERT_TRUE(b.send(bNamesA, pattern(12, 9)));
  pump();
  ASSERT_TRUE(take(a, from, got));
  EXPECT_EQ(from, aNamesB) << "the same peer must give an equal handle";
  EXPECT_EQ(got, pattern(12, 9));

  // A handle that names no peer of this link is refused: the invalid one, and the one the other link gave.
  EXPECT_FALSE(a.send(NO_PEER, hello));
  EXPECT_FALSE(b.send(aNamesB, hello)) << "a handle from another link instance must not send";
  EXPECT_FALSE(a.send(bNamesA, hello)) << "a handle from another link instance must not send";
  pump();
  EXPECT_FALSE(take(a, from, got));
  EXPECT_FALSE(take(b, from, got));

  // Frames are delivered whole: boundaries kept, in order, at both ends of the size range.
  const std::vector<uint8_t> one = pattern(1, 3);
  const std::vector<uint8_t> big = pattern(Protocol::MAX_FRAME_BYTES, 5);
  const std::vector<uint8_t> mid = pattern(300, 8);
  ASSERT_TRUE(a.send(aNamesB, one));
  ASSERT_TRUE(a.send(aNamesB, big));
  ASSERT_TRUE(a.send(aNamesB, mid));
  pump();
  ASSERT_TRUE(take(b, from, got));
  EXPECT_EQ(got, one);
  ASSERT_TRUE(take(b, from, got));
  EXPECT_EQ(got, big);
  ASSERT_TRUE(take(b, from, got));
  EXPECT_EQ(got, mid);
  EXPECT_FALSE(take(b, from, got));

  // An empty or over-limit frame is refused by send and broadcast, and nothing arrives.
  const std::vector<uint8_t> tooBig = pattern(Protocol::MAX_FRAME_BYTES + 1, 2);
  EXPECT_FALSE(a.send(aNamesB, std::span<const uint8_t>()));
  EXPECT_FALSE(a.send(aNamesB, tooBig));
  EXPECT_FALSE(a.broadcast(std::span<const uint8_t>()));
  EXPECT_FALSE(a.broadcast(tooBig));
  pump();
  EXPECT_FALSE(take(b, from, got));

  // The link copies the frame before returning: the caller may reuse its buffer at once.
  std::vector<uint8_t> reused = pattern(64, 11);
  const std::vector<uint8_t> original = reused;
  ASSERT_TRUE(a.send(aNamesB, reused));
  std::fill(reused.begin(), reused.end(), uint8_t{0xEE});
  pump();
  ASSERT_TRUE(take(b, from, got));
  EXPECT_EQ(got, original);

  // A frame longer than the receive buffer is dropped, never truncated, and receive goes on to the next one.
  ASSERT_TRUE(a.send(aNamesB, pattern(100, 4)));
  ASSERT_TRUE(a.send(aNamesB, pattern(20, 6)));
  pump();
  std::vector<uint8_t> small(50, 0xAA);
  length = 0;
  ASSERT_TRUE(b.receive(from, small, length));
  EXPECT_EQ(length, 20u);
  EXPECT_TRUE(std::equal(small.begin(), small.begin() + 20, pattern(20, 6).begin()));
  EXPECT_FALSE(b.receive(from, small, length)) << "the long frame must be dropped, not delivered later";

  // end() is idempotent and stops all traffic through the ended link; waiting frames are discarded.
  ASSERT_TRUE(b.send(bNamesA, hello));
  pump();
  a.end();
  a.end();
  EXPECT_FALSE(a.send(aNamesB, hello));
  EXPECT_FALSE(a.broadcast(hello));
  EXPECT_FALSE(take(a, from, got)) << "an ended link must discard what was waiting";
  (void)b.broadcast(hello);
  (void)b.send(bNamesA, hello);
  pump();
  EXPECT_FALSE(take(a, from, got));
  b.end();
  b.end();
  EXPECT_FALSE(b.send(bNamesA, hello));
  EXPECT_FALSE(b.broadcast(hello));
}

}  // namespace GameCore::LinkTest
