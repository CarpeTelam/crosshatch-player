#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "FakeLink.h"
#include "LinkContract.h"
#include "Protocol.h"

// GameCore::LinkTest::FakeLink: the seeded hostile link (a stand-in for EspNowLink and UdpLink, FakeLink.h says
// which behaviours it keeps and which it relaxes), and the link contract it must agree with.

using namespace GameCore;
using namespace GameCore::LinkTest;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes frameOf(const uint8_t tag, const size_t size = 8) { return Bytes(size, tag); }

struct Got {
  bool any = false;
  PeerHandle from;
  Bytes frame;
};

Got takeFrom(FakeLink& link) {
  Got got;
  Bytes buffer(Protocol::MAX_FRAME_BYTES);
  size_t length = 0;
  got.any = link.receive(got.from, buffer, length);
  if (got.any) got.frame.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(length));
  return got;
}

size_t drain(FakeLink& link, std::vector<Bytes>* frames = nullptr) {
  size_t count = 0;
  for (Got got = takeFrom(link); got.any; got = takeFrom(link)) {
    count++;
    if (frames != nullptr) frames->push_back(got.frame);
  }
  return count;
}

struct Pair {
  ManualClock clock;
  FakeLinkNet net{clock, 7};
  FakeLink a{net};
  FakeLink b{net};
};

TEST(FakeLinkTest, ADefaultHandleIsInvalidAndSendsNowhere) {
  Pair p;
  const PeerHandle none;
  EXPECT_FALSE(none.valid());
  EXPECT_EQ(none, NO_PEER);
  EXPECT_EQ(none.value(), 0u);
  EXPECT_FALSE(p.a.send(none, frameOf(1)));
  EXPECT_EQ(p.net.refusedSends(), 1u);
  EXPECT_EQ(p.net.foreignHandleSends(), 0u) << "the invalid handle is not a foreign one";
  EXPECT_EQ(p.net.carried(), 0u);
}

TEST(FakeLinkTest, AReceivedHandleSendsBackToItsPeer) {
  Pair p;
  ASSERT_TRUE(p.a.broadcast(frameOf(1)));
  const Got first = takeFrom(p.b);
  ASSERT_TRUE(first.any);
  EXPECT_TRUE(first.from.valid());
  ASSERT_TRUE(p.b.send(first.from, frameOf(2)));
  const Got reply = takeFrom(p.a);
  ASSERT_TRUE(reply.any);
  EXPECT_EQ(reply.frame, frameOf(2));
  // The peer is named the same way every time, and the other way round is a different handle.
  ASSERT_TRUE(p.a.broadcast(frameOf(3)));
  EXPECT_EQ(takeFrom(p.b).from, first.from);
  EXPECT_EQ(p.a.handleOf(p.b), reply.from);
  EXPECT_EQ(p.b.handleOf(p.a), first.from);
  EXPECT_NE(reply.from, first.from);
}

TEST(FakeLinkTest, TwoPeersGetTwoHandlesAndNeverShareOne) {
  ManualClock clock;
  FakeLinkNet net(clock, 1);
  FakeLink host(net);
  FakeLink g1(net);
  FakeLink g2(net);
  ASSERT_TRUE(g1.broadcast(frameOf(1)));
  ASSERT_TRUE(g2.broadcast(frameOf(2)));
  const Got fromG1 = takeFrom(host);
  const Got fromG2 = takeFrom(host);
  ASSERT_TRUE(fromG1.any);
  ASSERT_TRUE(fromG2.any);
  EXPECT_NE(fromG1.from, fromG2.from);
  ASSERT_TRUE(host.send(fromG2.from, frameOf(9)));
  EXPECT_EQ(drain(g1), 1u) << "g1 heard g2's broadcast too";  // g2's broadcast reached g1, not the reply
  EXPECT_EQ(takeFrom(g2).frame, frameOf(1)) << "g2 heard g1's broadcast";
  EXPECT_EQ(takeFrom(g2).frame, frameOf(9));
}

TEST(FakeLinkTest, AForeignHandleReturnsFalseAndIsCounted) {
  ManualClock clock;
  FakeLinkNet net(clock, 1);
  FakeLink a(net);
  FakeLink b(net);
  FakeLink c(net);
  const PeerHandle aNamesB = a.handleOf(b);
  const PeerHandle bNamesC = b.handleOf(c);
  ASSERT_TRUE(aNamesB.valid());
  ASSERT_TRUE(bNamesC.valid());
  EXPECT_NE(aNamesB, bNamesC);
  EXPECT_FALSE(c.send(aNamesB, frameOf(1)));                            // a's handle on c
  EXPECT_FALSE(a.send(bNamesC, frameOf(1)));                            // b's handle on a
  EXPECT_FALSE(a.send(PeerHandle::fromValue(0x00010099), frameOf(1)));  // a's tag, but never given
  EXPECT_EQ(net.foreignHandleSends(), 3u);
  EXPECT_EQ(net.refusedSends(), 3u);
  EXPECT_EQ(net.carried(), 0u);
  EXPECT_TRUE(a.send(aNamesB, frameOf(1)));
  EXPECT_EQ(net.foreignHandleSends(), 3u) << "a good send is not counted";
  EXPECT_EQ(drain(b), 1u);
}

TEST(FakeLinkTest, HandleOfIsStableAndNeverTheLinkItself) {
  Pair p;
  EXPECT_EQ(p.a.handleOf(p.b), p.a.handleOf(p.b));
  EXPECT_FALSE(p.a.handleOf(p.a).valid());
  ManualClock otherClock;
  FakeLinkNet otherNet(otherClock);
  FakeLink stranger(otherNet);
  EXPECT_FALSE(p.a.handleOf(stranger).valid());
}

TEST(FakeLinkTest, ABroadcastReachesTheOthersAndNotTheSender) {
  ManualClock clock;
  FakeLinkNet net(clock, 3);
  FakeLink a(net);
  FakeLink b(net);
  FakeLink c(net);
  ASSERT_TRUE(a.broadcast(frameOf(5, 30)));
  EXPECT_EQ(net.carried(), 2u);
  EXPECT_EQ(drain(a), 0u);
  std::vector<Bytes> forB;
  std::vector<Bytes> forC;
  EXPECT_EQ(drain(b, &forB), 1u);
  EXPECT_EQ(drain(c, &forC), 1u);
  EXPECT_EQ(forB[0], frameOf(5, 30));
  EXPECT_EQ(forC[0], frameOf(5, 30));
}

TEST(FakeLinkTest, ABroadcastTakesEachReceiversOwnLossRoll) {
  ManualClock clock;
  FakeLinkNet net(clock, 11);
  Faults f;
  f.lossPercent = 50;
  net.setFaults(f);
  FakeLink sender(net);
  FakeLink b(net);
  FakeLink c(net);
  size_t both = 0;
  size_t onlyOne = 0;
  for (int i = 0; i < 200; i++) {
    ASSERT_TRUE(sender.broadcast(frameOf(1)));
    const size_t got = drain(b) + drain(c);
    both += got == 2;
    onlyOne += got == 1;
  }
  EXPECT_GT(onlyOne, 0u) << "one shared verdict per broadcast would make every outcome all or nothing";
  EXPECT_GT(both, 0u);
}

TEST(FakeLinkTest, TotalLossDropsEverythingButSendSaysTrue) {
  Pair p;
  Faults f;
  f.lossPercent = 100;
  p.net.setFaults(f);
  const PeerHandle toB = p.a.handleOf(p.b);
  EXPECT_TRUE(p.a.send(toB, frameOf(1)));
  EXPECT_TRUE(p.a.broadcast(frameOf(2)));
  EXPECT_EQ(drain(p.b), 0u);
  EXPECT_EQ(p.net.carried(), 0u);
  EXPECT_EQ(p.net.dropped(), 2u);
}

TEST(FakeLinkTest, TotalDuplicationDeliversEveryFrameTwice) {
  Pair p;
  Faults f;
  f.duplicatePercent = 100;
  p.net.setFaults(f);
  ASSERT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(4)));
  EXPECT_EQ(p.net.carried(), 2u);
  std::vector<Bytes> frames;
  EXPECT_EQ(drain(p.b, &frames), 2u);
  EXPECT_EQ(frames[0], frames[1]);
}

TEST(FakeLinkTest, DelayHoldsAFrameUntilTheFakeClockReachesIt) {
  Pair p;
  Faults f;
  f.delayMs = 100;
  p.net.setFaults(f);
  ASSERT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(1)));
  EXPECT_EQ(p.net.waiting(), 1u);
  EXPECT_FALSE(takeFrom(p.b).any);
  p.clock.advance(99);
  EXPECT_FALSE(takeFrom(p.b).any);
  p.clock.advance(1);
  EXPECT_TRUE(takeFrom(p.b).any);
  EXPECT_EQ(p.net.waiting(), 0u);
}

TEST(FakeLinkTest, JitterStaysWithinItsBoundAndVaries) {
  Pair p;
  Faults f;
  f.jitterMs = 30;
  p.net.setFaults(f);
  const PeerHandle toB = p.a.handleOf(p.b);
  std::vector<uint64_t> arrivals;
  for (int i = 0; i < 100; i++) {
    const uint64_t sentAt = p.clock.nowMs();
    ASSERT_TRUE(p.a.send(toB, frameOf(1)));
    uint64_t waited = 0;
    while (!takeFrom(p.b).any) {
      p.clock.advance(1);
      waited++;
      ASSERT_LE(waited, 30u);
    }
    arrivals.push_back(p.clock.nowMs() - sentAt);
  }
  EXPECT_GT(*std::max_element(arrivals.begin(), arrivals.end()), *std::min_element(arrivals.begin(), arrivals.end()));
}

TEST(FakeLinkTest, ReorderLetsALaterFrameOvertakeAHeldBackOne) {
  Pair p;
  const PeerHandle toB = p.a.handleOf(p.b);
  Faults slow;
  slow.reorderPercent = 100;
  slow.reorderExtraMs = 50;
  p.net.setFaults(slow);
  ASSERT_TRUE(p.a.send(toB, frameOf(1)));
  p.net.setFaults(Faults());
  ASSERT_TRUE(p.a.send(toB, frameOf(2)));
  EXPECT_EQ(takeFrom(p.b).frame, frameOf(2));
  EXPECT_FALSE(takeFrom(p.b).any);
  p.clock.advance(50);
  EXPECT_EQ(takeFrom(p.b).frame, frameOf(1));
}

TEST(FakeLinkTest, WithoutFaultsFramesArriveInOrderAndWhole) {
  Pair p;
  const PeerHandle toB = p.a.handleOf(p.b);
  for (uint8_t i = 1; i <= 20; i++) ASSERT_TRUE(p.a.send(toB, frameOf(i, i)));
  for (uint8_t i = 1; i <= 20; i++) {
    const Got got = takeFrom(p.b);
    ASSERT_TRUE(got.any);
    EXPECT_EQ(got.frame, frameOf(i, i));
  }
  EXPECT_FALSE(takeFrom(p.b).any);
}

// Everything a net does to a fixed script of traffic: when and where each copy came out.
using Trace = std::vector<std::tuple<uint64_t, size_t, Bytes>>;

Trace runScript(const uint32_t seed, const Faults& faults) {
  ManualClock clock;
  FakeLinkNet net(clock, seed);
  net.setFaults(faults);
  FakeLink a(net);
  FakeLink b(net);
  FakeLink c(net);
  FakeLink* const links[] = {&a, &b, &c};
  Trace trace;
  for (uint8_t i = 0; i < 120; i++) {
    FakeLink& sender = *links[i % 3];
    if (i % 4 == 0) {
      sender.broadcast(frameOf(i, 3 + i % 5));
    } else {
      sender.send(sender.handleOf(*links[(i + 1) % 3]), frameOf(i, 3 + i % 5));
    }
    clock.advance(3);
    for (FakeLink* link : links) {
      for (Got got = takeFrom(*link); got.any; got = takeFrom(*link)) {
        trace.emplace_back(clock.nowMs(), link->index(), got.frame);
      }
    }
  }
  clock.advance(1000);
  for (FakeLink* link : links) {
    for (Got got = takeFrom(*link); got.any; got = takeFrom(*link)) {
      trace.emplace_back(clock.nowMs(), link->index(), got.frame);
    }
  }
  trace.emplace_back(net.carried(), net.dropped(), Bytes());
  return trace;
}

Faults hostile() {
  Faults f;
  f.lossPercent = 25;
  f.duplicatePercent = 25;
  f.reorderPercent = 30;
  f.delayMs = 5;
  f.jitterMs = 20;
  f.reorderExtraMs = 40;
  return f;
}

TEST(FakeLinkTest, ASeedRepeatsExactlyAndAnotherSeedDiffers) {
  const Trace first = runScript(1234, hostile());
  const Trace again = runScript(1234, hostile());
  const Trace other = runScript(4321, hostile());
  EXPECT_GT(first.size(), 50u);
  EXPECT_EQ(first, again);
  EXPECT_NE(first, other);
}

TEST(FakeLinkTest, SeedZeroIsMappedToAWorkingSeed) {
  const Trace zero = runScript(0, hostile());
  EXPECT_EQ(zero, runScript(0, hostile()));
  EXPECT_NE(zero, runScript(1, hostile()));
  // A xorshift stuck at zero would deliver everything or nothing.
  EXPECT_GT(zero.size(), 20u);
  EXPECT_LT(std::get<0>(zero.back()), 400u);
}

// The loss, jitter, delay, and reorder knobs never change the number of rolls, so with one seed (and no duplicates)
// changing one leaves the others' decisions alone.
std::map<uint8_t, uint64_t> deliveries(const Faults& faults) {
  ManualClock clock;
  FakeLinkNet net(clock, 99);
  net.setFaults(faults);
  FakeLink a(net);
  FakeLink b(net);
  const PeerHandle toB = a.handleOf(b);
  for (uint8_t i = 0; i < 60; i++) a.send(toB, frameOf(i));
  std::map<uint8_t, uint64_t> arrived;  // frame tag -> time
  for (uint64_t t = 0; t <= 200; t++) {
    clock.set(t);
    for (Got got = takeFrom(b); got.any; got = takeFrom(b)) arrived[got.frame[0]] = t;
  }
  return arrived;
}

TEST(FakeLinkTest, LossDoesNotReshuffleJitterOrReorder) {
  Faults f;
  f.jitterMs = 50;
  f.reorderPercent = 30;
  f.reorderExtraMs = 40;
  const auto lossless = deliveries(f);
  f.lossPercent = 40;
  const auto lossy = deliveries(f);
  ASSERT_EQ(lossless.size(), 60u);
  EXPECT_LT(lossy.size(), 60u);
  EXPECT_GT(lossy.size(), 10u);
  for (const auto& [tag, time] : lossy) {
    ASSERT_TRUE(lossless.count(tag));
    EXPECT_EQ(time, lossless.at(tag)) << "frame " << int(tag) << " moved when only the loss changed";
  }
}

TEST(FakeLinkTest, JitterDoesNotReshuffleLossOrReorder) {
  Faults f;
  f.lossPercent = 40;
  f.reorderPercent = 30;
  f.reorderExtraMs = 40;
  f.jitterMs = 50;
  const auto wide = deliveries(f);
  f.jitterMs = 10;
  const auto narrow = deliveries(f);
  ASSERT_LT(wide.size(), 60u);
  ASSERT_GT(wide.size(), 10u);
  for (const auto& [tag, time] : wide)
    EXPECT_TRUE(narrow.count(tag)) << "frame " << int(tag) << " lost or kept differently";
  EXPECT_EQ(wide.size(), narrow.size()) << "the same frames survive whatever the jitter";
  for (const auto& [tag, time] : narrow) EXPECT_LE(time, 10u + 40u);
}

TEST(FakeLinkTest, ReorderDoesNotReshuffleLossOrJitter) {
  Faults f;
  f.lossPercent = 40;
  f.jitterMs = 50;
  const auto plain = deliveries(f);
  f.reorderPercent = 50;
  f.reorderExtraMs = 30;
  const auto reordered = deliveries(f);
  ASSERT_LT(plain.size(), 60u);
  ASSERT_GT(plain.size(), 10u);
  EXPECT_EQ(plain.size(), reordered.size()) << "the same frames survive whatever the reorder";
  size_t held = 0;
  for (const auto& [tag, time] : plain) {
    ASSERT_TRUE(reordered.count(tag));
    const uint64_t later = reordered.at(tag);
    EXPECT_TRUE(later == time || later == time + 30) << "frame " << int(tag) << " moved by " << later - time;
    held += later == time + 30;
  }
  EXPECT_GT(held, 0u);
  EXPECT_LT(held, plain.size());
}

TEST(FakeLinkTest, DelayShiftsEveryDeliveryAndNothingElse) {
  Faults f;
  f.lossPercent = 40;
  f.jitterMs = 50;
  f.reorderPercent = 30;
  f.reorderExtraMs = 40;
  const auto none = deliveries(f);
  f.delayMs = 25;
  const auto delayed = deliveries(f);
  ASSERT_EQ(none.size(), delayed.size());
  for (const auto& [tag, time] : none) {
    ASSERT_TRUE(delayed.count(tag));
    EXPECT_EQ(delayed.at(tag), time + 25);
  }
}

TEST(FakeLinkTest, AJitterOfTheLargestValueDoesNotDivideByZero) {
  Pair p;
  Faults f;
  f.jitterMs = UINT32_MAX;
  p.net.setFaults(f);
  EXPECT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(1)));
  EXPECT_EQ(p.net.carried(), 1u);
}

TEST(FakeLinkTest, AHandleFromAnotherNetIsForeignEvenWithTheSameEndpointAndCounter) {
  ManualClock clock;
  FakeLinkNet net1(clock, 1);
  FakeLinkNet net2(clock, 1);
  FakeLink a1(net1);
  FakeLink b1(net1);
  FakeLink a2(net2);
  FakeLink b2(net2);
  const PeerHandle a1NamesB1 = a1.handleOf(b1);
  const PeerHandle a2NamesB2 = a2.handleOf(b2);
  ASSERT_TRUE(a1NamesB1.valid());
  ASSERT_TRUE(a2NamesB2.valid());
  EXPECT_NE(a1NamesB1, a2NamesB2) << "handle values are unique across link instances";
  EXPECT_FALSE(a2.send(a1NamesB1, frameOf(1)));
  EXPECT_FALSE(a1.send(a2NamesB2, frameOf(1)));
  EXPECT_EQ(net1.foreignHandleSends(), 1u);
  EXPECT_EQ(net2.foreignHandleSends(), 1u);
  EXPECT_EQ(net1.carried() + net2.carried(), 0u);
  EXPECT_EQ(drain(b1) + drain(b2), 0u);
  EXPECT_TRUE(a1.send(a1NamesB1, frameOf(2)));
  EXPECT_EQ(drain(b1), 1u);
}

TEST(FakeLinkTest, FaultsCanBeSetForOneDirectionOnly) {
  Pair p;
  Faults f;
  f.lossPercent = 100;
  p.net.setFaults(p.a.index(), p.b.index(), f);
  ASSERT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(1)));
  ASSERT_TRUE(p.b.send(p.b.handleOf(p.a), frameOf(2)));
  EXPECT_EQ(drain(p.b), 0u);
  EXPECT_EQ(drain(p.a), 1u);
}

TEST(FakeLinkTest, ABlackholeDropsOneSourceAndStillSaysTrue) {
  Pair p;
  p.net.blackhole(p.a.index());
  EXPECT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(1)));
  EXPECT_TRUE(p.a.broadcast(frameOf(2)));
  EXPECT_EQ(drain(p.b), 0u);
  EXPECT_EQ(p.net.carried(), 0u);
  ASSERT_TRUE(p.b.send(p.b.handleOf(p.a), frameOf(3)));
  EXPECT_EQ(drain(p.a), 1u) << "the other direction still works";
  p.net.blackhole(p.a.index(), false);
  ASSERT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(4)));
  EXPECT_EQ(drain(p.b), 1u);
}

// A looping echo is correct on every state assertion and still saturates a real radio: the carried budget
// turns that into a failing assertion.
TEST(FakeLinkTest, APacketStormTripsTheCarriedBudget) {
  Pair p;
  p.net.setCarriedBudget(50);
  ASSERT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(1)));
  for (int i = 0; i < 200 && !p.net.overBudget(); i++) {
    for (FakeLink* link : {&p.a, &p.b}) {
      const Got got = takeFrom(*link);
      if (got.any) link->send(got.from, got.frame);
    }
  }
  EXPECT_TRUE(p.net.overBudget());
  EXPECT_GT(p.net.carried(), 50u);
}

TEST(FakeLinkTest, ModestTrafficStaysWithinTheBudget) {
  Pair p;
  p.net.setCarriedBudget(10);
  const PeerHandle toB = p.a.handleOf(p.b);
  for (int i = 0; i < 10; i++) ASSERT_TRUE(p.a.send(toB, frameOf(1)));
  EXPECT_EQ(p.net.carried(), 10u);
  EXPECT_FALSE(p.net.overBudget());
  ASSERT_TRUE(p.a.send(toB, frameOf(1)));
  EXPECT_TRUE(p.net.overBudget());
}

TEST(FakeLinkTest, AFrameLongerThanTheBufferIsDroppedAndTheNextOneDelivered) {
  Pair p;
  const PeerHandle toB = p.a.handleOf(p.b);
  ASSERT_TRUE(p.a.send(toB, frameOf(1, 100)));
  ASSERT_TRUE(p.a.send(toB, frameOf(2, 20)));
  Bytes small(50, 0xAA);
  PeerHandle from;
  size_t length = 0;
  ASSERT_TRUE(p.b.receive(from, small, length));
  EXPECT_EQ(length, 20u);
  EXPECT_EQ(small[0], 2);
  EXPECT_EQ(small[20], 0xAA) << "no byte past the frame is written";
  EXPECT_EQ(p.net.droppedForCapacity(), 1u);
  EXPECT_FALSE(p.b.receive(from, small, length));
  // With only the long frame waiting, receive says false and leaves its outputs.
  ASSERT_TRUE(p.a.send(toB, frameOf(3, 100)));
  PeerHandle keep = PeerHandle::fromValue(77);
  size_t keepLength = 5;
  EXPECT_FALSE(p.b.receive(keep, small, keepLength));
  EXPECT_EQ(keep.value(), 77u);
  EXPECT_EQ(keepLength, 5u);
  EXPECT_EQ(p.net.droppedForCapacity(), 2u);
}

TEST(FakeLinkTest, EmptyAndOverLimitFramesAreRefused) {
  Pair p;
  const PeerHandle toB = p.a.handleOf(p.b);
  EXPECT_FALSE(p.a.send(toB, std::span<const uint8_t>()));
  EXPECT_FALSE(p.a.broadcast(std::span<const uint8_t>()));
  EXPECT_FALSE(p.a.send(toB, frameOf(1, Protocol::MAX_FRAME_BYTES + 1)));
  EXPECT_FALSE(p.a.broadcast(frameOf(1, Protocol::MAX_FRAME_BYTES + 1)));
  EXPECT_EQ(p.net.refusedSends(), 4u);
  EXPECT_EQ(p.net.foreignHandleSends(), 0u);
  EXPECT_EQ(p.net.carried(), 0u);
  EXPECT_TRUE(p.a.send(toB, frameOf(1, Protocol::MAX_FRAME_BYTES)));
  EXPECT_TRUE(p.a.send(toB, frameOf(1, 1)));
  EXPECT_EQ(drain(p.b), 2u);
}

TEST(FakeLinkTest, TheSenderMayReuseItsBufferAtOnce) {
  Pair p;
  Bytes buffer = frameOf(7, 16);
  ASSERT_TRUE(p.a.send(p.a.handleOf(p.b), buffer));
  std::fill(buffer.begin(), buffer.end(), uint8_t{0});
  EXPECT_EQ(takeFrom(p.b).frame, frameOf(7, 16));
}

TEST(FakeLinkTest, EndTwiceIsSafeAndStopsAllTraffic) {
  ManualClock clock;
  FakeLinkNet net(clock, 1);
  FakeLink a(net);
  FakeLink b(net);
  FakeLink c(net);
  const PeerHandle aNamesB = a.handleOf(b);
  ASSERT_TRUE(b.send(b.handleOf(a), frameOf(1)));
  ASSERT_TRUE(a.broadcast(frameOf(2)));
  a.end();
  a.end();
  EXPECT_TRUE(a.ended());
  EXPECT_FALSE(a.send(aNamesB, frameOf(3)));
  EXPECT_FALSE(a.broadcast(frameOf(3)));
  EXPECT_FALSE(takeFrom(a).any) << "frames waiting at an ended link are discarded";
  EXPECT_FALSE(a.handleOf(b).valid());
  // Others' traffic does not reach it, and a broadcast skips it without counting a copy.
  const uint64_t before = net.carried();
  ASSERT_TRUE(b.broadcast(frameOf(4)));
  EXPECT_EQ(net.carried(), before + 1) << "only c is still listening";
  EXPECT_EQ(drain(c), 2u) << "c still hears a's earlier broadcast and b's";
  ASSERT_TRUE(b.send(b.handleOf(a), frameOf(5)));
  EXPECT_FALSE(takeFrom(a).any);
  // Exactly one frame waits for b: a's earlier broadcast (frame 2), still in flight because end() brings a's link
  // down, not the air. b's own earlier send to a was discarded with a's queue, and c's traffic is c's.
  EXPECT_EQ(drain(b), 1u);
  c.end();
  b.end();
  EXPECT_FALSE(b.broadcast(frameOf(6)));
}

TEST(FakeLinkTest, AFrameInFlightIsNotRecalledByItsSendersEnd) {
  Pair p;
  Faults f;
  f.delayMs = 10;
  p.net.setFaults(f);
  ASSERT_TRUE(p.a.send(p.a.handleOf(p.b), frameOf(1)));
  p.a.end();
  p.clock.advance(10);
  EXPECT_EQ(takeFrom(p.b).frame, frameOf(1));
}

TEST(FakeLinkTest, RefuseSendsRefusesTheNextNCallsAndThenSendsAgain) {
  Pair p;
  const PeerHandle toB = p.a.handleOf(p.b);
  p.a.refuseSends(2);
  EXPECT_FALSE(p.a.send(toB, frameOf(1)));
  EXPECT_FALSE(p.a.broadcast(frameOf(2))) << "broadcast shares the knob";
  EXPECT_EQ(p.net.refusedSends(), 2u);
  EXPECT_EQ(p.net.foreignHandleSends(), 0u) << "a full queue is not a foreign handle";
  EXPECT_EQ(p.net.carried(), 0u);
  EXPECT_EQ(p.net.waiting(), 0u) << "a refused call queues nothing";
  EXPECT_TRUE(p.a.send(toB, frameOf(3))) << "the (n+1)th call succeeds";
  EXPECT_TRUE(p.a.broadcast(frameOf(4)));
  EXPECT_EQ(p.net.refusedSends(), 2u);
  std::vector<Bytes> frames;
  EXPECT_EQ(drain(p.b, &frames), 2u);
  EXPECT_EQ(frames, (std::vector<Bytes>{frameOf(3), frameOf(4)}));
}

TEST(FakeLinkTest, RefuseSendsIsOnlyUsedUpByCallsThatWouldHaveSucceeded) {
  Pair p;
  const PeerHandle toB = p.a.handleOf(p.b);
  p.a.refuseSends(1);
  EXPECT_FALSE(p.a.send(NO_PEER, frameOf(1)));
  EXPECT_FALSE(p.a.send(PeerHandle::fromValue(0xFFFFFFu), frameOf(1)));
  EXPECT_FALSE(p.a.send(toB, Bytes{}));
  EXPECT_FALSE(p.a.send(toB, Bytes(Protocol::MAX_FRAME_BYTES + 1, 1)));
  EXPECT_FALSE(p.a.broadcast(Bytes{}));
  EXPECT_EQ(p.net.refusedSends(), 5u);
  EXPECT_EQ(p.net.foreignHandleSends(), 1u) << "only the made-up handle is foreign";
  EXPECT_FALSE(p.a.send(toB, frameOf(2))) << "the one refusal is still waiting";
  EXPECT_EQ(p.net.refusedSends(), 6u);
  EXPECT_EQ(p.net.foreignHandleSends(), 1u);
  EXPECT_TRUE(p.a.send(toB, frameOf(3)));
  EXPECT_EQ(p.net.carried(), 1u);
}

TEST(FakeLinkTest, RefuseSendsAgreesWithTheDeviceContractAndDrawsNoRolls) {
  // ILink.h: a false is "nothing sent" and leaves no trace but the caller's loss. Two nets with one seed: one sees a
  // refused call first, the other does not; every later frame meets the same fate, so a refusal drew no roll.
  Faults f;
  f.lossPercent = 50;
  f.duplicatePercent = 30;
  f.reorderPercent = 30;
  f.delayMs = 5;
  f.jitterMs = 20;
  f.reorderExtraMs = 40;
  std::vector<Bytes> got[2];
  for (int run = 0; run < 2; run++) {
    Pair p;
    p.net.setFaults(f);
    const PeerHandle toB = p.a.handleOf(p.b);
    if (run == 1) {
      p.a.refuseSends(3);
      for (int i = 0; i < 3; i++) EXPECT_FALSE(p.a.send(toB, frameOf(99)));
    }
    for (uint8_t i = 0; i < 60; i++) ASSERT_TRUE(p.a.send(toB, frameOf(i)));
    p.clock.advance(1000);
    drain(p.b, &got[run]);
  }
  EXPECT_FALSE(got[0].empty());
  EXPECT_EQ(got[0], got[1]);
}

TEST(FakeLinkTest, FakeLinkAgreesWithTheLinkContract) {
  Pair p;
  expectLinkContract(p.a, p.b, [] {});
}

TEST(FakeLinkTest, FakeLinkWithDelayAgreesWithTheLinkContractOnceFramesAreDue) {
  Pair p;
  Faults f;
  f.delayMs = 25;
  p.net.setFaults(f);
  expectLinkContract(p.a, p.b, [&p] { p.clock.advance(25); });
}

TEST(FakeLinkTest, RestartKeepsThePeersHandleForItAndKillsItsOwnOldHandles) {
  Pair p;
  const PeerHandle aNamesB = p.a.handleOf(p.b);
  const PeerHandle bNamesA = p.b.handleOf(p.a);
  ASSERT_TRUE(p.b.broadcast(frameOf(1)));
  const Got heard = takeFrom(p.a);
  ASSERT_TRUE(heard.any);
  ASSERT_EQ(heard.from, aNamesB);
  p.a.restart();
  EXPECT_FALSE(p.a.ended());
  // The handles a gave before are dead; the ones it gives now differ from them.
  EXPECT_FALSE(p.a.send(aNamesB, frameOf(2)));
  EXPECT_EQ(p.net.foreignHandleSends(), 1u);
  const PeerHandle again = p.a.handleOf(p.b);
  EXPECT_TRUE(again.valid());
  EXPECT_NE(again, aNamesB);
  // The handle b holds for a is the same, and traffic flows both ways.
  EXPECT_EQ(p.b.handleOf(p.a), bNamesA);
  ASSERT_TRUE(p.b.send(bNamesA, frameOf(3)));
  const Got toA = takeFrom(p.a);
  ASSERT_TRUE(toA.any);
  EXPECT_EQ(toA.frame, frameOf(3));
  EXPECT_EQ(toA.from, again);
  ASSERT_TRUE(p.a.send(again, frameOf(4)));
  const Got toB = takeFrom(p.b);
  ASSERT_TRUE(toB.any);
  EXPECT_EQ(toB.from, bNamesA);
  EXPECT_EQ(toB.frame, frameOf(4));
}

TEST(FakeLinkTest, RestartDiscardsWhatWasInFlightToTheLink) {
  Pair p;
  Faults f;
  f.delayMs = 10;
  p.net.setFaults(f);
  ASSERT_TRUE(p.b.send(p.b.handleOf(p.a), frameOf(1)));
  ASSERT_TRUE(p.a.broadcast(frameOf(2)));
  p.a.restart();
  p.clock.advance(10);
  EXPECT_FALSE(takeFrom(p.a).any) << "the device was down while it was in flight";
  EXPECT_EQ(takeFrom(p.b).frame, frameOf(2)) << "what the link itself sent before is still on its way";
  // Copies sent after the restart arrive.
  ASSERT_TRUE(p.b.send(p.b.handleOf(p.a), frameOf(3)));
  p.clock.advance(10);
  EXPECT_EQ(takeFrom(p.a).frame, frameOf(3));
}

TEST(FakeLinkTest, RestartAfterEndAndTwiceIsSafe) {
  Pair p;
  p.a.end();
  p.a.restart();
  EXPECT_FALSE(p.a.ended());
  p.a.restart();
  ASSERT_TRUE(p.a.broadcast(frameOf(1)));
  EXPECT_EQ(takeFrom(p.b).frame, frameOf(1));
}

TEST(FakeLinkTest, ARestartedLinkAgreesWithTheLinkContract) {
  Pair p;
  p.a.restart();
  p.b.restart();
  expectLinkContract(p.a, p.b, [] {});
}

TEST(FakeLinkTest, ManualClockAdvancesAndSets) {
  ManualClock clock;
  EXPECT_EQ(clock.nowMs(), 0u);
  clock.advance(5);
  clock.advance(7);
  EXPECT_EQ(clock.nowMs(), 12u);
  clock.set(3);
  EXPECT_EQ(clock.nowMs(), 3u);
}

}  // namespace
