#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>

#include "LobbySupport.h"
#include "MatchSupport.h"

// Two devices, one seat each, play whole matches over the seeded hostile FakeLink with CounterRules (plan 7, Design
// Note 6): the mover applies, the peer adopts the shipped STATE, and both end on the same snapshot, ver, and status.
// The rig is MatchSupport.h's MatchPair; every bound is in fake milliseconds. The soak's frame bound is an estimate
// from the per-STATE worst case (resends every 400 ms and an ack each way), confirmed here by the measured maximum.

using namespace GameCore;
using namespace GameCore::MatchTest;

namespace {

using Protocol::AbortReason;

constexpr uint32_t MAX_MATCH_MS = 600000;

Faults hostileFaults() { return LobbyTest::hostile(); }

struct Played {
  bool finished = false;
  uint32_t elapsedMs = 0;
};

bool finishedRounds(MatchPair& p, const uint32_t rounds) {
  return p.a.roundsStarted == rounds && p.a.session.status().over && p.b.session.hasState() &&
         p.b.session.status().over && p.agree() && p.quiet() && p.a.rules.overs[1] == rounds &&
         p.b.rules.overs[2] == rounds;
}

// Seat 1 starts `rounds` rounds one after another; both devices tap whenever the turn is theirs.
Played playRounds(MatchPair& p, const uint32_t rounds, const uint32_t maxMs = MAX_MATCH_MS) {
  p.a.playing = true;
  p.b.playing = true;
  const uint8_t parity = p.a.rules.firstTurn;
  auto firstTurn = [&] { return static_cast<uint8_t>(1 + (parity - 1 + p.a.roundsStarted) % 2); };
  p.a.rules.firstTurn = firstTurn();
  Played result;
  if (!p.a.startRound()) return result;
  for (uint32_t t = 0; t < maxMs; t += MATCH_STEP_MS) {
    p.step();
    if (p.a.session.hasState() && p.a.session.status().over && p.a.roundsStarted < rounds) {
      p.a.rules.firstTurn = firstTurn();
      p.a.startRound();
    }
    if (finishedRounds(p, rounds)) {
      result.finished = true;
      result.elapsedMs = t + MATCH_STEP_MS;
      return result;
    }
  }
  result.elapsedMs = maxMs;
  return result;
}

uint32_t movesOf(const MatchPair& p) { return p.a.acceptedMoves + p.b.acceptedMoves; }

// Everything a finished match must hold, whatever the link did.
void expectSound(MatchPair& p, const uint32_t rounds) {
  EXPECT_TRUE(p.agree());
  // Each move was applied on its mover's side only.
  for (const uint8_t seat : p.a.rules.appliedSeats) ASSERT_EQ(seat, 1);
  for (const uint8_t seat : p.b.rules.appliedSeats) ASSERT_EQ(seat, 2);
  EXPECT_EQ(p.a.rules.applies, p.a.acceptedMoves + p.a.rejectedMoves);
  EXPECT_EQ(p.b.rules.applies, p.b.acceptedMoves + p.b.rejectedMoves);
  EXPECT_EQ(p.a.session.ver(), rounds + movesOf(p)) << "one ver per round start and per accepted move";
  // The guest never ran setup, and a device computes a status only for a snapshot it made itself (AD-9): never one
  // it adopted.
  EXPECT_EQ(p.b.rules.setups, 0u);
  EXPECT_EQ(p.b.rules.statusCalls, p.b.acceptedMoves);
  EXPECT_EQ(p.a.rules.setups, rounds);
  EXPECT_EQ(p.a.rules.statusCalls, rounds + p.a.acceptedMoves);
  // Over once a round on each device, to its own seat.
  EXPECT_EQ(p.a.rules.overs[1], rounds);
  EXPECT_EQ(p.b.rules.overs[2], rounds);
  EXPECT_EQ(p.a.rules.overCount(), rounds);
  EXPECT_EQ(p.b.rules.overCount(), rounds);
  // Nothing the link delivered was refused: the link keeps order and the rules gate the turn.
  for (const Adoption v : p.a.verdicts) EXPECT_TRUE(v == Adoption::Adopted || v == Adoption::RoundStarted) << int(v);
  for (const Adoption v : p.b.verdicts) EXPECT_TRUE(v == Adoption::Adopted || v == Adoption::RoundStarted) << int(v);
  EXPECT_EQ(p.b.countVerdict(Adoption::RoundStarted), rounds);
  EXPECT_EQ(p.a.countVerdict(Adoption::RoundStarted), 0u);
  EXPECT_EQ(p.a.store.inUse(), 0u);
  EXPECT_EQ(p.b.store.inUse(), 0u);
  EXPECT_EQ(p.a.store.badReleases + p.b.store.badReleases, 0u);
  EXPECT_FALSE(p.a.ended);
  EXPECT_FALSE(p.b.ended);
  EXPECT_EQ(p.a.sink.stats().strangers + p.b.sink.stats().strangers, 0u);
}

void configure(MatchPair& p, const uint32_t seed) {
  p.a.rules.limit = p.b.rules.limit = 40;
  p.a.rules.keepPercent = p.b.rules.keepPercent = (seed % 3) * 20;
  p.a.rules.firstTurn = static_cast<uint8_t>(1 + seed % 2);
}

TEST(NearbyMatchTest, TwoDevicesPlayTwoRoundsToOverOnAHostileLinkForManySeeds) {
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    SCOPED_TRACE("seed " + std::to_string(seed));
    MatchPair p(seed, hostileFaults());
    configure(p, seed);
    const Played played = playRounds(p, 2);
    ASSERT_TRUE(played.finished);
    expectSound(p, 2);
    EXPECT_GE(movesOf(p), 20u);
    EXPECT_GT(p.a.acceptedMoves, 0u);
    EXPECT_GT(p.b.acceptedMoves, 0u);
  }
}

TEST(NearbyMatchTest, ARoundWhoseFirstTurnIsSeatTwoIsMovedByTheGuestFirst) {
  MatchPair p(3);
  p.a.rules.firstTurn = 2;
  p.a.playing = p.b.playing = true;
  ASSERT_TRUE(p.a.startRound());
  ASSERT_TRUE(p.until([&] { return p.b.rules.applies > 0; }, 5000));
  EXPECT_EQ(p.a.rules.applies, 0u) << "seat 2's device applied before seat 1's ever did";
  EXPECT_EQ(p.b.rules.appliedSeats.front(), 2);
  EXPECT_EQ(p.b.countVerdict(Adoption::RoundStarted), 1u);
  EXPECT_EQ(p.b.rules.setups, 0u);
}

TEST(NearbyMatchTest, ARunOfKeepTurnMovesIsFollowedByThePeerWithoutApplying) {
  MatchPair p(5);
  p.a.rules.script = {CounterRules::KEEP_TURN | 1, CounterRules::KEEP_TURN | 2, CounterRules::KEEP_TURN | 3, 4};
  ASSERT_TRUE(p.a.startRound());
  p.run(100);  // the guest has the first STATE
  ASSERT_TRUE(p.a.tapOnce());
  p.run(200);
  // Two keep-turn moves made before the link is offered either: the later snapshot replaces the earlier shipment.
  ASSERT_TRUE(p.a.tapOnce());
  ASSERT_TRUE(p.a.tapOnce());
  p.run(200);
  ASSERT_TRUE(p.a.tapOnce());  // passes the turn
  p.run(200);
  EXPECT_EQ(p.a.acceptedMoves, 4u);
  EXPECT_EQ(p.b.rules.applies, 0u);
  EXPECT_TRUE(p.agree());
  EXPECT_EQ(p.b.session.status().turn, 2);
  EXPECT_EQ(p.b.session.ver(), 5u);
  ASSERT_EQ(p.b.verdicts.size(), 4u);
  EXPECT_EQ(p.b.verdicts[0], Adoption::RoundStarted);
  for (size_t i = 1; i < 4; ++i) EXPECT_EQ(p.b.verdicts[i], Adoption::Adopted) << i;
}

TEST(NearbyMatchTest, ARematchKeepsVerRisingAcrossThreeRounds) {
  MatchPair p(11, hostileFaults());
  configure(p, 11);
  p.a.playing = p.b.playing = true;
  uint32_t previous = 0;
  for (uint32_t round = 1; round <= 3; ++round) {
    ASSERT_TRUE(p.a.startRound());
    EXPECT_GT(p.a.session.ver(), previous) << round;
    previous = p.a.session.ver();
    ASSERT_TRUE(
        p.until([&] { return p.a.session.status().over && p.b.session.status().over && p.agree(); }, MAX_MATCH_MS));
    EXPECT_GT(p.a.session.ver(), previous);
    previous = p.a.session.ver();
    ASSERT_TRUE(p.until([&] { return p.quiet(); }, 60000));
  }
  EXPECT_EQ(p.a.session.ver(), 3 + movesOf(p));
  EXPECT_EQ(p.b.session.ver(), p.a.session.ver());
  EXPECT_EQ(p.b.countVerdict(Adoption::RoundStarted), 3u);
}

TEST(NearbyMatchTest, AnOverStateAndTheNextRoundArriveTogetherAndAreAdoptedInOrder) {
  MatchPair p(2);
  p.a.rules.limit = p.b.rules.limit = 10;
  ASSERT_TRUE(p.a.startRound());
  p.run(100);
  ASSERT_TRUE(p.b.session.hasState());
  p.b.adopting = false;  // the guest's queue holds what arrives
  p.a.rules.script = {CounterRules::KEEP_TURN | 50};
  ASSERT_TRUE(p.a.tapOnce());  // the round ends on seat 1's move
  p.run(50);                   // shipped
  ASSERT_FALSE(p.a.session.shipmentPending());
  ASSERT_TRUE(p.a.startRound());  // Play again at once
  p.run(100);
  ASSERT_EQ(p.b.queue.size(), 2u) << "the over STATE is kept and the new round queued behind it";
  EXPECT_EQ(p.b.rules.overs[2], 0u);
  p.b.adopting = true;
  p.step();  // one entry a step
  ASSERT_EQ(p.b.verdicts.size(), 2u);
  EXPECT_EQ(p.b.verdicts.back(), Adoption::Adopted);
  EXPECT_TRUE(p.b.session.status().over);
  EXPECT_EQ(p.b.rules.overs[2], 1u) << "Over is delivered before the new round starts";
  p.step();
  ASSERT_EQ(p.b.verdicts.size(), 3u);
  EXPECT_EQ(p.b.verdicts.back(), Adoption::RoundStarted);
  EXPECT_FALSE(p.b.session.status().over);
  EXPECT_EQ(p.b.rules.overs[2], 1u);
  EXPECT_EQ(p.b.session.ver(), p.a.session.ver());
}

TEST(NearbyMatchTest, ARejectedMoveStaysOnItsDeviceAndPutsNothingOnTheLink) {
  MatchPair p(4);
  ASSERT_TRUE(p.a.startRound());
  p.run(200);
  ASSERT_TRUE(p.quiet());
  ASSERT_FALSE(p.a.session.shipmentPending());
  const uint64_t carried = p.net.carried();
  const uint32_t sent = p.a.rl.stats().framesSent;
  const uint32_t ver = p.a.session.ver();
  p.a.rules.script = {CounterRules::REJECTED_MOVE};
  ASSERT_TRUE(p.a.tapOnce());
  EXPECT_EQ(p.a.rejectedMoves, 1u);
  EXPECT_EQ(p.a.rules.rejectedEvents, 1u);
  EXPECT_EQ(p.a.rules.lastReason, "bad move");
  EXPECT_EQ(p.a.session.ver(), ver);
  EXPECT_FALSE(p.a.session.shipmentPending());
  EXPECT_EQ(p.net.carried(), carried);
  p.run(500);
  EXPECT_EQ(p.a.rl.stats().framesSent, sent) << "no STATE, and no REJECT, is sent";
  EXPECT_EQ(p.b.verdicts.size(), 1u);
  EXPECT_EQ(p.b.rules.rejectedEvents, 0u);
  EXPECT_EQ(p.b.rules.applies, 0u);
  // The turn is still seat 1's: the next move is accepted.
  p.a.rules.script = {2};
  ASSERT_TRUE(p.a.tapOnce());
  EXPECT_EQ(p.a.session.ver(), ver + 1);
}

TEST(NearbyMatchTest, AQueuedTurnPassingStateDiscardsALocalTapAndApplyIsNeverCalled) {
  MatchPair p(6);
  ASSERT_TRUE(p.a.startRound());
  p.run(100);
  ASSERT_TRUE(p.b.session.hasState());
  p.b.adopting = false;
  p.a.rules.script = {3};  // passes the turn to seat 2
  ASSERT_TRUE(p.a.tapOnce());
  p.run(100);
  ASSERT_EQ(p.b.queue.size(), 1u);
  ASSERT_EQ(p.b.session.status().turn, 1) << "the guest has not adopted it yet";
  p.b.rules.script = {5, 6};
  EXPECT_FALSE(p.b.tapOnce());
  EXPECT_EQ(p.b.session.discardedMoves(), 1u);
  EXPECT_EQ(p.b.rules.applies, 0u);
  // Later the queued STATE is adopted, and the tap that follows is kept and applied by seat 2's device.
  ASSERT_TRUE(p.b.adoptOne());
  EXPECT_EQ(p.b.verdicts.back(), Adoption::Adopted);
  EXPECT_TRUE(p.b.tapOnce());
  EXPECT_EQ(p.b.rules.applies, 1u);
  EXPECT_EQ(p.b.rules.appliedSeats.back(), 2);
  p.b.adopting = true;
  p.run(300);
  EXPECT_TRUE(p.agree());
}

TEST(NearbyMatchTest, AStrangerIsRefusedAtTheSinkAndAStaleReplayIsNeverAdopted) {
  MatchPair p(8);
  p.a.playing = p.b.playing = true;
  ASSERT_TRUE(p.a.startRound());
  ASSERT_TRUE(
      p.until([&] { return p.a.session.status().over && p.b.session.status().over && p.quiet(); }, MAX_MATCH_MS));
  p.a.playing = p.b.playing = false;
  // A third device in range puts a STATE to the guest that carries a newer ver than anything yet.
  FakeLink stranger(p.net);
  const PeerHandle toGuest = stranger.handleOf(p.lb);
  Status status;
  status.turn = 2;
  const Bytes forgedSnapshot = CounterRules::stateOf(1, 2, 1);
  Protocol::Frame forged = stateFrameOf(static_cast<uint16_t>(p.b.session.ver() + 1), status, forgedSnapshot);
  forged.header.session = MATCH_SESSION;
  forged.header.seq = 0;
  forged.header.ack = 0xFFFF;
  Bytes wire(Protocol::MAX_FRAME_BYTES);
  const size_t n = Protocol::encode(wire, forged);
  ASSERT_GT(n, 0u);
  const size_t verdictsBefore = p.b.verdicts.size();
  const uint32_t verBefore = p.b.session.ver();
  ASSERT_TRUE(stranger.send(toGuest, std::span<const uint8_t>(wire.data(), n)));
  p.run(200);
  EXPECT_EQ(p.b.sink.stats().strangers, 1u);
  EXPECT_EQ(p.b.queue.size(), 0u);
  EXPECT_EQ(p.b.verdicts.size(), verdictsBefore) << "never offered to adopt";
  EXPECT_EQ(p.b.session.ver(), verBefore);

  // A replay of an old STATE from the real peer is acked and queued, and adoption refuses it as stale.
  const Bytes before(p.b.session.snapshot().begin(), p.b.session.snapshot().end());
  Status old;
  old.turn = 2;
  EXPECT_EQ(p.b.sink.onFrame(p.b.peer, stateFrameOf(1, old, CounterRules::stateOf(0, 2, 1))),
            ReliableLink::Verdict::Taken);
  ASSERT_TRUE(p.b.adoptOne());
  EXPECT_EQ(p.b.verdicts.back(), Adoption::Stale);
  EXPECT_EQ(p.b.session.ver(), verBefore);
  EXPECT_EQ(Bytes(p.b.session.snapshot().begin(), p.b.session.snapshot().end()), before);
}

TEST(NearbyMatchTest, TheVerCrossesTheU16WrapWithoutAResetWhenBothRestoreAt65533) {
  MatchPair p(9, hostileFaults());
  p.a.rules.limit = p.b.rules.limit = 40;
  const Bytes snapshot = CounterRules::stateOf(0, 1, 0);
  for (MatchPeer* peer : {&p.a, &p.b}) {
    ASSERT_TRUE(peer->session.restore(snapshot, 65533));
    ASSERT_EQ(peer->session.start(), Outcome::Ok);
    EXPECT_FALSE(peer->session.shipmentPending()) << "the restore path ships nothing";
    peer->playing = true;
  }
  ASSERT_TRUE(p.until([&] { return p.a.session.status().over && p.b.session.status().over && p.agree() && p.quiet(); },
                      MAX_MATCH_MS));
  EXPECT_GT(p.a.session.ver(), 65536u) << "the local counter runs on through the wrap";
  EXPECT_EQ(p.a.session.ver(), 65533u + movesOf(p));
  EXPECT_EQ(p.b.session.ver(), p.a.session.ver());
  for (const Adoption v : p.a.verdicts) EXPECT_EQ(v, Adoption::Adopted);
  for (const Adoption v : p.b.verdicts) EXPECT_EQ(v, Adoption::Adopted);
  EXPECT_EQ(p.a.rules.setups + p.b.rules.setups, 0u);
}

TEST(NearbyMatchTest, ARefusedLinkSendIsRetriedUntilDeliveredAndTheOverShipmentIsKept) {
  MatchPair p(12);
  p.a.rules.limit = p.b.rules.limit = 10;
  ASSERT_TRUE(p.a.startRound());
  p.run(100);
  ASSERT_TRUE(p.b.session.hasState());
  p.a.denySends = 3;  // the link refuses the next three offers
  p.a.rules.script = {CounterRules::KEEP_TURN | 50};
  ASSERT_TRUE(p.a.tapOnce());
  ASSERT_TRUE(p.a.session.status().over);
  for (int i = 0; i < 3; ++i) {
    p.step();
    EXPECT_TRUE(p.a.session.shipmentPending()) << "a refused send keeps the shipment, over included";
    EXPECT_FALSE(p.a.startRound()) << "Play again waits for the over shipment (plan C6)";
  }
  EXPECT_EQ(p.a.deniedSends, 3u);
  EXPECT_EQ(p.a.overShipments, 0u);
  p.step();
  EXPECT_FALSE(p.a.session.shipmentPending());
  EXPECT_EQ(p.a.overShipments, 1u);
  ASSERT_TRUE(p.a.startRound());
  ASSERT_TRUE(p.until([&] { return p.quiet() && p.agree(); }, 5000));
  ASSERT_GE(p.b.verdicts.size(), 3u);
  EXPECT_EQ(p.b.verdicts[1], Adoption::Adopted);
  EXPECT_EQ(p.b.verdicts[2], Adoption::RoundStarted);
  EXPECT_EQ(p.b.rules.overs[2], 1u);
}

TEST(NearbyMatchTest, AnAbortOrSilenceEndsTheMatchThroughTheEndSlot) {
  MatchPair p(13);
  ASSERT_TRUE(p.a.startRound());
  p.run(100);
  Protocol::Frame abort;
  abort.header.type = Protocol::Type::Abort;
  abort.abortReason = AbortReason::Left;
  ASSERT_TRUE(p.a.rl.send(p.a.peer, abort));
  p.run(100);
  EXPECT_TRUE(p.b.ended);
  EXPECT_EQ(p.b.endReason, AbortReason::Left);
  EXPECT_FALSE(p.a.ended);

  // Silence: the peer's frames stop; the guest latches Timeout after silenceMs and the first reason stays.
  MatchPair q(14);
  ASSERT_TRUE(q.a.startRound());
  q.run(100);
  q.net.blackhole(q.la.index());
  ASSERT_TRUE(q.until([&] { return q.b.ended; }, ReliableLink::DEFAULT_SILENCE_MS + 2000));
  EXPECT_EQ(q.b.endReason, AbortReason::Timeout);
  EXPECT_GE(q.b.rl.stats().silentReports, 1u);
}

// The soak. Loss to 35 %, duplicates to 30 %, jitter to 60 ms; two rounds; at least 20 moves; agreement; and the
// frames carried (every copy the net queued, duplicates included) bounded by 2 + (T / keepalive + 1) + 6 * S for T fake
// ms and S STATEs shipped. The plan's estimate was 4 + 2 * (T / keepalive + 1) + 12 * S; this run's measured maximum
// was 0.26 of it, so the bound is the estimate halved (1.9 times the measured maximum, within the plan's twice).
TEST(NearbyMatchTest, TwoHundredRandomlyHostileMatchesAgreeAndStayUnderTheFrameBound) {
  uint64_t worstCarried = 0;
  double worstRatio = 0;
  uint32_t fewestMoves = 1000000;
  uint32_t slowest = 0;
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    SCOPED_TRACE("seed " + std::to_string(seed));
    uint32_t r = seed * 2654435761u + 12345u;
    auto roll = [&r](const uint32_t mod) {
      r ^= r << 13;
      r ^= r >> 17;
      r ^= r << 5;
      return r % mod;
    };
    Faults f;
    f.lossPercent = static_cast<int>(roll(36));
    f.duplicatePercent = static_cast<int>(roll(31));
    f.reorderPercent = static_cast<int>(roll(31));
    f.delayMs = roll(41);
    f.jitterMs = roll(61);
    f.reorderExtraMs = 100;
    MatchPair p(seed * 7919u, f);
    configure(p, seed);
    p.a.rules.rejectPercent = p.b.rules.rejectPercent = roll(2) * 10;
    const Played played = playRounds(p, 2);
    ASSERT_TRUE(played.finished) << "loss " << f.lossPercent << " dup " << f.duplicatePercent;
    // A rejected move is not a snapshot: sound counts only accepted moves, so run the same checks.
    expectSound(p, 2);
    ASSERT_GE(movesOf(p), 20u);
    const uint64_t shipped = p.a.shipments + p.b.shipments;
    const double estimate = 4.0 + 2.0 * (played.elapsedMs / ReliableLink::DEFAULT_KEEPALIVE_MS + 1) + 12.0 * shipped;
    const double bound = estimate / 2;
    const uint64_t carried = p.net.carried();
    worstCarried = std::max(worstCarried, carried);
    worstRatio = std::max(worstRatio, carried / estimate);
    fewestMoves = std::min(fewestMoves, movesOf(p));
    slowest = std::max(slowest, played.elapsedMs);
    EXPECT_LE(carried, bound) << "frames carried " << carried << " vs bound " << bound;
  }
  std::printf("[ soak ] worst carried %llu, worst carried/estimate %.3f, fewest moves %u, slowest %u ms\n",
              static_cast<unsigned long long>(worstCarried), worstRatio, fewestMoves, slowest);
}

}  // namespace
