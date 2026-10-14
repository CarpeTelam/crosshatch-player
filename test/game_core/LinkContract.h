#pragma once

// The checks every GameCore::ILink must pass (ILink.h, sections "Frames", "Ownership", and the PeerHandle rules).
// Run over FakeLink (FakeLinkTest.cpp), UdpLink (UdpLinkTest.cpp), and EspNowLinkT over the fake radio
// (EspNowLinkTest.cpp; the real ESP-NOW calls are checked on the device). This is what pins a test double to the
// interface the device adapters also implement.
//
// expectLinkContract: `a` and `b` are two connected, running links with no faults between them: in range of each
// other, with nothing waiting. `pump` makes frames sent so far receivable (a no-op for FakeLink without delay; the
// socket read for UdpLink). Both links are ended on return.
//
// expectThreeLinkContract: what two links cannot check (deferred-work 6.2, second item), for three connected links:
// every handle is valid and names one peer, no two links share a handle value, a handle stays the same on a second
// round, no receive returns NO_PEER, and a handle is refused by every link that did not give it. `a`, `b`, and `c`
// are three connected, running links with no faults between them: all in range of one another, with nothing waiting.
// `pump` as above. All three are ended on return.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
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

namespace three {

// Every link broadcasts once, and each of the other two receives it exactly once, with a valid handle. names[r][s] is
// the handle receiver r gave sender s (NO_PEER on the diagonal, and when a check failed).
inline void broadcastRound(const std::array<ILink*, 3>& links, const std::function<void()>& pump, const uint8_t seed,
                           PeerHandle (&names)[3][3]) {
  using contract::pattern;
  using contract::take;
  for (size_t sender = 0; sender < 3; sender++) {
    const std::vector<uint8_t> frame = pattern(24 + sender, static_cast<uint8_t>(seed + sender));
    ASSERT_TRUE(links[sender]->broadcast(frame)) << "sender " << sender;
    pump();
    for (size_t receiver = 0; receiver < 3; receiver++) {
      PeerHandle from = NO_PEER;
      std::vector<uint8_t> got;
      if (receiver == sender) {
        EXPECT_FALSE(take(*links[receiver], from, got)) << "link " << sender << " received its own broadcast";
        continue;
      }
      ASSERT_TRUE(take(*links[receiver], from, got)) << "link " << receiver << " missed the broadcast of " << sender;
      EXPECT_TRUE(from.valid()) << "link " << receiver << " named sender " << sender << " NO_PEER";
      EXPECT_EQ(got, frame);
      names[receiver][sender] = from;
      PeerHandle again = NO_PEER;
      EXPECT_FALSE(take(*links[receiver], again, got)) << "a broadcast arrived twice at " << receiver;
    }
  }
}

}  // namespace three

inline void expectThreeLinkContract(ILink& a, ILink& b, ILink& c, const std::function<void()>& pump) {
  using contract::pattern;
  using contract::take;
  SCOPED_TRACE("expectThreeLinkContract");
  const std::array<ILink*, 3> links{&a, &b, &c};

  // Each of the three hears the other two broadcast, with valid handles.
  PeerHandle first[3][3];
  three::broadcastRound(links, pump, 1, first);
  if (::testing::Test::HasFatalFailure()) return;

  // The six handles are pairwise distinct across links, and the two one link holds name two peers.
  std::set<uint32_t> values;
  size_t valid = 0;
  for (size_t receiver = 0; receiver < 3; receiver++) {
    for (size_t sender = 0; sender < 3; sender++) {
      if (receiver == sender) continue;
      ASSERT_TRUE(first[receiver][sender].valid());
      values.insert(first[receiver][sender].value());
      valid++;
    }
  }
  EXPECT_EQ(valid, 6u);
  EXPECT_EQ(values.size(), 6u) << "two handles share a value";

  // A second round gives the same handles: a peer is named once and keeps its name.
  PeerHandle second[3][3];
  three::broadcastRound(links, pump, 40, second);
  if (::testing::Test::HasFatalFailure()) return;
  for (size_t receiver = 0; receiver < 3; receiver++) {
    for (size_t sender = 0; sender < 3; sender++) {
      EXPECT_EQ(second[receiver][sender], first[receiver][sender]) << receiver << " <- " << sender;
    }
  }

  // A handle reaches its peer and no other; every link that did not give it refuses it.
  const std::vector<uint8_t> hello = pattern(33, 7);
  for (size_t owner = 0; owner < 3; owner++) {
    for (size_t peer = 0; peer < 3; peer++) {
      if (owner == peer) continue;
      const PeerHandle handle = first[owner][peer];
      for (size_t other = 0; other < 3; other++) {
        if (other == owner) continue;
        EXPECT_FALSE(links[other]->send(handle, hello))
            << "link " << other << " took the handle link " << owner << " gave for " << peer;
      }
      pump();
      for (size_t listener = 0; listener < 3; listener++) {
        PeerHandle from = NO_PEER;
        std::vector<uint8_t> got;
        EXPECT_FALSE(take(*links[listener], from, got)) << "a refused send arrived at " << listener;
      }
      ASSERT_TRUE(links[owner]->send(handle, hello)) << "link " << owner << " could not send to " << peer;
      pump();
      for (size_t listener = 0; listener < 3; listener++) {
        PeerHandle from = NO_PEER;
        std::vector<uint8_t> got;
        if (listener != peer) {
          EXPECT_FALSE(take(*links[listener], from, got)) << "a send to " << peer << " arrived at " << listener;
          continue;
        }
        ASSERT_TRUE(take(*links[listener], from, got));
        EXPECT_EQ(got, hello);
        EXPECT_EQ(from, first[peer][owner]) << "the reply handle must name the sender";
        EXPECT_TRUE(from.valid());
      }
    }
  }

  for (ILink* link : links) link->end();
}

}  // namespace GameCore::LinkTest
