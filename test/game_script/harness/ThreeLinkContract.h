#pragma once

// What LinkContract.h cannot check with two links (deferred-work 6.2, second item), for three connected links: every
// handle is valid and names one peer, no two links share a handle value, a handle stays the same on a second round, no
// receive returns NO_PEER, and a handle is refused by every link that did not give it. Run over three UdpLinks and
// over three FakeLinks (UdpLinkTest.cpp): the double and the real link agree. EspNowLink still cannot run it on the
// host (the device run, entry 11).
//
// `a`, `b`, and `c` are three connected, running links with no faults between them: all in range of one another, with
// nothing waiting. `pump` makes the frames sent so far receivable (a no-op for a FakeLink without delay; a short wait
// for sockets). All three are ended on return.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include "../../game_core/LinkContract.h"
#include "ILink.h"
#include "Protocol.h"

namespace GameCore::LinkTest {

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
