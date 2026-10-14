#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "GameLinkSinkGate.h"
#include "NearbySessionSupport.h"

// NearbySession and its GameLink task over FakeLink on real threads (entry 8 of epic-play-nearby): the lobby to a round
// of CounterRules played by two test-driven Sessions, the three handoffs (a stopped consumer, a held `over`, a full
// queue, a latched end), ship, strangers and the ghost seat, teardown, and bring-up failure. NearbyLeaveTest has leave.
// Every time bound is in fake milliseconds, estimates until a device run; the tests wait on conditions, never on a
// fixed sleep.

namespace {

using namespace nearby;
using GameCore::Protocol::AbortReason;
using GameCore::Protocol::Type;

// The sum of the `n=` field of the stats lines that contain `part`, and the largest `total=`.
void sumStatsLines(const std::string& part, uint32_t& sumN, uint32_t& lastTotal, size_t& lines) {
  sumN = 0;
  lastTotal = 0;
  lines = 0;
  for (const std::string& line : fakelog::snapshot()) {
    if (line.find("LINK:") == std::string::npos || line.find(part) == std::string::npos) continue;
    unsigned n = 0, total = 0;
    const size_t at = line.find(" n=");
    if (at == std::string::npos || std::sscanf(line.c_str() + at, " n=%u total=%u", &n, &total) != 2) continue;
    sumN += n;
    lastTotal = std::max<uint32_t>(lastTotal, total);
    ++lines;
  }
}

// ---------------------------------------------------------------- lobby to round

TEST(NearbySessionTest, TheLobbyPhaseAdvertisesJoinsAndStarts) {
  Duo d;
  d.openBoth();
  ASSERT_NE(d.host, nullptr);
  ASSERT_NE(d.guest, nullptr);
  EXPECT_EQ(d.host->role(), NearbySession::Role::Host);
  EXPECT_EQ(d.guest->role(), NearbySession::Role::Guest);
  EXPECT_TRUE(d.host->taskRunning());
  EXPECT_NE(d.host->lobby().host(), nullptr);
  EXPECT_EQ(d.host->lobby().guest(), nullptr);
  EXPECT_NE(d.guest->lobby().guest(), nullptr);
  EXPECT_EQ(d.guest->lobby().host(), nullptr);
  EXPECT_EQ(d.host->session(), 0x5E551011u);
  EXPECT_EQ(d.guest->session(), 0u) << "00000000 before the guest joins";
  EXPECT_FALSE(d.host->matchBegun());
  EXPECT_FALSE(d.guest->beginMatch()) << "not before the lobby Started";
  EXPECT_EQ(d.guest->startHost(), StartResult::NotWaiting) << "a guest does not start";

  ASSERT_TRUE(waitFor([&] { return d.guest->lobby().guest()->hostCount() >= 1; }));
  {
    auto lobby = d.guest->lobby();
    const HostEntry& entry = lobby.guest()->host(0);
    EXPECT_EQ(std::string(entry.label.begin(), entry.label.begin() + entry.labelLen), "host");
    EXPECT_EQ(entry.session, 0x5E551011u);
  }
  ASSERT_TRUE(d.join());
  ASSERT_TRUE(waitFor([&] { return d.guest->session() == 0x5E551011u; }));
  EXPECT_EQ(d.host->startHost(), StartResult::Started);
  EXPECT_TRUE(d.host->matchBegun());
  EXPECT_EQ(d.host->roster().seats, 2);
  EXPECT_EQ(d.host->localSeat(), 1);
  ASSERT_TRUE(waitFor([&] { return d.guest->lobby().guest()->state() == GuestState::Started; }));
  EXPECT_TRUE(d.guest->beginMatch());
  EXPECT_TRUE(d.guest->beginMatch()) << "idempotent";
  EXPECT_EQ(d.guest->localSeat(), 2);
  EXPECT_EQ(d.guest->roster().seats, 2);
  EXPECT_EQ(d.host->startHost(), StartResult::NotWaiting) << "once";
  expectNoRenderLockFromTasks();
}

TEST(NearbySessionTest, TwoDrivenSessionsPlayARoundToTheSameFinalState) {
  Duo d;
  d.openBoth();
  ASSERT_NE(d.host, nullptr);
  ASSERT_NE(d.guest, nullptr);
  ASSERT_TRUE(d.joinAndStart());
  MatchDriver a(*d.host, 1), b(*d.guest, 2);
  ASSERT_TRUE(a.startRound());
  ASSERT_TRUE(waitFor([&] {
    a.step();
    b.step();
    return a.match.status().over && b.match.status().over && sameState(a.match, b.match);
  }));
  EXPECT_EQ(a.match.ver(), b.match.ver());
  EXPECT_GT(a.moves + b.moves, 2u);
  EXPECT_GT(a.moves, 0u);
  EXPECT_GT(b.moves, 0u) << "both devices moved";
  EXPECT_FALSE(a.ended);
  EXPECT_FALSE(b.ended);
  EXPECT_GE(std::count(b.verdicts.begin(), b.verdicts.end(), Adoption::RoundStarted), 1);
  EXPECT_EQ(std::count(a.verdicts.begin(), a.verdicts.end(), Adoption::RoundStarted), 0);

  // Each LINK event logged its line, and every LINK line names the session.
  EXPECT_GE(linkLines("state sent"), 2u);
  EXPECT_GE(linkLines("state recv"), 2u);
  EXPECT_GE(linkLines("state adopt"), 2u);
  EXPECT_EQ(linkLines("task started"), 2u);
  EXPECT_EQ(linkLines("task started; stack 4096 bytes"), 2u);
  const std::string hostId = "session=" + sessionHex(0x5E551011u);
  EXPECT_EQ(linkLines("session=00000000 task started"), 1u) << "the guest's first step was before it joined";
  EXPECT_EQ(linkLines(hostId + " task started"), 1u);
  for (const std::string& line : fakelog::snapshot()) {
    if (line.find("LINK:") != std::string::npos) {
      EXPECT_NE(line.find("session="), std::string::npos) << line;
    }
    if (line.find("state ") != std::string::npos && line.find("LINK:") != std::string::npos) {
      EXPECT_NE(line.find("ver="), std::string::npos) << line;
    }
  }
  EXPECT_GE(linkLines(hostId + " state adopt"), 1u);
  EXPECT_GE(linkLines(hostId + " state recv"), 1u);
  expectNoRenderLockFromTasks();
}

TEST(NearbySessionTest, ARoundOverAHostileNetEndsInTheSameState) {
  Duo d(7);
  d.net.setFaults(LobbyTest::hostile());
  d.openBoth(fastSettings(6000));
  ASSERT_NE(d.host, nullptr);
  ASSERT_NE(d.guest, nullptr);
  ASSERT_TRUE(d.joinAndStart());
  MatchDriver a(*d.host, 3), b(*d.guest, 4);
  ASSERT_TRUE(a.startRound());
  ASSERT_TRUE(waitFor(
      [&] {
        a.step();
        b.step();
        return a.match.status().over && b.match.status().over && sameState(a.match, b.match);
      },
      20000));
  EXPECT_FALSE(a.ended);
  EXPECT_FALSE(b.ended);
  expectNoRenderLockFromTasks();
}

// ---------------------------------------------------------------- the three handoffs

TEST(NearbySessionTest, AStoppedConsumerStillGetsItsFramesAckedAndTheSenderResends) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_NE(d.host, nullptr);
  ASSERT_NE(d.guest, nullptr);
  ASSERT_TRUE(d.joinAndStart());
  // Nothing reaches the guest at first: the host's frame is lost, and the host's task resends it.
  Faults lossy;
  lossy.lossPercent = 100;
  d.net.setFaults(d.hostEnd.index(), d.guestEnd.index(), lossy);
  MatchDriver a(*d.host, 1);
  a.playing = false;
  ASSERT_TRUE(a.startRound());
  ASSERT_TRUE(waitFor([&] {
    a.step();
    return d.host->counters().link.resends >= 2;
  }));
  EXPECT_EQ(d.guest->counters().queueSize, 0u);
  EXPECT_EQ(d.host->counters().unacked, 1u);
  EXPECT_GE(linkLines("resend n="), 1u);

  // The path heals; the guest never takes a state, yet its task queues the frame and acks it.
  d.net.setFaults(d.hostEnd.index(), d.guestEnd.index(), Faults{});
  ASSERT_TRUE(waitFor([&] { return d.host->counters().unacked == 0 && d.guest->counters().queueSize == 1; }));
  EXPECT_EQ(d.guest->counters().queued, 1u);
  uint32_t sumN = 0, lastTotal = 0;
  size_t lines = 0;
  sumStatsLines("resend n=", sumN, lastTotal, lines);
  EXPECT_EQ(sumN, d.host->counters().link.resends) << "each resend counted in exactly one line";
  EXPECT_EQ(lastTotal, d.host->counters().link.resends);
  expectNoRenderLockFromTasks();
}

TEST(NearbySessionTest, ALostPeerLatchesTimeoutAndTheEndIsReadOnce) {
  Duo d;
  d.openBoth(fastSettings(1500));
  ASSERT_NE(d.host, nullptr);
  ASSERT_NE(d.guest, nullptr);
  ASSERT_TRUE(d.joinAndStart());
  EXPECT_FALSE(d.host->endLatched());
  d.net.blackhole(d.guestEnd.index());  // the guest's frames no longer reach the host
  const uint64_t before = fakertos::S().nowMs.load();
  // The consumer is stopped the whole time: the reason waits in the slot.
  ASSERT_TRUE(waitFor([&] { return d.host->endLatched(); }));
  EXPECT_LT(fakertos::S().nowMs.load() - before, 1500u + 500u) << "within the silence limit plus a few steps";
  AbortReason reason = AbortReason::Left;
  EXPECT_TRUE(d.host->endLatched());
  EXPECT_TRUE(d.host->takeEnd(reason));
  EXPECT_EQ(reason, AbortReason::Timeout);
  EXPECT_FALSE(d.host->takeEnd(reason)) << "once";
  EXPECT_TRUE(d.host->endLatched()) << "taken, but still latched";
  EXPECT_EQ(d.host->counters().link.silentReports, 1u);
  EXPECT_EQ(linkLines("silence n=1 total=1"), 1u);
  EXPECT_FALSE(d.guest->endLatched()) << "only the host lost its peer";
  expectNoRenderLockFromTasks();
}

TEST(NearbySessionTest, AnOverHeldInTheQueueIsKeptWithTheNewestLaterStateAndDrainedInOrder) {
  RawHostRig rig;
  ASSERT_TRUE(rig.open());
  ASSERT_TRUE(rig.joinAndStart());
  ASSERT_TRUE(rig.putState(1, false));
  ASSERT_TRUE(rig.putState(2, true));
  ASSERT_TRUE(rig.putState(3, false));
  ASSERT_TRUE(rig.putState(4, false));
  ASSERT_TRUE(waitFor([&] {
    const NearbySession::Counters counts = rig.guest->counters();
    return counts.queued + counts.replaced == 4;
  }));
  const NearbySession::Counters counts = rig.guest->counters();
  EXPECT_EQ(counts.queueSize, 2u);
  EXPECT_EQ(counts.queued, 2u);
  EXPECT_EQ(counts.replaced, 2u);

  NearbySession::NearbyState first, second, none;
  ASSERT_TRUE(rig.guest->takeState(first));
  EXPECT_EQ(first.peer.ver, 2u);
  EXPECT_TRUE(first.peer.status.over) << "the over was kept";
  EXPECT_EQ(first.peer.fromSeat, 1);
  EXPECT_FALSE(first.peer.snapshot.empty());
  ASSERT_TRUE(rig.guest->takeState(second));
  EXPECT_EQ(second.peer.ver, 4u) << "the newest later STATE";
  EXPECT_FALSE(second.peer.status.over);
  EXPECT_FALSE(rig.guest->takeState(none));
  rig.guest->releaseState(first);
  rig.guest->releaseState(second);
  EXPECT_TRUE(first.peer.snapshot.empty());
  rig.guest->releaseState(first);  // a second release is harmless
  EXPECT_EQ(linkLines("state recv"), 4u);
  expectNoRenderLockFromTasks();
}

TEST(NearbySessionTest, AFullQueueLeavesTheFifthUnackedAndAResendDeliversItOnceTheConsumerTakesOne) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_NE(d.host, nullptr);
  ASSERT_NE(d.guest, nullptr);
  ASSERT_TRUE(d.joinAndStart());
  std::vector<std::unique_ptr<TestShipment>> shipments;
  for (uint16_t ver = 1; ver <= 5; ++ver) {
    shipments.push_back(std::make_unique<TestShipment>(ver, true));
    ASSERT_TRUE(waitFor([&] { return d.host->ship(shipments.back()->shipment); })) << "ver " << ver;
  }
  ASSERT_TRUE(waitFor([&] { return d.guest->counters().queueOverflows >= 1; }));
  EXPECT_EQ(d.guest->counters().queueSize, 4u);
  EXPECT_EQ(d.host->counters().unacked, 1u) << "the fifth is left unacked";
  EXPECT_GE(d.guest->counters().link.noRoom, 1u);
  // The first overflow line carries the count 1.
  EXPECT_EQ(linkLines("queue full ver=5 overflows=1"), 1u);
  EXPECT_GE(d.host->counters().link.framesSent, 5u);

  NearbySession::NearbyState taken;
  ASSERT_TRUE(d.guest->takeState(taken));
  EXPECT_EQ(taken.peer.ver, 1u);
  d.guest->releaseState(taken);
  ASSERT_TRUE(waitFor([&] { return d.host->counters().unacked == 0 && d.guest->counters().queueSize == 4; }));
  std::vector<uint16_t> order;
  while (d.guest->takeState(taken)) {
    order.push_back(taken.peer.ver);
    EXPECT_TRUE(taken.peer.status.over);
    d.guest->releaseState(taken);
  }
  EXPECT_EQ(order, (std::vector<uint16_t>{2, 3, 4, 5})) << "nothing lost, in order";
  expectNoRenderLockFromTasks();
}

TEST(NearbySessionTest, AdoptNextDoesNothingWhileAMoveIsPendingAndTakesTheStateAfter) {
  RawHostRig rig;
  ASSERT_TRUE(rig.open());
  ASSERT_TRUE(rig.joinAndStart());
  MatchDriver b(*rig.guest, 5);
  b.playing = false;
  // The raw host starts a round and gives the guest the turn.
  ASSERT_TRUE(rig.putState(1, false, 2));
  ASSERT_TRUE(waitFor([&] {
    b.step();
    return b.match.hasState();
  }));
  ASSERT_EQ(b.match.status().turn, 2);
  b.match.handle(MatchTest::tapEvent(), 2);
  ASSERT_TRUE(b.match.pending());
  ASSERT_TRUE(rig.putState(9, false, 1));
  ASSERT_TRUE(waitFor([&] { return rig.guest->counters().queueSize == 1; }));
  for (int i = 0; i < 5; ++i) {
    const NearbySession::AdoptStep step = rig.guest->adoptNext(b.match);
    EXPECT_FALSE(step.taken) << "a taken entry adopt() answers Busy is lost";
  }
  EXPECT_EQ(rig.guest->counters().queueSize, 1u);
  b.match.applyPending();
  const NearbySession::AdoptStep step = rig.guest->adoptNext(b.match);
  EXPECT_TRUE(step.taken);
  EXPECT_EQ(step.ver, 9u);
  EXPECT_EQ(step.fromSeat, 1);
  EXPECT_EQ(rig.guest->counters().queueSize, 0u);
  EXPECT_EQ(linkLines("state adopt ver=9 seat=1 result="), 1u);
  expectNoRenderLockFromTasks();
}

TEST(NearbySessionTest, AnAbortFromThePeerLatchesTheEndAndALaterOneIsIgnored) {
  RawHostRig rig;
  ASSERT_TRUE(rig.open());
  ASSERT_TRUE(rig.joinAndStart());
  ASSERT_TRUE(rig.putAbort(AbortReason::Left));
  ASSERT_TRUE(waitFor([&] { return rig.guest->endLatched(); }));
  ASSERT_TRUE(rig.putAbort(AbortReason::ScriptError));
  ASSERT_TRUE(waitFor([&] { return rig.guest->counters().endIgnored >= 1; }));
  AbortReason reason = AbortReason::Timeout;
  EXPECT_TRUE(rig.guest->takeEnd(reason));
  EXPECT_EQ(reason, AbortReason::Left) << "the first reason wins";
  EXPECT_FALSE(rig.guest->takeEnd(reason));
  EXPECT_EQ(linkLines("abort recv reason=left seat=1"), 1u);
  EXPECT_EQ(linkLines("abort recv reason=script_error seat=1"), 1u);
}

TEST(NearbySessionTest, AnAbortBeforeTheMatchBeganIsReplayedWhenItBegins) {
  RawHostRig rig;
  ASSERT_TRUE(rig.open());
  ASSERT_TRUE(rig.joinAndStart(false));
  ASSERT_TRUE(rig.putAbort(AbortReason::Left));
  ASSERT_TRUE(waitFor([&] { return rig.guest->lobby().guest()->ended().kind == LobbyEnd::Kind::Abort; }));
  EXPECT_FALSE(rig.guest->endLatched());
  ASSERT_TRUE(rig.guest->beginMatch());
  EXPECT_TRUE(rig.guest->endLatched());
  AbortReason reason = AbortReason::Timeout;
  EXPECT_TRUE(rig.guest->takeEnd(reason));
  EXPECT_EQ(reason, AbortReason::Left);
}

// ---------------------------------------------------------------- ship

// Fills the host's ReliableLink queue toward the guest (QUEUE_DEPTH `over` frames, never skipped, none acked because
// the host's frames are lost), so ReliableLink::send answers false to the next STATE: the case ship() retries each
// step. Returns the shipments, which must outlive the call.
std::vector<std::unique_ptr<TestShipment>> fillTheLinkQueue(Duo& d) {
  d.net.blackhole(d.hostEnd.index());
  std::vector<std::unique_ptr<TestShipment>> shipments;
  for (uint16_t ver = 1; ver <= ReliableLink::QUEUE_DEPTH; ++ver) {
    shipments.push_back(std::make_unique<TestShipment>(ver, true));
    EXPECT_FALSE(d.host->ship(shipments.back()->shipment)) << "staging is not shipping";
    EXPECT_TRUE(waitFor([&] { return d.host->ship(shipments.back()->shipment); })) << "ver " << ver;
  }
  EXPECT_EQ(d.host->counters().unacked, ReliableLink::QUEUE_DEPTH);
  return shipments;
}

// Lets `steps` of the task's 2 ms steps pass (fake time).
void letTheTaskStep(const int steps) {
  const uint64_t until = fakertos::S().nowMs.load() + static_cast<uint64_t>(steps) * 2;
  EXPECT_TRUE(waitFor([&] { return fakertos::S().nowMs.load() >= until; }));
}

// Takes every queued STATE of `session` as it arrives until `count` are in `got`.
bool drainInto(NearbySession& session, std::vector<NearbySession::NearbyState>& got, std::vector<uint16_t>& vers,
               const size_t count) {
  return waitFor([&] {
    NearbySession::NearbyState state;
    while (session.takeState(state)) {
      vers.push_back(state.peer.ver);
      got.push_back(state);
    }
    return vers.size() >= count;
  });
}

TEST(NearbySessionTest, ShipIsFalseUntilTheLinkTookItThenTrueOnceAndARefusedSendIsRetried) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  const auto queued = fillTheLinkQueue(d);
  // The link takes no more: ReliableLink::send says false, and the shipment waits in the slot, retried each step.
  TestShipment fifth(5, true);
  EXPECT_FALSE(d.host->ship(fifth.shipment)) << "staged";
  letTheTaskStep(30);
  EXPECT_FALSE(d.host->ship(fifth.shipment)) << "still staged: the link has not taken it";
  EXPECT_EQ(linkLines("state sent ver=5"), 0u);
  EXPECT_EQ(linkLines("state sent ver="), ReliableLink::QUEUE_DEPTH);
  EXPECT_GE(d.host->counters().link.queueFull, 1u) << "the refusals were the link's queue";

  // The path heals, the guest takes the frames as they come, and nothing offers the fifth again: the slot survived.
  d.net.blackhole(d.hostEnd.index(), false);
  std::vector<NearbySession::NearbyState> got;
  std::vector<uint16_t> vers;
  ASSERT_TRUE(drainInto(*d.guest, got, vers, 5));
  EXPECT_EQ(vers, (std::vector<uint16_t>{1, 2, 3, 4, 5}));
  for (NearbySession::NearbyState& state : got) d.guest->releaseState(state);
  ASSERT_TRUE(waitFor([&] { return linkLines("state sent ver=5") == 1u; }));
  EXPECT_TRUE(d.host->ship(fifth.shipment)) << "true once the link took it";
  EXPECT_EQ(linkLines("state sent ver=5"), 1u) << "one line per shipment";
}

TEST(NearbySessionTest, AStagedOverIsNeverReplacedByAnotherVersion) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  const auto queued = fillTheLinkQueue(d);
  TestShipment over(5, true);
  TestShipment later(6, false);
  EXPECT_FALSE(d.host->ship(over.shipment));
  EXPECT_FALSE(d.host->ship(later.shipment)) << "not replaced, not shipped";
  letTheTaskStep(10);
  d.net.blackhole(d.hostEnd.index(), false);
  std::vector<NearbySession::NearbyState> got;
  std::vector<uint16_t> vers;
  ASSERT_TRUE(drainInto(*d.guest, got, vers, 5));
  EXPECT_EQ(vers, (std::vector<uint16_t>{1, 2, 3, 4, 5})) << "the over itself went";
  EXPECT_TRUE(got.back().peer.status.over);
  for (NearbySession::NearbyState& state : got) d.guest->releaseState(state);
  // The later one goes once the match offers it again.
  ASSERT_TRUE(waitFor([&] { return d.host->ship(over.shipment); }));
  ASSERT_TRUE(waitFor([&] { return d.host->ship(later.shipment); }));
  got.clear();
  vers.clear();
  ASSERT_TRUE(drainInto(*d.guest, got, vers, 1));
  EXPECT_EQ(vers, (std::vector<uint16_t>{6}));
  for (NearbySession::NearbyState& state : got) d.guest->releaseState(state);
}

TEST(NearbySessionTest, ANewerStagedStateThatIsNotOverReplacesAStagedOne) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  const auto queued = fillTheLinkQueue(d);
  TestShipment first(5, false);
  TestShipment second(6, false);
  EXPECT_FALSE(d.host->ship(first.shipment));
  EXPECT_FALSE(d.host->ship(second.shipment)) << "latest wins";
  letTheTaskStep(10);
  d.net.blackhole(d.hostEnd.index(), false);
  std::vector<NearbySession::NearbyState> got;
  std::vector<uint16_t> vers;
  ASSERT_TRUE(drainInto(*d.guest, got, vers, 5));
  EXPECT_EQ(vers, (std::vector<uint16_t>{1, 2, 3, 4, 6}));
  for (NearbySession::NearbyState& state : got) d.guest->releaseState(state);
  ASSERT_TRUE(waitFor([&] { return d.host->ship(second.shipment); }));
  EXPECT_EQ(linkLines("state sent ver=5"), 0u);
  EXPECT_EQ(linkLines("state sent ver=6"), 1u);
}

TEST(NearbySessionTest, AnEmptyOrOversizeSnapshotIsRefusedAndLoggedOnce) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  Shipment empty;
  empty.ver = 1;
  empty.status.turn = 1;
  std::vector<uint8_t> big(1401, 7);
  Shipment oversize = empty;
  oversize.snapshot = std::span<const uint8_t>(big.data(), big.size());
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(d.host->ship(empty));
    EXPECT_FALSE(d.host->ship(oversize));
  }
  EXPECT_EQ(linkLines("ship refused"), 1u);
  // 1,400 bytes is the limit and goes.
  big.resize(1400);
  oversize.snapshot = std::span<const uint8_t>(big.data(), big.size());
  oversize.ver = 2;
  EXPECT_FALSE(d.host->ship(oversize));
  EXPECT_TRUE(waitFor([&] { return d.host->ship(oversize); }));
  EXPECT_TRUE(waitFor([&] { return d.guest->counters().queueSize == 1; }));
  NearbySession::NearbyState state;
  ASSERT_TRUE(d.guest->takeState(state));
  EXPECT_EQ(state.peer.snapshot.size(), 1400u);
  d.guest->releaseState(state);
}

// ---------------------------------------------------------------- strangers and the ghost seat

TEST(NearbySessionTest, ThirdDevicesSendingIntoTheMatchAreRefusedForgottenAndNeverFillThePeerTable) {
  Duo d;
  d.openBoth(fastSettings(60000));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  const uint32_t session = d.host->session();
  LinkTest::ManualClock rawClock;
  std::vector<std::unique_ptr<LobbyTest::RawPeer>> strangers;
  for (int i = 0; i < 12; ++i) {  // well over the peer table's size
    strangers.push_back(std::make_unique<LobbyTest::RawPeer>(d.net, rawClock));
    strangers.back()->connect(d.hostEnd);
    EXPECT_TRUE(strangers.back()->put(LobbyTest::stateFrame(LobbyTest::bytesOf("x"), 1), session, 0));
    EXPECT_TRUE(strangers.back()->put(LobbyTest::abortFrame(AbortReason::Left), session));
  }
  // The real peer still works: the guest ships and the host queues it (its frame was sent after the strangers').
  TestShipment ship(1, false);
  ASSERT_TRUE(waitFor([&] { return d.guest->ship(ship.shipment); }));
  ASSERT_TRUE(waitFor([&] { return d.host->counters().queueSize == 1; }));
  EXPECT_EQ(d.host->counters().link.peerTableFull, 0u);
  EXPECT_FALSE(d.host->endLatched()) << "a stranger's ABORT ends nothing";
  NearbySession::NearbyState state;
  ASSERT_TRUE(d.host->takeState(state));
  EXPECT_EQ(state.peer.fromSeat, 2);
  d.host->releaseState(state);
  expectNoRenderLockFromTasks();
}

// The gate keeps which seats sent an ABORT, per seat (R3, AD-11: nothing assumes two seats), for NearbySession::leave,
// which sends its goodbye only to the others. A stranger's ABORT marks no seat.
TEST(NearbySessionGateTest, AnAbortMarksOnlyTheSeatThatSentIt) {
  MatchTest::ArrayStateStore store;
  StateQueue queue(store);
  EndSlot end;
  const GameCore::PeerHandle two = GameCore::PeerHandle::fromValue(0x101);
  const GameCore::PeerHandle three = GameCore::PeerHandle::fromValue(0x102);
  const SeatPeer seatPeers[2] = {SeatPeer{two, 2}, SeatPeer{three, 3}};
  GameLinkSinkGate gate(queue, end, nullptr, 0xABCD0002u, seatPeers);
  GameCore::Protocol::Frame abort;
  abort.header.type = Type::Abort;
  abort.header.session = 0xABCD0002u;
  abort.abortReason = AbortReason::Left;
  gate.onFrame(GameCore::PeerHandle::fromValue(0x1FF), abort);  // a stranger
  EXPECT_FALSE(gate.abortHeardFrom(2));
  EXPECT_FALSE(gate.abortHeardFrom(3));
  gate.onFrame(three, abort);
  EXPECT_FALSE(gate.abortHeardFrom(1));
  EXPECT_FALSE(gate.abortHeardFrom(2)) << "seat 2 sent nothing and still gets the goodbye";
  EXPECT_TRUE(gate.abortHeardFrom(3));
  EXPECT_TRUE(end.latched());
}

TEST(NearbySessionGateTest, AStrangerIsRefusedCountedAndForgottenSoThePeerTableStaysFree) {
  using LobbyTest::RawPeer;
  LinkTest::ManualClock clock;
  FakeLinkNet net(clock);
  FakeLink local(net);
  RawPeer real(net, clock);
  real.connect(local);
  std::vector<std::unique_ptr<RawPeer>> strangers;
  MatchTest::ArrayStateStore store;
  StateQueue queue(store);
  EndSlot end;
  const SeatPeer seatPeers[1] = {SeatPeer{local.handleOf(real.link), 2}};
  GameLinkSinkGate gate(queue, end, nullptr, 0xABCD0001u, seatPeers);
  ReliableLink::Settings settings;
  settings.maxPeers = 2;
  ReliableLink link(local, clock, gate, settings);
  link.setSession(0xABCD0001u);
  gate.bind(link);
  fakelog::clearLines();

  const std::vector<uint8_t> snapshot = LobbyTest::bytesOf("s");
  ASSERT_TRUE(real.put(LobbyTest::stateFrame(snapshot, 1), 0xABCD0001u, 0));
  clock.advance(10);
  link.tick();
  EXPECT_EQ(queue.size(), 1u);
  for (int i = 0; i < 6; ++i) {  // three more than the table holds
    strangers.push_back(std::make_unique<RawPeer>(net, clock));
    strangers.back()->connect(local);
    ASSERT_TRUE(strangers.back()->put(LobbyTest::stateFrame(snapshot, 1), 0xABCD0001u, 0));
    clock.advance(10);
    link.tick();
  }
  EXPECT_EQ(link.stats().peerTableFull, 0u) << "each stranger was forgotten as it came";
  EXPECT_EQ(gate.stats().strangers, 6u);
  EXPECT_EQ(queue.size(), 1u);
  EXPECT_EQ(queue.overflows(), 0u);
  // The real peer still works: its next STATE replaces the queued one (latest wins).
  ASSERT_TRUE(real.put(LobbyTest::stateFrame(snapshot, 2), 0xABCD0001u, 1));
  clock.advance(10);
  link.tick();
  EXPECT_EQ(gate.stats().replaced + gate.stats().queued, 2u);
  QueuedState taken;
  ASSERT_TRUE(queue.take(taken));
  EXPECT_EQ(taken.ver, 2u);
  queue.release(taken);

  // A stranger's silence is forgotten too and ends nothing; the real peer's silence is the end.
  gate.onPeerSilent(local.handleOf(strangers.front()->link));
  EXPECT_FALSE(end.latched());
  gate.onPeerSilent(local.handleOf(real.link));
  AbortReason reason = AbortReason::Left;
  EXPECT_TRUE(end.peek(reason));
  EXPECT_EQ(reason, AbortReason::Timeout);
}

TEST(NearbySessionGateTest, TheGateLogsTheQueueFullAndTheAbortLines) {
  using LobbyTest::RawPeer;
  LinkTest::ManualClock clock;
  FakeLinkNet net(clock);
  FakeLink local(net);
  RawPeer real(net, clock);
  real.connect(local);
  MatchTest::ArrayStateStore store;
  StateQueue queue(store);
  EndSlot end;
  const SeatPeer seatPeers[1] = {SeatPeer{local.handleOf(real.link), 2}};
  GameLinkSinkGate gate(queue, end, nullptr, 0x0000ABCDu, seatPeers);
  ReliableLink link(local, clock, gate);
  link.setSession(0x0000ABCDu);
  gate.bind(link);
  fakelog::clearLines();
  const std::vector<uint8_t> snapshot = LobbyTest::bytesOf("s");
  for (uint16_t ver = 1; ver <= 5; ++ver) {
    Protocol::Frame frame = LobbyTest::stateFrame(snapshot, ver);
    frame.state.status = Status{true, 0, 1};
    ASSERT_TRUE(real.put(frame, 0x0000ABCDu, static_cast<uint16_t>(ver - 1)));
  }
  clock.advance(10);
  link.tick();
  EXPECT_EQ(queue.size(), 4u);
  EXPECT_EQ(link.stats().noRoom, 1u) << "the fifth is left unacked";
  const std::vector<std::string> lines = fakelog::snapshot();
  EXPECT_EQ(std::count(lines.begin(), lines.end(), "INF LINK: session=0000abcd state recv ver=1 seat=2"), 1);
  EXPECT_EQ(std::count(lines.begin(), lines.end(), "INF LINK: session=0000abcd queue full ver=5 overflows=1"), 1);
  ASSERT_TRUE(real.put(LobbyTest::abortFrame(AbortReason::VersionMismatch), 0x0000ABCDu));
  clock.advance(10);
  link.tick();
  EXPECT_EQ(fakelog::countLines("INF LINK: session=0000abcd abort recv reason=version_mismatch seat=2"), 1u);
  AbortReason reason = AbortReason::Left;
  EXPECT_TRUE(end.take(reason));
  EXPECT_EQ(reason, AbortReason::VersionMismatch);
}

TEST(NearbySessionTest, AGuestThatVanishedBeforeStartIsEndedBySilenceWithinTheLimit) {
  Duo d;
  d.openBoth(fastSettings(1500));
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.join());
  d.guest.reset();  // gone: its link ended, nothing more is heard from it
  const uint64_t vanishedAt = fakertos::S().nowMs.load();
  ASSERT_EQ(d.host->startHost(), StartResult::Started) << "the host cannot tell yet";
  ASSERT_TRUE(waitFor([&] { return d.host->endLatched(); }));
  const uint64_t latchedAt = fakertos::S().nowMs.load();
  EXPECT_LT(latchedAt - vanishedAt, 1500u + 500u) << "within the silence limit plus a few steps, never a hang";
  AbortReason reason = AbortReason::Left;
  EXPECT_TRUE(d.host->takeEnd(reason));
  EXPECT_EQ(reason, AbortReason::Timeout);
  expectNoRenderLockFromTasks();
}

// ---------------------------------------------------------------- stats lines

TEST(NearbySessionTest, ALinkRefusalIsCountedAndLoggedOncePerStep) {
  Duo d;
  d.hostProbe.refuseSends = 3;  // the first ADVERTs find the send queue full
  d.openHost();
  ASSERT_NE(d.host, nullptr);
  ASSERT_TRUE(waitFor([&] { return d.host->counters().link.linkRefused >= 1; }));
  ASSERT_TRUE(waitFor([&] { return d.hostProbe.refuseSends.load() == 0; }));
  uint32_t sumN = 0, lastTotal = 0;
  size_t lines = 0;
  ASSERT_TRUE(waitFor([&] {
    sumStatsLines("link refused", sumN, lastTotal, lines);
    return sumN == d.host->counters().link.linkRefused && lines >= 1;
  }));
  EXPECT_EQ(lastTotal, sumN) << "the totals add up: no counter step was logged twice or dropped";
}

// ---------------------------------------------------------------- teardown

TEST(NearbySessionTest, DestroyingASessionWithoutAMatchEndsTheLinkStopsTheTaskAndFreesThePool) {
  Duo d;
  d.openHost();
  ASSERT_NE(d.host, nullptr);
  EXPECT_EQ(fakepsram::liveBlocks, 1u);
  EXPECT_EQ(fakepsram::lastRequestedBytes, 8400u);
  EXPECT_EQ(d.hostProbe.endCalls.load(), 0);
  d.host.reset();
  EXPECT_EQ(d.hostProbe.endCalls.load(), 1) << "the link was ended";
  EXPECT_TRUE(d.hostEnd.ended());
  EXPECT_TRUE(fakertos::waitNoTasks());
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
  EXPECT_EQ(linkLines("task stopped; stack high-water"), 1u);
}

TEST(NearbySessionTest, DestroyingBothSessionsOfAMatchLeavesNoTaskNoPoolAndNoOpenLink) {
  Duo d;
  d.openBoth();
  ASSERT_TRUE(d.host && d.guest);
  ASSERT_TRUE(d.joinAndStart());
  MatchDriver a(*d.host, 1), b(*d.guest, 2);
  ASSERT_TRUE(a.startRound());
  ASSERT_TRUE(waitFor([&] {
    a.step();
    b.step();
    return b.match.hasState();
  }));
  EXPECT_EQ(fakepsram::liveBlocks, 2u);
  d.host.reset();
  d.guest.reset();
  EXPECT_EQ(d.hostProbe.endCalls.load(), 1);
  EXPECT_EQ(d.guestProbe.endCalls.load(), 1);
  EXPECT_TRUE(fakertos::waitNoTasks());
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
  EXPECT_EQ(linkLines("task stopped; stack high-water"), 2u);
}

TEST(NearbySessionTest, ATaskThatWillNotStopLeaksItsStateAndIsLoggedRatherThanFreed) {
  Duo d;
  NearbySession::Options options = fastOptions();
  options.joinMs = 20;
  d.openHost(fastSettings(), options);
  ASSERT_NE(d.host, nullptr);
  ASSERT_TRUE(waitFor([&] { return linkLines("task started") == 1u; }));
  struct Release {
    ~Release() { fakertos::release(); }
  } releaseOnExit;
  fakertos::arm(fakertos::At::Clock);  // the task's next clock read, inside its step, holds it
  ASSERT_TRUE(fakertos::waitParked());
  d.host.reset();
  EXPECT_EQ(linkLines("task did not stop; its state is leaked"), 1u);
  EXPECT_EQ(d.hostProbe.endCalls.load(), 0) << "the link is not ended under a task that may be using it";
  EXPECT_EQ(fakepsram::liveBlocks, 1u) << "the pool is leaked, not freed";
  fakertos::release();
  EXPECT_TRUE(fakertos::waitNoTasks()) << "once released the leaked task still ends itself";
}

TEST(NearbySessionTest, BringUpFailureReturnsNullEndsTheLinkAndStartsNoTask) {
  {
    Duo d;
    fakepsram::failNext = true;
    d.openHost();
    EXPECT_EQ(d.host, nullptr) << "no PSRAM for the pool";
    EXPECT_EQ(d.hostProbe.endCalls.load(), 1);
    EXPECT_EQ(fakertos::S().tasksCreated.load(), 0);
    EXPECT_EQ(fakepsram::liveBlocks, 0u);
    EXPECT_EQ(linkLines("session=00000000 bring-up failed: no PSRAM for the pool"), 1u);
  }
  {
    Duo d;
    fakepsram::failNext = true;
    d.openGuest();
    EXPECT_EQ(d.guest, nullptr);
    EXPECT_EQ(d.guestProbe.endCalls.load(), 1);
    EXPECT_EQ(fakertos::S().tasksCreated.load(), 0);
  }
  {
    Duo d;
    fakertos::S().failNextCreate = true;
    d.openHost();
    EXPECT_EQ(d.host, nullptr) << "the task could not be created";
    EXPECT_EQ(d.hostProbe.endCalls.load(), 1);
    EXPECT_EQ(fakepsram::liveBlocks, 0u) << "the pool was freed";
    EXPECT_EQ(linkLines("bring-up failed: cannot create the task"), 1u);
  }
  {
    Duo d;
    EXPECT_EQ(NearbySession::openHost(nullptr, d.clock, d.random, d.game, LobbyTest::bytesOf("x")), nullptr);
    EXPECT_EQ(NearbySession::openGuest(nullptr, d.clock, d.game), nullptr);
    EXPECT_EQ(fakertos::S().tasksCreated.load(), 0);
  }
}

// ---------------------------------------------------------------- the doubles

TEST(NearbySessionDoublesTest, ObservedLinkAgreesWithTheContractItStandsInFor) {
  // ObservedLink stands in for EspNowLink: a refused send is a false that sends nothing, end() is idempotent and
  // afterwards every call is false (ILink.h), and an ABORT frame handed to it is counted whether or not it was taken.
  LinkTest::ManualClock clock;
  FakeLinkNet net(clock);
  FakeLink a(net), b(net);
  LinkProbe probe;
  ObservedLink link(a, probe);
  const PeerHandle toB = a.handleOf(b);
  Protocol::Frame abort;
  abort.header.type = Type::Abort;
  abort.header.session = 1;
  abort.abortReason = AbortReason::Left;
  uint8_t bytes[Protocol::MAX_FRAME_BYTES];
  const size_t length = Protocol::encode(bytes, abort);
  ASSERT_GT(length, 0u);
  probe.refuseSends = 1;
  EXPECT_FALSE(link.send(toB, std::span<const uint8_t>(bytes, length)));
  EXPECT_EQ(net.carried(), 0u) << "nothing sent";
  EXPECT_TRUE(link.send(toB, std::span<const uint8_t>(bytes, length)));
  EXPECT_EQ(net.carried(), 1u);
  EXPECT_EQ(probe.abortFrames.load(), 2);
  link.end();
  link.end();
  EXPECT_EQ(probe.endCalls.load(), 2) << "every call is recorded; the inner end is idempotent";
  EXPECT_FALSE(link.send(toB, std::span<const uint8_t>(bytes, length)));
  PeerHandle from;
  size_t got = 0;
  uint8_t buffer[Protocol::MAX_FRAME_BYTES];
  EXPECT_FALSE(link.receive(from, buffer, got));
}

}  // namespace
