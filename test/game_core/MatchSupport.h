#pragma once

// The rig the nearby-match suites share (SessionAdoptTest, StateQueueTest, MatchSinkTest, NearbyMatchTest): a counter
// game for the rules, an array standing in for the state pool, and a two-device pair stepped in fake time.
//
// Test doubles (orchestrated-epics.md, Build-agent brief):
// - CounterRules stands in for GameScript::LuaGame, the script adapter behind IGameRules (SessionTest's FakeRules is
//   the single-device sibling). State {sum u16, turn, lastMover}: a move adds `m & 0x7F`, keeps the turn when the
//   high bit is set (KEEP_TURN) and otherwise passes it to the next seat, 0xFF is rejected, and the round is over at
//   `limit` with the last mover the winner. `firstTurn` (1 or 2) is the turn setup() gives. Like LuaGame it refuses
//   a move from a seat that does not hold the turn (a rejection, so an `apply` on the wrong side shows up as one) and
//   its status() is a ScriptError for a turn outside the roster. More permissive than LuaGame: no codec, no byte
//   limits beyond Session's, no instruction budget, and the game knows its seat count from `seats`, set by the test
//   (a device that never ran setup() cannot learn it from the context). SessionAdoptTest pins status, apply, and a
//   rejection against the IGameRules contract.
// - ArrayStateStore stands in for the device's PSRAM pool of snapshot buffers (entry 8): a fixed array, acquire never
//   allocates, a double release or a stray handle is counted (the device adapter would corrupt its free list), and a
//   leak is inUse() above zero. More permissive than the device: no PSRAM latency, and acquire fails only when the
//   pool is empty or `denyAcquire` is set.
// - MatchPeer stands in for the GameLink task plus the VM task of one device, in one thread: a FakeLink endpoint, a
//   ReliableLink, a MatchSink, a StateQueue, an EndSlot, and a Session whose step() is the reference loop entries 8
//   and 9 read: tick the link, read the end slot, take and adopt one queued STATE when no move is pending, tap when
//   the turn is local, and offer the pending shipment to the link, calling shipped() when the link took it. More
//   permissive than the device: no cross-task race, no handoff slot, and `denySends` stands in for a
//   ReliableLink::send that returns false (a full queue, a lost peer) by count.
// - MatchPair is two MatchPeers (seat 1 and seat 2) on one seeded FakeLinkNet, stepped 10 fake ms at a time. Every
//   bound in the suites is in fake milliseconds, never host speed.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "EndSlot.h"
#include "FakeLink.h"
#include "IGameRules.h"
#include "MatchSink.h"
#include "Protocol.h"
#include "ReliableLink.h"
#include "Session.h"
#include "StateQueue.h"

namespace GameCore::MatchTest {

using namespace GameCore;
using namespace GameCore::LinkTest;
using Bytes = std::vector<uint8_t>;

inline constexpr uint32_t MATCH_SESSION = 0x4D41544Bu;
inline constexpr uint32_t MATCH_STEP_MS = 10;

class CounterRules final : public IGameRules {
 public:
  static constexpr uint8_t KEEP_TURN = 0x80;
  static constexpr uint8_t REJECTED_MOVE = 0xFF;

  static Bytes stateOf(const uint16_t sum, const uint8_t turn, const uint8_t lastMover) {
    return Bytes{static_cast<uint8_t>(sum & 0xFF), static_cast<uint8_t>(sum >> 8), turn, lastMover};
  }

  Outcome setup(const GameContext& ctx, std::span<const uint8_t>& state) override {
    ++setups;
    seats = ctx.seats;
    seenCtx = ctx;
    const Bytes initial = stateOf(0, firstTurn, 0);
    std::copy(initial.begin(), initial.end(), stateBuffer);
    state = {stateBuffer, initial.size()};
    return Outcome::Ok;
  }
  Outcome status(std::span<const uint8_t> state, const Roster& roster, Status& out) override {
    ++statusCalls;
    if (statusOutcome != Outcome::Ok) return statusOutcome;
    if (state.size() != 4) return Outcome::ScriptError;
    const uint16_t sum = static_cast<uint16_t>(state[0] | (state[1] << 8));
    Status result;
    if (sum >= limit) {
      result.over = true;
      result.winners = state[3] != 0 ? static_cast<uint16_t>(1u << (state[3] - 1)) : 0;
    } else {
      if (!roster.isSeat(state[2])) return Outcome::ScriptError;
      result.turn = state[2];
    }
    out = result;
    return Outcome::Ok;
  }
  Outcome apply(std::span<const uint8_t> state, uint8_t seat, std::span<const uint8_t> move,
                std::span<const uint8_t>& next, std::span<char> reason) override {
    ++applies;
    appliedSeats.push_back(seat);
    next = {};
    if (state.size() != 4 || move.size() != 1) return Outcome::ScriptError;
    const uint16_t sum = static_cast<uint16_t>(state[0] | (state[1] << 8));
    if (sum >= limit || seat != state[2]) {
      ++rejects;
      std::snprintf(reason.data(), reason.size(), "%s", "not your turn");
      return Outcome::Ok;
    }
    if (move[0] == REJECTED_MOVE) {
      ++rejects;
      std::snprintf(reason.data(), reason.size(), "%s", "bad move");
      return Outcome::Ok;
    }
    const uint16_t total = static_cast<uint16_t>(sum + (move[0] & 0x7F));
    const uint8_t turn = (move[0] & KEEP_TURN) != 0 ? seat : static_cast<uint8_t>(seat % seats + 1);
    const Bytes after = stateOf(total, turn, seat);
    std::copy(after.begin(), after.end(), stateBuffer);
    next = {stateBuffer, after.size()};
    return Outcome::Ok;
  }
  Outcome draw(std::span<const uint8_t>, uint8_t seat) override {
    ++draws;
    drawnSeat = seat;
    return Outcome::Ok;
  }
  Outcome input(std::span<const uint8_t>, uint8_t seat, const GameEvent& event,
                std::span<const uint8_t>& move) override {
    ++inputs;
    move = {};
    switch (event.kind) {
      case EventKind::Over:
        if (seat < overs.size()) ++overs[seat];
        return seat == failOverSeat ? Outcome::ScriptError : Outcome::Ok;
      case EventKind::Rejected:
        ++rejectedEvents;
        lastReason = event.reason != nullptr ? event.reason : "";
        return Outcome::Ok;
      case EventKind::Tap:
        ++taps;
        moveBuffer = nextMove();
        move = {&moveBuffer, 1};
        return Outcome::Ok;
      default:
        return Outcome::Ok;
    }
  }

  size_t overCount() const {
    size_t n = 0;
    for (const uint32_t c : overs) n += c;
    return n;
  }

  // The move the next tap returns: the script first, then a pseudo-random one from `rngState`.
  uint8_t nextMove() {
    if (!script.empty()) {
      const uint8_t m = script.front();
      script.pop_front();
      return m;
    }
    if (rejectPercent > 0 && roll() % 100 < rejectPercent) return REJECTED_MOVE;
    uint8_t m = static_cast<uint8_t>(1 + roll() % maxStep);
    if (keepPercent > 0 && roll() % 100 < keepPercent) m |= KEEP_TURN;
    return m;
  }

  uint16_t limit = 40;
  uint8_t firstTurn = 1;
  uint8_t seats = 2;
  uint8_t maxStep = 4;
  uint32_t keepPercent = 0;
  uint32_t rejectPercent = 0;
  uint32_t rngState = 1;
  uint8_t failOverSeat = 0;  // input() fails (ScriptError) for the Over event to this seat; 0 for none
  Outcome statusOutcome = Outcome::Ok;
  std::deque<uint8_t> script;  // moves for the next taps, ahead of the random ones

  uint32_t setups = 0, statusCalls = 0, applies = 0, rejects = 0, draws = 0, inputs = 0, taps = 0, rejectedEvents = 0;
  std::array<uint32_t, Roster::MAX_SEATS + 1> overs{};  // Over events by seat
  std::vector<uint8_t> appliedSeats;
  std::string lastReason;
  uint8_t drawnSeat = 0;
  GameContext seenCtx;

 private:
  uint32_t roll() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
  }

  uint8_t stateBuffer[8] = {};
  uint8_t moveBuffer = 0;
};

class ArrayStateStore final : public IStateStore {
 public:
  explicit ArrayStateStore(const size_t buffers = StateQueue::DEPTH + 1) : pool(buffers), used(buffers, false) {}

  size_t capacity() const override { return pool.size(); }
  StateHandle acquire() override {
    if (denyAcquire) {
      ++denied;
      return NO_STATE_HANDLE;
    }
    for (size_t i = 0; i < used.size(); ++i) {
      if (used[i]) continue;
      used[i] = true;
      ++acquired;
      return static_cast<StateHandle>(i);
    }
    return NO_STATE_HANDLE;
  }
  void release(const StateHandle h) override {
    if (h == NO_STATE_HANDLE) return;
    if (h >= used.size() || !used[h]) {
      ++badReleases;
      return;
    }
    used[h] = false;
  }
  std::span<uint8_t> bytes(const StateHandle h) override {
    if (h >= used.size() || !used[h]) return {};
    return pool[h];
  }

  size_t inUse() const {
    size_t n = 0;
    for (const bool u : used) n += u ? 1 : 0;
    return n;
  }

  bool denyAcquire = false;
  uint32_t denied = 0, acquired = 0, badReleases = 0;

 private:
  std::vector<std::array<uint8_t, Protocol::MAX_SNAPSHOT_BYTES>> pool;
  std::vector<bool> used;
};

inline Protocol::Frame stateFrameOf(const uint16_t ver, const Status& status, const std::span<const uint8_t> snapshot) {
  Protocol::Frame f;
  f.header.type = Protocol::Type::State;
  f.state.ver = ver;
  f.state.status = status;
  f.state.snapshot = snapshot;
  return f;
}

inline Protocol::Frame stateFrameOf(const Shipment& s) { return stateFrameOf(s.ver, s.status, s.snapshot); }

inline GameEvent tapEvent() { return GameEvent{EventKind::Tap, 10, 20}; }

class MatchPeer {
 public:
  MatchPeer(FakeLink& linkRef, const IClock& clockRef, const PeerHandle peerHandle, const Roster& roster,
            const uint32_t seed = 1, const size_t storeBuffers = StateQueue::DEPTH + 1,
            const ReliableLink::Settings& settings = {})
      : link(linkRef),
        clock(clockRef),
        peer(peerHandle),
        localSeat(roster.firstLocalSeat()),
        rules(),
        session(roster, rules),
        store(storeBuffers),
        queue(store),
        seatPeers{SeatPeer{peerHandle, static_cast<uint8_t>(roster.firstLocalSeat() == 1 ? 2 : 1)}},
        sink(queue, end, seatPeers),
        rl(linkRef, clockRef, sink, settings) {
    rules.rngState = seed * 2654435761u + 1u;
    rl.setSession(MATCH_SESSION);
  }

  // The reference step (see the header comment).
  void step() {
    rl.tick();
    Protocol::AbortReason reason;
    if (end.take(reason)) {
      ended = true;
      endReason = reason;
    }
    if (adopting) adoptOne();
    if (playing && isLocalTurn()) tapOnce();
    shipOnce();
  }

  bool isLocalTurn() const {
    return session.hasState() && !session.status().over && session.status().turn == localSeat && !session.pending();
  }

  // Takes one queued STATE and adopts it, when no move is pending. False: nothing was taken.
  bool adoptOne() {
    if (session.pending()) return false;
    QueuedState q;
    if (!queue.take(q)) return false;
    PeerState p;
    p.fromSeat = q.fromSeat;
    p.ver = q.ver;
    p.status = q.status;
    p.snapshot = queue.bytes(q);
    Adoption result = Adoption::NotPeer;
    adoptOutcome = session.adopt(p, result);
    queue.release(q);
    verdicts.push_back(result);
    return true;
  }

  // One tap through handle() and applyPending(). True when the rules returned a move that Session kept.
  bool tapOnce() {
    if (!session.hasState() || session.pending()) return false;
    const uint32_t before = session.ver();
    session.handle(tapEvent(), localSeat);
    const bool kept = session.pending();
    session.applyPending();
    if (kept) {
      if (session.ver() != before) {
        ++acceptedMoves;
      } else {
        ++rejectedMoves;
      }
    }
    return kept;
  }

  // Offers the pending shipment to the link; shipped() once the link took it.
  bool shipOnce() {
    Shipment s;
    if (!session.pendingShipment(s)) return false;
    if (denySends > 0) {
      --denySends;
      ++deniedSends;
      return false;
    }
    if (!rl.send(peer, stateFrameOf(s))) return false;
    session.shipped();
    ++shipments;
    if (s.status.over) ++overShipments;
    return true;
  }

  // Play again (round one for seat 1): deferred while a shipment is pending (plan C6).
  bool startRound() {
    if (session.shipmentPending()) return false;
    startOutcome = session.start();
    ++roundsStarted;
    return true;
  }

  size_t countVerdict(const Adoption a) const {
    size_t n = 0;
    for (const Adoption v : verdicts) n += v == a ? 1 : 0;
    return n;
  }

  FakeLink& link;
  const IClock& clock;
  PeerHandle peer;
  uint8_t localSeat;
  CounterRules rules;
  Session session;
  ArrayStateStore store;
  StateQueue queue;
  EndSlot end;
  std::array<SeatPeer, 1> seatPeers;
  MatchSink sink;
  ReliableLink rl;

  bool adopting = true;  // step() adopts one queued STATE; a test turns it off to hold the queue
  bool playing = false;  // step() taps when the turn is local
  size_t denySends = 0;  // the next n shipments find the link refusing (ReliableLink::send false)
  uint32_t deniedSends = 0, shipments = 0, overShipments = 0, acceptedMoves = 0, rejectedMoves = 0, roundsStarted = 0;
  std::vector<Adoption> verdicts;
  Outcome adoptOutcome = Outcome::Ok;
  Outcome startOutcome = Outcome::Ok;
  bool ended = false;
  Protocol::AbortReason endReason = Protocol::AbortReason::Left;
};

// Seat 1 (a) and seat 2 (b) over one seeded net.
class MatchPair {
 public:
  explicit MatchPair(const uint32_t seed = 1, const Faults& faults = {},
                     const size_t storeBuffers = StateQueue::DEPTH + 1)
      : net(clock, seed),
        la(net),
        lb(net),
        a(la, clock, la.handleOf(lb), Roster::nearby(2, 1, API_LEVEL), seed * 2, storeBuffers),
        b(lb, clock, lb.handleOf(la), Roster::nearby(2, 2, API_LEVEL), seed * 2 + 1, storeBuffers) {
    net.setFaults(faults);
  }

  void step() {
    clock.advance(MATCH_STEP_MS);
    a.step();
    b.step();
  }
  // Steps until `pred` holds (checked before each step), for at most `maxMs` fake ms. True once it held.
  bool until(const std::function<bool()>& pred, const uint32_t maxMs) {
    for (uint32_t t = 0; t <= maxMs; t += MATCH_STEP_MS) {
      if (pred()) return true;
      step();
    }
    return false;
  }
  void run(const uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += MATCH_STEP_MS) step();
  }

  // Both devices hold the same state, status, and ver.
  bool agree() const {
    return a.session.hasState() && b.session.hasState() && a.session.ver() == b.session.ver() &&
           a.session.status().over == b.session.status().over && a.session.status().turn == b.session.status().turn &&
           a.session.status().winners == b.session.status().winners &&
           std::equal(a.session.snapshot().begin(), a.session.snapshot().end(), b.session.snapshot().begin(),
                      b.session.snapshot().end());
  }
  bool quiet() const {
    return a.rl.unacked() == 0 && b.rl.unacked() == 0 && a.queue.size() == 0 && b.queue.size() == 0;
  }

  ManualClock clock;
  FakeLinkNet net;
  FakeLink la;
  FakeLink lb;
  MatchPeer a;
  MatchPeer b;
};

}  // namespace GameCore::MatchTest
