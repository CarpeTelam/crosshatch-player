#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "NearbySessionSupport.h"

// leave(reason, flushMs) of NearbySession (entry 8 of epic-play-nearby, R6): with a flush it waits until the shipment
// is acked or the deadline, then puts ABORT on the link three times; without one it does so at once. The deadline is
// the caller's (entry 15's 800 ms). Time bounds are fake milliseconds (estimates until a device run).

namespace {

using namespace nearby;
using GameCore::Protocol::AbortReason;

TEST(NearbyLeaveTest, WithAFlushItReturnsAtTheAckAndThenPutsThreeAbortsOnTheLink) {
  Duo d;
  // A slow step, so the shipment is still only staged when leave() looks (the task has not yet carried it to the link).
  NearbySession::Options slow = fastOptions();
  slow.stepMs = 60;
  d.openBoth(fastSettings(60000), slow);
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  // Nothing is unacked (the ACCEPT is acked), so only the staged shipment can hold the flush.
  ASSERT_TRUE(waitFor([&] { return d.host->counters().unacked == 0; }));
  TestShipment move(1, false);
  EXPECT_FALSE(d.host->ship(move.shipment));  // staged: the frame is not even with the link yet
  const int abortsBefore = d.hostProbe.abortFrames.load();
  const NearbySession::LeaveResult result = d.host->leave(AbortReason::Left, 800);
  EXPECT_LT(result.waitedMs, 800u) << "returned at the ack, not at the deadline";
  EXPECT_EQ(result.unackedAtSend, 0u) << "the frame was acked first";
  EXPECT_EQ(result.copies, 3);
  // The shipment that was only staged when leave() was called is with the link before the goodbye goes out (the ABORT
  // is unsequenced and must not overtake the last STATE, or the peer never gets the last move).
  EXPECT_EQ(linkLines("state sent ver=1"), 1u);
  EXPECT_EQ(d.hostProbe.abortFrames.load() - abortsBefore, 3);
  EXPECT_EQ(d.host->counters().link.abortsSent, 3u);
  EXPECT_EQ(d.host->counters().unacked, 0u);
  EXPECT_EQ(linkLines("leave reason=left waited="), 1u);
  EXPECT_EQ(linkLines(" unacked=0 copies=3"), 1u);

  // The peer hears it.
  ASSERT_TRUE(waitFor([&] { return d.guest->endLatched(); }));
  AbortReason reason = AbortReason::Timeout;
  EXPECT_TRUE(d.guest->takeEnd(reason));
  EXPECT_EQ(reason, AbortReason::Left);
  ASSERT_TRUE(waitFor([&] { return d.guest->counters().endIgnored == 2; })) << "all three copies arrived";
  EXPECT_EQ(linkLines("abort recv reason=left seat=1"), 3u);
  ASSERT_TRUE(waitFor([&] { return linkLines("abort sent n=3 total=3") == 1u; }));
  expectNoRenderLockFromTasks();
}

TEST(NearbyLeaveTest, AnUnreachablePeerEndsTheWaitAtTheDeadline) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  d.net.blackhole(d.hostEnd.index());  // everything the host sends is lost, and says true
  TestShipment move(1, false);
  EXPECT_FALSE(d.host->ship(move.shipment));
  ASSERT_TRUE(waitFor([&] { return d.host->counters().unacked == 1; }));
  const NearbySession::LeaveResult result = d.host->leave(AbortReason::Left, 800);
  EXPECT_GE(result.waitedMs, 800u) << "never before the deadline";
  EXPECT_LT(result.waitedMs, 1100u) << "and at it, not long after";
  EXPECT_GE(result.unackedAtSend, 1u);
  EXPECT_EQ(result.copies, 3) << "the ABORTs go out all the same";
  EXPECT_EQ(d.hostProbe.abortFrames.load(), 3);
  EXPECT_EQ(linkLines("leave reason=left waited="), 1u);
  EXPECT_EQ(linkLines(" copies=3"), 1u);
}

TEST(NearbyLeaveTest, WithoutAFlushThreeAbortsGoAtOnce) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  d.net.blackhole(d.hostEnd.index());
  TestShipment move(1, false);
  EXPECT_FALSE(d.host->ship(move.shipment));
  ASSERT_TRUE(waitFor([&] { return d.host->counters().unacked == 1; }));
  const NearbySession::LeaveResult result = d.host->leave(AbortReason::ScriptError, 0);
  EXPECT_EQ(result.waitedMs, 0u) << "it did not wait";
  EXPECT_GE(result.unackedAtSend, 1u) << "an unacked frame does not hold the goodbye back";
  EXPECT_EQ(result.copies, 3);
  EXPECT_EQ(d.hostProbe.abortFrames.load(), 3);
  EXPECT_EQ(linkLines("leave reason=script_error waited=0ms"), 1u);
}

TEST(NearbyLeaveTest, ARefusedAbortIsNotACopy) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  d.hostProbe.refuseSends = 1;  // the link's send queue is full for the first copy only
  const NearbySession::LeaveResult result = d.host->leave(AbortReason::Left, 0);
  EXPECT_EQ(result.copies, 2);
  EXPECT_EQ(d.hostProbe.abortFrames.load(), 3) << "all three were handed to the link";
}

// The guest left first: its ABORT(left) reached the host, whose "player left" view then leaves too (Back, or a forced
// exit with no flush). The guest's session is over, so the host sends it nothing, and the leave line says copies=0
// (smoke run 2026-10-13: three wasted ABORTs and copies=3).
TEST(NearbyLeaveTest, AfterThePeersAbortItSendsThePeerNothing) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  EXPECT_EQ(d.guest->leave(AbortReason::Left, 0).copies, 3);
  ASSERT_TRUE(waitFor([&] { return linkLines("abort recv reason=left seat=2") == 3u; }));
  ASSERT_TRUE(d.host->endLatched());
  for (const uint32_t flushMs : {0u, 800u}) {
    SCOPED_TRACE(flushMs);
    fakelog::clearLines();
    const int abortsBefore = d.hostProbe.abortFrames.load();
    const NearbySession::LeaveResult result = d.host->leave(AbortReason::Left, flushMs);
    EXPECT_EQ(result.copies, 0);
    EXPECT_EQ(d.hostProbe.abortFrames.load(), abortsBefore) << "an ABORT went to a peer that had left";
    EXPECT_EQ(linkLines("leave reason=left waited="), 1u);
    EXPECT_EQ(linkLines(" copies=0"), 1u);
  }
}

// A peer that went silent (Timeout, no ABORT heard) may still hear us one way: the three copies go out as before.
TEST(NearbyLeaveTest, AfterTheSilenceLimitItStillSendsThreeAborts) {
  Duo d;
  d.openBoth(fastSettings(1500));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  d.net.blackhole(d.guestEnd.index());  // the guest's frames no longer reach the host
  ASSERT_TRUE(waitFor([&] { return d.host->endLatched(); }));
  AbortReason reason = AbortReason::Left;
  ASSERT_TRUE(d.host->takeEnd(reason));
  ASSERT_EQ(reason, AbortReason::Timeout);
  const NearbySession::LeaveResult result = d.host->leave(AbortReason::Left, 0);
  EXPECT_EQ(result.copies, 3);
  EXPECT_EQ(d.hostProbe.abortFrames.load(), 3);
  EXPECT_EQ(linkLines(" copies=3"), 1u);
}

TEST(NearbyLeaveTest, BeforeTheMatchBeganItSendsNothing) {
  Duo d;
  d.openHost();
  ASSERT_NE(d.host, nullptr);
  const NearbySession::LeaveResult result = d.host->leave(AbortReason::Left, 800);
  EXPECT_EQ(result.waitedMs, 0u);
  EXPECT_EQ(result.copies, 0);
  EXPECT_EQ(d.hostProbe.abortFrames.load(), 0);
  EXPECT_EQ(linkLines("leave reason"), 0u);
}

TEST(NearbyLeaveTest, AClockThatNeverMovesCannotHangTheFlush) {
  // The host runs on a clock the test never moves, and its peer is a RawPeer that acks nothing: the ACCEPT stays
  // unacked, no deadline is ever reached by time, and the poll count (flushMs / 5 + 2) is what ends the wait.
  LinkTest::ManualClock frozen;
  fakertos::reset();
  fakepsram::reset();
  fakelog::clearLines();
  activityManager.reset();
  FakeLinkNet net(frozen);
  FakeLink hostEnd(net);
  LinkProbe probe;
  LobbyTest::FixedRandom random({0x5E551011u});
  LobbyGame game = LobbyTest::testGame();
  LobbyTest::RawPeer raw(net, frozen);
  {
    auto host = NearbySession::openHost(std::make_unique<ObservedLink>(hostEnd, probe), frozen, random, game,
                                        LobbyTest::bytesOf("host"), fastSettings(60000), fastOptions());
    ASSERT_NE(host, nullptr);
    raw.connect(hostEnd);
    ASSERT_TRUE(raw.put(LobbyTest::joinFrameFor(game), host->session(), 0));
    ASSERT_TRUE(waitFor([&] { return host->lobby().host()->seatsTaken() == 2; }));
    ASSERT_EQ(host->startHost(), StartResult::Started);
    ASSERT_TRUE(waitFor([&] { return host->counters().unacked == 1; }));
    const NearbySession::LeaveResult result = host->leave(AbortReason::Left, 800);
    EXPECT_EQ(result.waitedMs, 0u) << "the clock never moved";
    EXPECT_GE(result.unackedAtSend, 1u);
    EXPECT_EQ(result.copies, 3);
    EXPECT_EQ(probe.abortFrames.load(), 3);
    raw.drain();
    EXPECT_EQ(raw.countAborts(AbortReason::Left), 3u);
  }
  EXPECT_EQ(probe.endCalls.load(), 1);
  EXPECT_TRUE(fakertos::waitNoTasks());
}

}  // namespace
