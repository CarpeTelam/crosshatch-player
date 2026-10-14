#include <gtest/gtest.h>

#include <type_traits>

#include "MatchSupport.h"
#include "Protocol.h"
#include "Session.h"

// Session::adopt and the pulled shipment (plan 7, Design Notes 1 to 3) over CounterRules. SessionTest stays as it is.

using namespace GameCore;
using namespace GameCore::MatchTest;

namespace {

// The shipment is Protocol::State's shape, repeated because Protocol.h includes Session.h.
static_assert(std::is_same_v<decltype(Shipment::ver), decltype(Protocol::State::ver)>);
static_assert(std::is_same_v<decltype(Shipment::status), decltype(Protocol::State::status)>);
static_assert(std::is_same_v<decltype(Shipment::snapshot), decltype(Protocol::State::snapshot)>);
static_assert(std::is_same_v<decltype(PeerState::ver), decltype(Protocol::State::ver)>);
static_assert(std::is_same_v<decltype(PeerState::status), decltype(Protocol::State::status)>);
static_assert(std::is_same_v<decltype(PeerState::snapshot), decltype(Protocol::State::snapshot)>);

Status turnOf(const uint8_t turn) {
  Status s;
  s.turn = turn;
  return s;
}
Status overWith(const uint16_t winners) {
  Status s;
  s.over = true;
  s.winners = winners;
  return s;
}

PeerState peerState(const uint8_t from, const uint16_t ver, const Status& status, const Bytes& snapshot) {
  PeerState p;
  p.fromSeat = from;
  p.ver = ver;
  p.status = status;
  p.snapshot = snapshot;
  return p;
}

// A device of `seats` seats playing `local`.
struct Device {
  explicit Device(const uint8_t seats = 2, const uint8_t local = 1)
      : session(Roster::nearby(seats, local, API_LEVEL), rules) {
    rules.seats = seats;
  }
  Adoption adopt(const PeerState& p, Outcome* outcome = nullptr) {
    Adoption result = Adoption::BadStatus;
    const Outcome o = session.adopt(p, result);
    if (outcome != nullptr) *outcome = o;
    return result;
  }
  // Shipment now pending, as a peer's STATE from `from`.
  PeerState shipped(const uint8_t from) {
    Shipment s;
    EXPECT_TRUE(session.pendingShipment(s));
    held.assign(s.snapshot.begin(), s.snapshot.end());
    return peerState(from, s.ver, s.status, held);
  }
  CounterRules rules;
  Session session;
  Bytes held;
};

// Everything a refusal must leave alone.
struct Snapshot {
  explicit Snapshot(const Session& s)
      : ver(s.ver()),
        settled(s.settledVer()),
        status(s.status()),
        bytes(s.snapshot().begin(), s.snapshot().end()),
        pending(s.shipmentPending()) {}
  bool operator==(const Snapshot& o) const {
    return ver == o.ver && settled == o.settled && status.over == o.status.over && status.turn == o.status.turn &&
           status.winners == o.status.winners && bytes == o.bytes && pending == o.pending;
  }
  uint32_t ver;
  uint32_t settled;
  Status status;
  Bytes bytes;
  bool pending;
};

TEST(SessionAdoptTest, TheRulesDoubleKeepsTheIGameRulesContract) {
  CounterRules rules;
  const Roster roster = Roster::nearby(2, 1, API_LEVEL);
  std::span<const uint8_t> state;
  ASSERT_EQ(rules.setup(GameContext{2, Mode::Nearby, API_LEVEL}, state), Outcome::Ok);
  const Bytes initial(state.begin(), state.end());
  Status status;
  ASSERT_EQ(rules.status(initial, roster, status), Outcome::Ok);
  EXPECT_EQ(status.turn, 1);
  EXPECT_FALSE(status.over);

  char reasonBuffer[16] = {};
  std::span<const uint8_t> next;
  const Bytes move{3};
  ASSERT_EQ(rules.apply(initial, 1, move, next, reasonBuffer), Outcome::Ok);
  ASSERT_FALSE(next.empty());
  EXPECT_EQ(Bytes(next.begin(), next.end()), CounterRules::stateOf(3, 2, 1));

  // A rejection: Ok, `next` empty, the reason NUL-terminated and cut to fit.
  const Bytes bad{CounterRules::REJECTED_MOVE};
  ASSERT_EQ(rules.apply(initial, 1, bad, next, reasonBuffer), Outcome::Ok);
  EXPECT_TRUE(next.empty());
  EXPECT_STREQ(reasonBuffer, "bad move");
  // A seat that does not hold the turn is rejected too, never applied.
  ASSERT_EQ(rules.apply(initial, 2, move, next, reasonBuffer), Outcome::Ok);
  EXPECT_TRUE(next.empty());
  // A turn that is no seat of the roster is a ScriptError (LuaGame's status).
  EXPECT_EQ(rules.status(CounterRules::stateOf(0, 3, 0), roster, status), Outcome::ScriptError);
  // Over: no turn, the last mover the winner.
  ASSERT_EQ(rules.status(CounterRules::stateOf(40, 2, 2), roster, status), Outcome::Ok);
  EXPECT_TRUE(status.over);
  EXPECT_EQ(status.turn, 0);
  EXPECT_EQ(status.winners, 2);
}

TEST(SessionAdoptTest, SeatOneStartsAndShipsTheGuestAdoptsRoundStarted) {
  Device host(2, 1), guest(2, 2);
  EXPECT_FALSE(host.session.shipmentPending());
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  ASSERT_TRUE(host.session.shipmentPending());
  Shipment s;
  ASSERT_TRUE(host.session.pendingShipment(s));
  EXPECT_EQ(s.ver, 1);
  EXPECT_EQ(s.status.turn, 1);
  EXPECT_FALSE(s.status.over);
  EXPECT_EQ(Bytes(s.snapshot.begin(), s.snapshot.end()),
            Bytes(host.session.snapshot().begin(), host.session.snapshot().end()));

  EXPECT_FALSE(guest.session.hasState());
  Outcome o = Outcome::Cancelled;
  EXPECT_EQ(guest.adopt(host.shipped(1), &o), Adoption::RoundStarted);
  EXPECT_EQ(o, Outcome::Ok);
  EXPECT_TRUE(guest.session.hasState());
  EXPECT_EQ(guest.session.ver(), 1u);
  EXPECT_EQ(guest.session.settledVer(), 1u);
  EXPECT_EQ(guest.session.status().turn, 1);
  EXPECT_EQ(guest.rules.setups, 0u) << "a guest never runs setup";
  EXPECT_EQ(guest.rules.statusCalls, 0u) << "the shipped status is trusted (AD-9)";
  // The status gates input: seat 2 is not on turn, so its tap is discarded.
  ASSERT_EQ(guest.session.handle(tapEvent(), 2), Outcome::Ok);
  EXPECT_FALSE(guest.session.pending());
  EXPECT_EQ(guest.session.discardedMoves(), 1u);
}

TEST(SessionAdoptTest, ADeviceWithNoStateCallsNoRulesInHandleOrDraw) {
  Device guest(2, 2);
  EXPECT_FALSE(guest.session.hasState());
  EXPECT_EQ(guest.session.handle(tapEvent(), 2), Outcome::Ok);
  EXPECT_EQ(guest.session.draw(2), Outcome::Ok);
  EXPECT_EQ(guest.rules.inputs, 0u);
  EXPECT_EQ(guest.rules.draws, 0u);
  EXPECT_EQ(guest.rules.statusCalls, 0u);
  EXPECT_FALSE(guest.session.pending());
  EXPECT_EQ(guest.session.discardedMoves(), 0u);
}

TEST(SessionAdoptTest, SoloAndPassCallTheRulesAsBeforeAndShipNothing) {
  CounterRules solo;
  Session soloSession(Roster::solo(), solo);
  solo.seats = 1;
  ASSERT_EQ(soloSession.start(), Outcome::Ok);
  EXPECT_TRUE(soloSession.hasState());
  EXPECT_FALSE(soloSession.shipmentPending());
  solo.script.push_back(2);
  ASSERT_EQ(soloSession.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_TRUE(soloSession.pending());
  ASSERT_EQ(soloSession.applyPending(), Outcome::Ok);
  EXPECT_FALSE(soloSession.shipmentPending());
  Shipment none;
  EXPECT_FALSE(soloSession.pendingShipment(none));
  EXPECT_EQ(soloSession.draw(1), Outcome::Ok);
  EXPECT_EQ(solo.draws, 1u);

  CounterRules pass;
  Session passSession(Roster::pass(2), pass);
  ASSERT_EQ(passSession.start(), Outcome::Ok);
  pass.script.push_back(2);
  ASSERT_EQ(passSession.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_EQ(passSession.applyPending(), Outcome::Ok);
  EXPECT_EQ(passSession.ver(), 2u);
  EXPECT_FALSE(passSession.shipmentPending());
  EXPECT_FALSE(passSession.pendingShipment(none));
}

TEST(SessionAdoptTest, TheShipmentIsSetAfterStartAndAnAcceptedMoveAndStaysUntilShipped) {
  Device host(2, 1);
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  ASSERT_TRUE(host.session.shipmentPending());
  // A snapshot not yet shipped stays pending, whatever the owner does meanwhile.
  Shipment s;
  ASSERT_TRUE(host.session.pendingShipment(s));
  ASSERT_TRUE(host.session.pendingShipment(s));
  EXPECT_EQ(s.ver, 1);
  host.session.shipped();
  EXPECT_FALSE(host.session.shipmentPending());
  EXPECT_FALSE(host.session.pendingShipment(s));

  host.rules.script.push_back(5);
  ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_EQ(host.session.applyPending(), Outcome::Ok);
  ASSERT_TRUE(host.session.pendingShipment(s));
  EXPECT_EQ(s.ver, 2);
  EXPECT_EQ(s.status.turn, 2);
  EXPECT_EQ(Bytes(s.snapshot.begin(), s.snapshot.end()), CounterRules::stateOf(5, 2, 1));

  // Latest wins: a second snapshot before shipped() replaces the first.
  Device keeper(2, 1);
  keeper.rules.script = {CounterRules::KEEP_TURN | 1, CounterRules::KEEP_TURN | 2};
  ASSERT_EQ(keeper.session.start(), Outcome::Ok);
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(keeper.session.handle(tapEvent(), 1), Outcome::Ok);
    ASSERT_EQ(keeper.session.applyPending(), Outcome::Ok);
  }
  ASSERT_TRUE(keeper.session.pendingShipment(s));
  EXPECT_EQ(s.ver, 3);
  EXPECT_EQ(Bytes(s.snapshot.begin(), s.snapshot.end()), CounterRules::stateOf(3, 1, 1));
}

TEST(SessionAdoptTest, ARejectedMoveShipsNothingAndStaysOnTheDevice) {
  Device host(2, 1);
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  host.session.shipped();
  host.rules.script.push_back(CounterRules::REJECTED_MOVE);
  ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_TRUE(host.session.pending());
  ASSERT_EQ(host.session.applyPending(), Outcome::Ok);
  EXPECT_EQ(host.rules.rejectedEvents, 1u);
  EXPECT_EQ(host.rules.lastReason, "bad move");
  EXPECT_FALSE(host.session.shipmentPending());
  EXPECT_EQ(host.session.ver(), 1u);
}

TEST(SessionAdoptTest, AFailedCallShipsNothing) {
  // A status that fails after an accepted move.
  Device host(2, 1);
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  host.session.shipped();
  host.rules.statusOutcome = Outcome::ScriptError;
  host.rules.script.push_back(1);
  ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
  EXPECT_EQ(host.session.applyPending(), Outcome::ScriptError);
  EXPECT_FALSE(host.session.shipmentPending());

  // A start whose status is cancelled.
  Device cancelled(2, 1);
  cancelled.rules.statusOutcome = Outcome::Cancelled;
  EXPECT_EQ(cancelled.session.start(), Outcome::Cancelled);
  EXPECT_FALSE(cancelled.session.shipmentPending());

  // An Over delivery that fails: the round ends on this device's move.
  Device ender(2, 1);
  ender.rules.limit = 2;
  ender.rules.failOverSeat = 1;
  ASSERT_EQ(ender.session.start(), Outcome::Ok);
  ender.session.shipped();
  ender.rules.script.push_back(5);
  ASSERT_EQ(ender.session.handle(tapEvent(), 1), Outcome::Ok);
  EXPECT_EQ(ender.session.applyPending(), Outcome::ScriptError);
  EXPECT_FALSE(ender.session.shipmentPending());
}

TEST(SessionAdoptTest, RestoreThenStartShipsNothingAndAFreshStartShipsAgain) {
  Device host(2, 1);
  ASSERT_TRUE(host.session.restore(CounterRules::stateOf(7, 1, 2), 9));
  EXPECT_TRUE(host.session.hasState());
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  EXPECT_FALSE(host.session.shipmentPending());
  EXPECT_EQ(host.rules.setups, 0u);
  EXPECT_EQ(host.session.ver(), 9u);
  // Play again runs setup and ships.
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  EXPECT_TRUE(host.session.shipmentPending());
  EXPECT_EQ(host.session.ver(), 10u);
}

TEST(SessionAdoptTest, ThePeerMovesAndTheGuestAdoptsItsSnapshotVerAndStatus) {
  Device host(2, 1), guest(2, 2);
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  ASSERT_EQ(guest.adopt(host.shipped(1)), Adoption::RoundStarted);
  host.session.shipped();
  host.rules.script.push_back(4);
  ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_EQ(host.session.applyPending(), Outcome::Ok);
  const uint32_t statusCalls = guest.rules.statusCalls;
  EXPECT_EQ(guest.adopt(host.shipped(1)), Adoption::Adopted);
  EXPECT_EQ(guest.session.ver(), 2u);
  EXPECT_EQ(guest.session.settledVer(), 2u);
  EXPECT_EQ(guest.session.status().turn, 2);
  EXPECT_EQ(Bytes(guest.session.snapshot().begin(), guest.session.snapshot().end()), CounterRules::stateOf(4, 2, 1));
  EXPECT_EQ(guest.rules.statusCalls, statusCalls) << "adopt never calls rules.status (AD-9)";
  EXPECT_EQ(guest.rules.applies, 0u) << "the mover applies; the guest never does";
  // It is now seat 2's turn: a tap is kept.
  guest.rules.script.push_back(1);
  ASSERT_EQ(guest.session.handle(tapEvent(), 2), Outcome::Ok);
  EXPECT_TRUE(guest.session.pending());
}

TEST(SessionAdoptTest, ARunOfKeepTurnStatesIsAdoptedInOrder) {
  Device host(2, 1), guest(2, 2);
  host.rules.script = {CounterRules::KEEP_TURN | 1, CounterRules::KEEP_TURN | 2, CounterRules::KEEP_TURN | 3};
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  ASSERT_EQ(guest.adopt(host.shipped(1)), Adoption::RoundStarted);
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
    ASSERT_EQ(host.session.applyPending(), Outcome::Ok);
    EXPECT_EQ(guest.adopt(host.shipped(1)), Adoption::Adopted) << i;
    EXPECT_EQ(guest.session.status().turn, 1);
  }
  EXPECT_EQ(guest.session.ver(), 4u);
  EXPECT_EQ(Bytes(guest.session.snapshot().begin(), guest.session.snapshot().end()), CounterRules::stateOf(6, 1, 1));
  EXPECT_EQ(guest.rules.applies, 0u);
}

TEST(SessionAdoptTest, ASeatOneStateWhileTheTurnIsOursIsOutOfTurnAndChangesNothing) {
  Device host(2, 1), guest(2, 2);
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  ASSERT_EQ(guest.adopt(host.shipped(1)), Adoption::RoundStarted);
  host.session.shipped();
  host.rules.script.push_back(4);  // passes the turn to seat 2
  ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_EQ(host.session.applyPending(), Outcome::Ok);
  ASSERT_EQ(guest.adopt(host.shipped(1)), Adoption::Adopted);
  ASSERT_EQ(guest.session.status().turn, 2);
  const Snapshot before(guest.session);
  // A newer STATE from seat 1 while seat 2 holds the turn.
  EXPECT_EQ(guest.adopt(peerState(1, 3, turnOf(1), CounterRules::stateOf(9, 1, 1))), Adoption::OutOfTurn);
  EXPECT_EQ(Snapshot(guest.session), before);
}

TEST(SessionAdoptTest, NotPeerIsASeatOutsideTheRosterOrOneThisDevicePlays) {
  Device guest(2, 2);
  const Bytes snap = CounterRules::stateOf(1, 1, 0);
  for (const uint8_t from : {uint8_t{0}, uint8_t{2}, uint8_t{3}, uint8_t{17}, uint8_t{255}}) {
    EXPECT_EQ(guest.adopt(peerState(from, 1, turnOf(1), snap)), Adoption::NotPeer) << int(from);
  }
  EXPECT_FALSE(guest.session.hasState());
  // A device that plays no seat has none local: seat 1 is a peer.
  CounterRules rules;
  Session watcher(Roster::nearby(2, 0, API_LEVEL), rules);
  Adoption result = Adoption::BadStatus;
  EXPECT_EQ(watcher.adopt(peerState(1, 1, turnOf(1), snap), result), Outcome::Ok);
  EXPECT_EQ(result, Adoption::RoundStarted);
}

TEST(SessionAdoptTest, BusyWhileALocalMoveIsPendingAndNothingChanges) {
  Device guest(2, 2);
  ASSERT_EQ(guest.adopt(peerState(1, 1, turnOf(2), CounterRules::stateOf(1, 2, 1))), Adoption::RoundStarted);
  guest.rules.script.push_back(3);
  ASSERT_EQ(guest.session.handle(tapEvent(), 2), Outcome::Ok);
  ASSERT_TRUE(guest.session.pending());
  const Snapshot before(guest.session);
  EXPECT_EQ(guest.adopt(peerState(1, 5, turnOf(2), CounterRules::stateOf(2, 2, 1))), Adoption::Busy);
  EXPECT_EQ(Snapshot(guest.session), before);
  EXPECT_TRUE(guest.session.pending());
  // The move is still applied to the state it was made on.
  ASSERT_EQ(guest.session.applyPending(), Outcome::Ok);
  EXPECT_EQ(guest.rules.applies, 1u);
  EXPECT_EQ(guest.session.ver(), 2u);
}

TEST(SessionAdoptTest, BadSnapshotIsEmptyOrLongerThanTheLimit) {
  Device guest(2, 2);
  const Bytes tooLong(SNAPSHOT_BYTES + 1, 0);
  EXPECT_EQ(guest.adopt(peerState(1, 1, turnOf(1), Bytes{})), Adoption::BadSnapshot);
  EXPECT_EQ(guest.adopt(peerState(1, 1, turnOf(1), tooLong)), Adoption::BadSnapshot);
  EXPECT_FALSE(guest.session.hasState());
  // The limit itself is adopted.
  const Bytes limit(SNAPSHOT_BYTES, 0);
  EXPECT_EQ(guest.adopt(peerState(1, 1, turnOf(1), limit)), Adoption::RoundStarted);
  EXPECT_EQ(guest.session.snapshot().size(), SNAPSHOT_BYTES);
}

TEST(SessionAdoptTest, StaleIsAVerThatIsNotNewer) {
  Device guest(2, 2);
  const Bytes snap = CounterRules::stateOf(1, 1, 0);
  // No state is ver 0: 0 is not newer, 1 to 32,767 are, 32,768 is behind.
  EXPECT_EQ(guest.adopt(peerState(1, 0, turnOf(1), snap)), Adoption::Stale);
  EXPECT_EQ(guest.adopt(peerState(1, 32768, turnOf(1), snap)), Adoption::Stale);
  EXPECT_EQ(guest.adopt(peerState(1, 32767, turnOf(1), snap)), Adoption::RoundStarted);
  EXPECT_EQ(guest.session.ver(), 32767u);
  EXPECT_EQ(guest.adopt(peerState(1, 32767, turnOf(1), snap)), Adoption::Stale) << "equal";
  EXPECT_EQ(guest.adopt(peerState(1, 32766, turnOf(1), snap)), Adoption::Stale) << "older";
  EXPECT_EQ(guest.adopt(peerState(1, 32768, turnOf(1), snap)), Adoption::Adopted) << "one ahead";
}

TEST(SessionAdoptTest, AnOutOfOrderStateIsNeverAdoptedAndTheVerCrossesTheWrapAsNewer) {
  Device guest(2, 2);
  ASSERT_TRUE(guest.session.restore(CounterRules::stateOf(1, 1, 0), 65534));
  ASSERT_EQ(guest.session.start(), Outcome::Ok);
  ASSERT_EQ(guest.session.status().turn, 1);
  const Bytes snap = CounterRules::stateOf(2, 1, 1);
  EXPECT_EQ(guest.adopt(peerState(1, 65535, turnOf(1), snap)), Adoption::Adopted);
  EXPECT_EQ(guest.session.ver(), 65535u);
  // The wire ver wraps to 0; the local counter keeps its upper bits and runs on.
  EXPECT_EQ(guest.adopt(peerState(1, 0, turnOf(1), snap)), Adoption::Adopted);
  EXPECT_EQ(guest.session.ver(), 65536u);
  EXPECT_EQ(guest.adopt(peerState(1, 1, turnOf(1), snap)), Adoption::Adopted);
  EXPECT_EQ(guest.session.ver(), 65537u);
  // Behind the wrap is stale.
  EXPECT_EQ(guest.adopt(peerState(1, 65535, turnOf(1), snap)), Adoption::Stale);
  EXPECT_EQ(guest.adopt(peerState(1, 1, turnOf(1), snap)), Adoption::Stale);
  EXPECT_EQ(guest.session.ver(), 65537u);
}

TEST(SessionAdoptTest, OutOfTurnIsASenderThatNeitherHoldsTheTurnNorStartsARound) {
  // Three seats, this device plays 3: with no state only seat 1 may start a round.
  Device guest(3, 3);
  const Bytes snap = CounterRules::stateOf(1, 2, 1);
  EXPECT_EQ(guest.adopt(peerState(2, 1, turnOf(2), snap)), Adoption::OutOfTurn);
  EXPECT_FALSE(guest.session.hasState());
  ASSERT_EQ(guest.adopt(peerState(1, 1, turnOf(2), snap)), Adoption::RoundStarted);
  // Mid-round seat 1 is a move, accepted only on seat 1's turn: it is seat 2's.
  EXPECT_EQ(guest.adopt(peerState(1, 2, turnOf(2), snap)), Adoption::OutOfTurn);
  EXPECT_EQ(guest.adopt(peerState(2, 2, turnOf(3), snap)), Adoption::Adopted);
  // Seat 3 holds the turn now (this device's own): neither peer may move.
  EXPECT_EQ(guest.adopt(peerState(2, 3, turnOf(3), snap)), Adoption::OutOfTurn);
  EXPECT_EQ(guest.adopt(peerState(1, 3, turnOf(3), snap)), Adoption::OutOfTurn);
}

TEST(SessionAdoptTest, BadStatusIsATurnOrWinnerThatIsNoSeatOfTheRoster) {
  const Bytes snap = CounterRules::stateOf(1, 1, 1);
  {
    Device guest(2, 2);
    EXPECT_EQ(guest.adopt(peerState(1, 1, turnOf(0), snap)), Adoption::BadStatus) << "not over needs a turn";
    EXPECT_EQ(guest.adopt(peerState(1, 1, turnOf(3), snap)), Adoption::BadStatus) << "turn outside the roster";
    Status winners = turnOf(1);
    winners.winners = 1;
    EXPECT_EQ(guest.adopt(peerState(1, 1, winners, snap)), Adoption::BadStatus) << "winners while playing";
    Status overTurn = overWith(1);
    overTurn.turn = 1;
    EXPECT_EQ(guest.adopt(peerState(1, 1, overTurn, snap)), Adoption::BadStatus) << "over has no turn";
    EXPECT_EQ(guest.adopt(peerState(1, 1, overWith(4), snap)), Adoption::BadStatus) << "winner seat 3 of 2";
    EXPECT_FALSE(guest.session.hasState());
  }
  Device guest(2, 2);
  EXPECT_EQ(guest.adopt(peerState(1, 1, overWith(3), snap)), Adoption::RoundStarted) << "both seats may win";
  Device draw(2, 2);
  EXPECT_EQ(draw.adopt(peerState(1, 1, overWith(0), snap)), Adoption::RoundStarted) << "a draw";
}

TEST(SessionAdoptTest, ARefusalIsDecidedInTheDocumentedOrder) {
  // Each case breaks the rule it names and every rule after it; the earlier one wins.
  Device guest(2, 2);
  ASSERT_EQ(guest.adopt(peerState(1, 5, turnOf(2), CounterRules::stateOf(1, 2, 1))), Adoption::RoundStarted);
  guest.rules.script.push_back(1);
  ASSERT_EQ(guest.session.handle(tapEvent(), 2), Outcome::Ok);
  ASSERT_TRUE(guest.session.pending());
  const Bytes tooLong(SNAPSHOT_BYTES + 1, 0);
  const Status badStatus = turnOf(9);
  // NotPeer before Busy.
  EXPECT_EQ(guest.adopt(peerState(2, 1, badStatus, tooLong)), Adoption::NotPeer);
  // Busy before BadSnapshot.
  EXPECT_EQ(guest.adopt(peerState(1, 1, badStatus, tooLong)), Adoption::Busy);
  ASSERT_EQ(guest.session.applyPending(), Outcome::Ok);
  // BadSnapshot before Stale.
  EXPECT_EQ(guest.adopt(peerState(1, 1, badStatus, tooLong)), Adoption::BadSnapshot);
  // Stale before OutOfTurn, and OutOfTurn before BadStatus: three seats, this device plays 3, the turn is seat 2's.
  Device other(3, 3);
  ASSERT_EQ(other.adopt(peerState(1, 1, turnOf(2), CounterRules::stateOf(1, 2, 1))), Adoption::RoundStarted);
  EXPECT_EQ(other.adopt(peerState(1, 1, badStatus, CounterRules::stateOf(1, 1, 1))), Adoption::Stale);
  EXPECT_EQ(other.adopt(peerState(1, 2, badStatus, CounterRules::stateOf(1, 1, 1))), Adoption::OutOfTurn);
  // And BadStatus last: the holder of the turn with a status that is no seat's.
  EXPECT_EQ(other.adopt(peerState(2, 2, badStatus, CounterRules::stateOf(1, 1, 1))), Adoption::BadStatus);
}

TEST(SessionAdoptTest, AdoptingAnOverStatusDeliversOverOnceAndOnlyOnceARound) {
  Device guest(2, 2);
  const Bytes snap = CounterRules::stateOf(40, 2, 1);
  ASSERT_EQ(guest.adopt(peerState(1, 1, turnOf(1), CounterRules::stateOf(0, 1, 0))), Adoption::RoundStarted);
  EXPECT_EQ(guest.rules.overs[2], 0u);
  ASSERT_EQ(guest.adopt(peerState(1, 2, overWith(1), snap)), Adoption::Adopted);
  EXPECT_EQ(guest.rules.overs[2], 1u);
  EXPECT_EQ(guest.rules.overCount(), 1u) << "to the local seat only";
  EXPECT_TRUE(guest.session.status().over);
  // The same state again, and an older one: refused, no second event.
  EXPECT_EQ(guest.adopt(peerState(1, 2, overWith(1), snap)), Adoption::Stale);
  EXPECT_EQ(guest.rules.overs[2], 1u);
  // Play again: seat 1 starts a round, then it ends again: one more event.
  ASSERT_EQ(guest.adopt(peerState(1, 3, turnOf(1), CounterRules::stateOf(0, 1, 0))), Adoption::RoundStarted);
  EXPECT_EQ(guest.rules.overs[2], 1u);
  ASSERT_EQ(guest.adopt(peerState(1, 4, overWith(1), snap)), Adoption::Adopted);
  EXPECT_EQ(guest.rules.overs[2], 2u);
}

TEST(SessionAdoptTest, ARoundThatStartsOverIsRoundStartedWithOneOverEvent) {
  Device guest(2, 2);
  ASSERT_EQ(guest.adopt(peerState(1, 1, overWith(2), CounterRules::stateOf(40, 2, 2))), Adoption::RoundStarted);
  EXPECT_EQ(guest.rules.overs[2], 1u);
}

TEST(SessionAdoptTest, OnlySeatOneMayFollowAnOverStatus) {
  Device guest(3, 3);
  ASSERT_EQ(guest.adopt(peerState(1, 1, overWith(1), CounterRules::stateOf(40, 2, 1))), Adoption::RoundStarted);
  EXPECT_EQ(guest.adopt(peerState(2, 2, turnOf(2), CounterRules::stateOf(0, 2, 0))), Adoption::OutOfTurn);
  EXPECT_EQ(guest.adopt(peerState(1, 2, turnOf(2), CounterRules::stateOf(0, 2, 0))), Adoption::RoundStarted);
}

TEST(SessionAdoptTest, AScriptErrorFromTheOverInputIsReportedAfterTheStateIsAdopted) {
  Device guest(2, 2);
  guest.rules.failOverSeat = 2;
  ASSERT_EQ(guest.adopt(peerState(1, 1, turnOf(1), CounterRules::stateOf(0, 1, 0))), Adoption::RoundStarted);
  Outcome o = Outcome::Ok;
  EXPECT_EQ(guest.adopt(peerState(1, 2, overWith(1), CounterRules::stateOf(40, 2, 1)), &o), Adoption::Adopted);
  EXPECT_EQ(o, Outcome::ScriptError);
  EXPECT_EQ(guest.session.ver(), 2u);
  EXPECT_TRUE(guest.session.status().over);
  EXPECT_EQ(guest.session.settledVer(), 2u);
}

TEST(SessionAdoptTest, EveryRefusalLeavesSnapshotVerStatusAndShipmentUntouched) {
  Device host(2, 1);
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  ASSERT_TRUE(host.session.shipmentPending());
  const Snapshot before(host.session);
  const Bytes snap = CounterRules::stateOf(9, 2, 2);
  const Bytes tooLong(SNAPSHOT_BYTES + 1, 0);
  EXPECT_EQ(host.adopt(peerState(1, 5, turnOf(2), snap)), Adoption::NotPeer);  // seat 1 is local here
  EXPECT_EQ(host.adopt(peerState(2, 5, turnOf(1), Bytes{})), Adoption::BadSnapshot);
  EXPECT_EQ(host.adopt(peerState(2, 5, turnOf(1), tooLong)), Adoption::BadSnapshot);
  EXPECT_EQ(host.adopt(peerState(2, 1, turnOf(1), snap)), Adoption::Stale);
  EXPECT_EQ(host.adopt(peerState(2, 0, turnOf(1), snap)), Adoption::Stale);
  EXPECT_EQ(host.adopt(peerState(2, 5, turnOf(1), snap)), Adoption::OutOfTurn);  // the turn is ours (seat 1)
  EXPECT_EQ(Snapshot(host.session), before);
  EXPECT_EQ(host.rules.statusCalls, 1u);
  Device guest(2, 2);
  EXPECT_EQ(guest.adopt(peerState(1, 5, turnOf(7), snap)), Adoption::BadStatus);
  EXPECT_FALSE(guest.session.hasState());
  EXPECT_EQ(guest.session.ver(), 0u);
}

TEST(SessionAdoptTest, AnAdoptClearsAShipmentThePeerHasAlreadySurpassed) {
  Device host(2, 1);
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  host.session.shipped();
  host.rules.script.push_back(3);  // passes the turn to seat 2
  ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_EQ(host.session.applyPending(), Outcome::Ok);
  ASSERT_TRUE(host.session.shipmentPending());
  // Seat 2 moved on the state it received and its newer STATE arrives before ours was handed to the link.
  EXPECT_EQ(host.adopt(peerState(2, 3, turnOf(1), CounterRules::stateOf(5, 1, 2))), Adoption::Adopted);
  EXPECT_FALSE(host.session.shipmentPending());
  EXPECT_EQ(host.session.ver(), 3u);
  // An adopt ships nothing of its own.
  Shipment s;
  EXPECT_FALSE(host.session.pendingShipment(s));
}

TEST(SessionAdoptTest, ALocalTapWhileThePeerHoldsTheTurnIsDiscardedAndApplyIsNeverCalled) {
  Device guest(2, 2);
  ASSERT_EQ(guest.adopt(peerState(1, 1, turnOf(1), CounterRules::stateOf(0, 1, 0))), Adoption::RoundStarted);
  // The peer's turn-passing STATE is queued (not yet adopted): the status still names the peer.
  guest.rules.script = {3, 2};
  ASSERT_EQ(guest.session.handle(tapEvent(), 2), Outcome::Ok);
  EXPECT_FALSE(guest.session.pending());
  EXPECT_EQ(guest.session.discardedMoves(), 1u);
  EXPECT_EQ(guest.session.applyPending(), Outcome::Ok);
  EXPECT_EQ(guest.rules.applies, 0u);
  // Later the queued STATE is adopted, and the next tap is kept.
  ASSERT_EQ(guest.adopt(peerState(1, 2, turnOf(2), CounterRules::stateOf(4, 2, 1))), Adoption::Adopted);
  ASSERT_EQ(guest.session.handle(tapEvent(), 2), Outcome::Ok);
  EXPECT_TRUE(guest.session.pending());
}

TEST(SessionAdoptTest, ARematchKeepsCountingVerAcrossRounds) {
  Device host(2, 1), guest(2, 2);
  host.rules.limit = 3;
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  ASSERT_EQ(guest.adopt(host.shipped(1)), Adoption::RoundStarted);
  host.session.shipped();
  host.rules.script.push_back(CounterRules::KEEP_TURN | 5);  // seat 1 ends the round at once
  ASSERT_EQ(host.session.handle(tapEvent(), 1), Outcome::Ok);
  ASSERT_EQ(host.session.applyPending(), Outcome::Ok);
  ASSERT_TRUE(host.session.status().over);
  EXPECT_EQ(host.rules.overs[1], 1u);
  ASSERT_EQ(guest.adopt(host.shipped(1)), Adoption::Adopted);
  EXPECT_EQ(guest.rules.overs[2], 1u);
  host.session.shipped();
  const uint32_t overVer = host.session.ver();
  ASSERT_EQ(host.session.start(), Outcome::Ok);
  EXPECT_EQ(host.session.ver(), overVer + 1);
  EXPECT_EQ(guest.adopt(host.shipped(1)), Adoption::RoundStarted);
  EXPECT_EQ(guest.session.ver(), host.session.ver());
  EXPECT_FALSE(guest.session.status().over);
}

}  // namespace
