#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "LobbySupport.h"

// HostLobby and GuestLobby together over the seeded hostile FakeLink: the handshake to a started roster, and the
// regressions crossplay's discovery found (host-tests/link/test_link.cpp, in this protocol's terms) and the rejoin and
// restart cases ReliableLink's follow-up review routed here. Every time bound is in fake milliseconds from
// LobbySettings and the link's settings, never host speed.

using namespace GameCore;
using namespace GameCore::LobbyTest;

namespace {

// A host and one guest on one net. The guest's game may differ in level from the host's.
struct Duo {
  Duo(const uint32_t seed, const Faults& faults, const int32_t maxSeats = 2, const int32_t hostApi = 1,
      const int32_t guestApi = 1)
      : rig(seed),
        hostGame(testGame(maxSeats, hostApi)),
        guestGame(testGame(maxSeats, guestApi)),
        hl(rig.net),
        gl(rig.net),
        random({0xAB000000u + seed}),
        host(hl, rig.clock, random, hostGame, bytesOf("duo")),
        guest(std::make_unique<GuestLobby>(gl, rig.clock, guestGame)),
        observer(rig.net, rig.clock) {
    rig.net.setFaults(faults);
    rig.track(host);
    rig.track(*guest);
    rig.onStep = [this] {
      const uint32_t received = host.link().stats().framesReceived;
      if (received != hostReceived) {
        hostReceived = received;
        hostHeardAt = rig.now();
      }
    };
  }

  // One step of what the screens do: the guest joins the first listed host, the host starts once it has a guest.
  void drive() {
    GuestLobby& g = *guest;
    if (g.state() == GuestState::Browsing && g.hostCount() > 0 && g.host(0).verdict == HostVerdict::Joinable) {
      g.join(g.host(0).handle, g.host(0).session);
    }
    if (host.state() == HostState::Waiting && host.canStart() &&
        (!startWhenJoined || g.state() == GuestState::Joined)) {
      host.start();
      startedAt = rig.now();
    }
  }
  bool bothStarted() const { return host.state() == HostState::Started && guest->state() == GuestState::Started; }
  bool run(const uint32_t maxMs) {
    return rig.until(
        [this] {
          observer.drain();
          drive();
          return bothStarted();
        },
        maxMs);
  }
  // A fresh guest on `link`, the old one gone (a cancel, or a device that came up again).
  GuestLobby& replaceGuest(FakeLink& link) {
    rig.untrack(guest.get());
    guest = std::make_unique<GuestLobby>(link, rig.clock, guestGame);
    rig.track(*guest);
    return *guest;
  }
  std::vector<HostEvent> events() {
    std::vector<HostEvent> out;
    HostEvent e;
    while (host.poll(e)) out.push_back(e);
    return out;
  }

  Rig rig;
  LobbyGame hostGame;
  LobbyGame guestGame;
  FakeLink hl;
  FakeLink gl;
  FixedRandom random;
  HostLobby host;
  std::unique_ptr<GuestLobby> guest;
  RawPeer observer;
  uint64_t startedAt = 0;
  // A rejoin test starts the host only once the new guest has joined: a seat held for a guest that is gone is not a
  // guest.
  bool startWhenJoined = false;
  // The last step in which the host's link took a frame (any peer's): read it before a second sender exists.
  uint32_t hostReceived = 0;
  uint64_t hostHeardAt = 0;
};

TEST(LobbyHandshakeTest, AHostAndAGuestReachAStartedRosterForManySeeds) {
  for (uint32_t seed = 1; seed <= 200; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, hostile());
    ASSERT_TRUE(d.host.ok());
    ASSERT_TRUE(d.run(5000)) << "host " << int{static_cast<uint8_t>(d.host.state())} << " guest "
                             << int{static_cast<uint8_t>(d.guest->state())} << " at " << d.rig.now();
    EXPECT_LE(d.rig.now(), 5000u);
    EXPECT_EQ(d.host.roster().mode, Mode::Nearby);
    EXPECT_EQ(d.host.roster().seats, 2);
    EXPECT_EQ(d.host.roster().localSeats, 0b01);
    EXPECT_EQ(d.host.roster().api, 1);
    EXPECT_EQ(d.guest->roster().mode, Mode::Nearby);
    EXPECT_EQ(d.guest->roster().seats, 2);
    EXPECT_EQ(d.guest->roster().localSeats, 0b10);
    EXPECT_EQ(d.guest->roster().api, 1);
    EXPECT_EQ(d.guest->seat(), 2);
    EXPECT_EQ(d.guest->session(), d.host.session());
    EXPECT_EQ(d.host.peerForSeat(2), d.hl.handleOf(d.gl));
    EXPECT_EQ(d.guest->peerForSeat(1), d.gl.handleOf(d.hl));
    EXPECT_EQ(d.guest->joinedHost().session, d.host.session());
    // ADVERT stops at Start: a copy in flight arrives within the longest delay, and none is sent after.
    d.rig.advance(2000, [&d] { d.observer.drain(); });
    for (const Seen& s : d.observer.of(Type::Advert)) {
      EXPECT_LE(s.atMs, d.startedAt + HOSTILE_MAX_DELAY_MS) << "an ADVERT after Start";
    }
  }
}

TEST(LobbyHandshakeTest, TwoGuestsReachThreeSeatsInJoinOrder) {
  for (uint32_t seed = 1; seed <= 100; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, hostile(), 3);
    FakeLink gl2(d.rig.net);
    GuestLobby second(gl2, d.rig.clock, d.guestGame);
    d.rig.track(second);
    // The first guest joins at once; the second waits until the host has seated the first, so the order is fixed.
    const bool ok = d.rig.until(
        [&] {
          GuestLobby& g = *d.guest;
          if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
          if (d.host.seatsTaken() >= 2 && second.state() == GuestState::Browsing && second.hostCount() > 0) {
            second.join(second.host(0).handle, second.host(0).session);
          }
          if (d.host.state() == HostState::Waiting && d.host.seatsTaken() == 3) d.host.start();
          return d.host.state() == HostState::Started && g.state() == GuestState::Started &&
                 second.state() == GuestState::Started;
        },
        15000);
    ASSERT_TRUE(ok);
    EXPECT_EQ(d.host.roster().seats, 3);
    EXPECT_EQ(d.guest->seat(), 2);
    EXPECT_EQ(second.seat(), 3);
    EXPECT_EQ(d.guest->roster().seats, 3);
    EXPECT_EQ(d.guest->roster().localSeats, 0b010);
    EXPECT_EQ(second.roster().localSeats, 0b100);
    EXPECT_EQ(d.host.peerForSeat(2), d.hl.handleOf(d.gl));
    EXPECT_EQ(d.host.peerForSeat(3), d.hl.handleOf(gl2));
    d.rig.untrack(&second);
  }
}

TEST(LobbyHandshakeTest, ASecondGuestAtTwoSeatsGetsFullAndTheFirstStarts) {
  for (uint32_t seed = 1; seed <= 100; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, hostile());
    FakeLink gl2(d.rig.net);
    GuestLobby second(gl2, d.rig.clock, d.guestGame);
    d.rig.track(second);
    // The first guest joins; the second joins once the host holds the first, is refused, and only then does the host
    // start (a started host no longer advertises, so a second guest that has not listed it by then never could).
    const bool ok = d.rig.until(
        [&] {
          GuestLobby& g = *d.guest;
          if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
          if (d.host.seatsTaken() >= 2 && second.state() == GuestState::Browsing && second.hostCount() > 0) {
            second.join(second.host(0).handle, second.host(0).session);
          }
          if (d.host.state() == HostState::Waiting && d.host.canStart() && second.state() == GuestState::Refused) {
            d.host.start();
          }
          return d.bothStarted() && second.state() == GuestState::Refused;
        },
        15000);
    ASSERT_TRUE(ok) << "guest " << int{static_cast<uint8_t>(d.guest->state())} << " second "
                    << int{static_cast<uint8_t>(second.state())};
    EXPECT_EQ(second.refusal(), Refusal::Full);
    EXPECT_EQ(d.guest->seat(), 2);
    EXPECT_EQ(d.host.roster().seats, 2);
    d.rig.untrack(&second);
  }
}

TEST(LobbyHandshakeTest, ARosterApiIsTheLowestLevel) {
  for (const auto& [hostApi, guestApi] : std::vector<std::pair<int32_t, int32_t>>{{3, 1}, {1, 3}, {2, 2}}) {
    SCOPED_TRACE(hostApi * 10 + guestApi);
    Duo d(1, Faults{}, 2, hostApi, guestApi);
    ASSERT_TRUE(d.run(5000));
    const int32_t lowest = std::min(hostApi, guestApi);
    EXPECT_EQ(d.host.roster().api, lowest);
    EXPECT_EQ(d.guest->roster().api, lowest);
  }
  // Three seats, three levels: the host takes the lowest over both guests. A guest cannot know the other guest's level
  // (ACCEPT carries only {seat, n}), so it takes the lowest of its own and the host's.
  Rig rig;
  const LobbyGame hostGame = testGame(3, 2);
  const LobbyGame highGame = testGame(3, 3);
  const LobbyGame lowGame = testGame(3, 1);
  FakeLink hl(rig.net);
  FakeLink highLink(rig.net);
  FakeLink lowLink(rig.net);
  FixedRandom random({0x3A3A3A3Au});
  HostLobby host(hl, rig.clock, random, hostGame, bytesOf("h"));
  GuestLobby high(highLink, rig.clock, highGame);
  GuestLobby low(lowLink, rig.clock, lowGame);
  rig.track(host);
  rig.track(high);
  rig.track(low);
  ASSERT_TRUE(rig.until(
      [&] {
        if (high.state() == GuestState::Browsing && high.hostCount() > 0)
          high.join(high.host(0).handle, high.host(0).session);
        if (host.seatsTaken() >= 2 && low.state() == GuestState::Browsing && low.hostCount() > 0) {
          low.join(low.host(0).handle, low.host(0).session);
        }
        if (host.state() == HostState::Waiting && host.seatsTaken() == 3) host.start();
        return host.state() == HostState::Started && high.state() == GuestState::Started &&
               low.state() == GuestState::Started;
      },
      5000));
  EXPECT_EQ(host.roster().api, 1) << "the lowest over both guests";
  EXPECT_EQ(low.roster().api, 1);
  EXPECT_EQ(high.roster().api, 2) << "the lowest it can know: its own 3 and the host's 2";
  rig.untrack(&host);
  rig.untrack(&high);
  rig.untrack(&low);
}

TEST(LobbyHandshakeTest, TheHostsCancelReachesTheWaitingGuest) {
  {
    Duo d(1, Faults{});
    ASSERT_TRUE(d.rig.until(
        [&] {
          if (d.guest->state() == GuestState::Browsing && d.guest->hostCount() > 0) {
            d.guest->join(d.guest->host(0).handle, d.guest->host(0).session);
          }
          return d.guest->state() == GuestState::Joined && d.host.seatsTaken() == 2;
        },
        3000));
    d.host.cancel();
    d.rig.advance(100);
    EXPECT_EQ(d.guest->state(), GuestState::HostGone);
    EXPECT_EQ(d.guest->gone(), GoneReason::Left);
  }
  // On a hostile net the three copies may all be lost; the host then says nothing more, and the guest leaves at the
  // silence limit (the last frame can have been delayed by HOSTILE_MAX_DELAY_MS).
  for (uint32_t seed = 1; seed <= 100; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, hostile());
    ASSERT_TRUE(d.rig.until(
        [&] {
          if (d.guest->state() == GuestState::Browsing && d.guest->hostCount() > 0) {
            d.guest->join(d.guest->host(0).handle, d.guest->host(0).session);
          }
          return d.guest->state() == GuestState::Joined && d.host.seatsTaken() == 2;
        },
        10000));
    d.host.cancel();
    const uint64_t cancelledAt = d.rig.now();
    ASSERT_TRUE(d.rig.until([&] { return d.guest->state() == GuestState::HostGone; }, 10000 + HOSTILE_MAX_DELAY_MS));
    EXPECT_LE(d.rig.now(), cancelledAt + 10000 + HOSTILE_MAX_DELAY_MS + STEP_MS);
    EXPECT_TRUE(d.guest->gone() == GoneReason::Left || d.guest->gone() == GoneReason::Silent);
  }
}

TEST(LobbyHandshakeTest, TheHostsSilenceReachesTheWaitingGuestAtTheLimit) {
  Duo d(1, Faults{});
  uint64_t lastHeard = 0;  // the last step in which the guest's link took a frame
  uint32_t received = 0;
  d.rig.onStep = [&] {
    if (d.guest->link().stats().framesReceived != received) {
      received = d.guest->link().stats().framesReceived;
      lastHeard = d.rig.now();
    }
  };
  ASSERT_TRUE(d.rig.until(
      [&] {
        if (d.guest->state() == GuestState::Browsing && d.guest->hostCount() > 0) {
          d.guest->join(d.guest->host(0).handle, d.guest->host(0).session);
        }
        return d.guest->state() == GuestState::Joined;
      },
      3000));
  d.rig.advance(3000);
  ASSERT_EQ(d.guest->state(), GuestState::Joined);
  // The host stops ticking (a crash, or out of range): the last frame it sent is the guest's last heard.
  d.rig.untrack(&d.host);
  d.rig.advance(500);
  ASSERT_GT(lastHeard, 0u);
  while (d.guest->state() == GuestState::Joined && d.rig.now() < lastHeard + 20000) {
    ASSERT_LT(d.rig.now(), lastHeard + 10000) << "still Joined at the limit";
    d.rig.advance(STEP_MS);
  }
  EXPECT_EQ(d.guest->state(), GuestState::HostGone);
  EXPECT_EQ(d.guest->gone(), GoneReason::Silent);
  EXPECT_GE(d.rig.now(), lastHeard + 10000) << "not before the limit";
  EXPECT_LE(d.rig.now(), lastHeard + 10000 + STEP_MS);
}

// Traffic budgets (FakeLinkNet::setCarriedBudget): generous ceilings, set from the longest run each test allows, so a
// packet storm fails a test instead of passing unseen. Observed worst cases are 11 (lost JOIN, 100 seeds) and 24
// (restart); the ceilings are more than ten times that, still far below what an echoing host would carry.
constexpr uint64_t LOST_JOIN_BUDGET = 300;
constexpr uint64_t ONE_WAY_BUDGET = 300;
constexpr uint64_t RESTART_BUDGET = 300;

TEST(LobbyHandshakeTest, ALostJoinIsResentUntilTheMatchStarts) {
  Faults half;
  half.lossPercent = 50;
  for (uint32_t seed = 1; seed <= 100; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, half);
    d.rig.net.setCarriedBudget(LOST_JOIN_BUDGET);
    ASSERT_TRUE(d.run(30000)) << "host " << int{static_cast<uint8_t>(d.host.state())} << " guest "
                              << int{static_cast<uint8_t>(d.guest->state())};
    EXPECT_EQ(d.guest->seat(), 2);
    EXPECT_FALSE(d.rig.net.overBudget()) << "carried " << d.rig.net.carried();
  }
}

TEST(LobbyHandshakeTest, OneWayRangeStillConverges) {
  // Guest to host blackholed for 2 s from the start: the first JOINs are lost, and are resent.
  {
    Duo d(1, Faults{});
    d.rig.net.setCarriedBudget(ONE_WAY_BUDGET);
    d.rig.net.blackhole(d.gl.index());
    bool cleared = false;
    ASSERT_TRUE(d.rig.until(
        [&] {
          if (!cleared && d.rig.now() >= 2000) {
            d.rig.net.blackhole(d.gl.index(), false);
            cleared = true;
          }
          d.drive();
          return d.bothStarted();
        },
        10000));
    EXPECT_GE(d.rig.now(), 2000u);
    EXPECT_EQ(d.guest->seat(), 2);
    EXPECT_FALSE(d.rig.net.overBudget());
  }
  // Host to guest blackholed for 2 s after the guest joined: the ack, the adverts, and the ACCEPT are lost.
  {
    Duo d(2, Faults{});
    d.rig.net.setCarriedBudget(ONE_WAY_BUDGET);
    ASSERT_TRUE(d.rig.until([&] { return d.guest->hostCount() > 0; }, 3000));
    d.guest->join(d.guest->host(0).handle, d.guest->host(0).session);
    d.rig.net.blackhole(d.hl.index());
    const uint64_t cut = d.rig.now();
    bool cleared = false;
    ASSERT_TRUE(d.rig.until(
        [&] {
          if (!cleared && d.rig.now() >= cut + 2000) {
            d.rig.net.blackhole(d.hl.index(), false);
            cleared = true;
          }
          d.drive();
          return d.bothStarted();
        },
        10000));
    EXPECT_GE(d.rig.now(), cut + 2000);
    EXPECT_EQ(d.guest->seat(), 2);
    EXPECT_FALSE(d.rig.net.overBudget());
  }
}

TEST(LobbyHandshakeTest, AHostHeardBroadcastingAgainAfterStartEndsTheMatch) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.state(), GuestState::Started);
  const uint64_t startedAt = g.rig.now();
  RecSink sink(g.rig.clock);
  g.guest.handOff(sink);
  // Inside the grace window it is a late copy of a pre-Start advert.
  g.run(500);
  ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), HOST_SESSION));
  g.run(STEP_MS);
  EXPECT_EQ(sink.count(Type::Abort), 0u);
  EXPECT_EQ(g.guest.ended().kind, LobbyEnd::Kind::None);
  // After it, the host is in a lobby again: the match is over.
  g.run(600);
  ASSERT_GE(g.rig.now(), startedAt + 1000);
  ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), HOST_SESSION));
  g.run(STEP_MS);
  ASSERT_EQ(sink.count(Type::Abort), 1u);
  EXPECT_EQ(sink.frames.back().reason, AbortReason::Left);
  EXPECT_EQ(sink.frames.back().from, g.link.handleOf(host.link));
  // Heard again, it is not ended twice.
  ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), HOST_SESSION));
  g.run(STEP_MS);
  EXPECT_EQ(sink.count(Type::Abort), 1u);
  // An advert of another session, or from another device, is not the host's.
  GuestRig o;
  RawPeer& host2 = o.addHost();
  RawPeer& other = o.addHost();
  o.joinAndAck(host2, HOST_SESSION);
  ASSERT_TRUE(host2.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  o.run(2000);
  RecSink sink2(o.rig.clock);
  o.guest.handOff(sink2);
  ASSERT_TRUE(other.broadcast(advertFrameFor(o.game, 1, 2), HOST_SESSION));
  ASSERT_TRUE(host2.broadcast(advertFrameFor(o.game, 1, 2), 0x0BADF00Du));
  o.run(STEP_MS);
  EXPECT_EQ(sink2.count(Type::Abort), 0u);
}

TEST(LobbyHandshakeTest, NoFalseEndFromLateCopiesAcrossSeeds) {
  for (uint32_t seed = 1; seed <= 100; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, hostile());
    RecSink hostSink(d.rig.clock);
    RecSink guestSink(d.rig.clock);
    bool hostAttached = false;
    bool guestAttached = false;
    ASSERT_TRUE(d.rig.until(
        [&] {
          d.drive();
          if (d.host.state() == HostState::Started && !hostAttached) {
            d.host.handOff(hostSink);
            hostAttached = true;
          }
          if (d.guest->state() == GuestState::Started && !guestAttached) {
            d.guest->handOff(guestSink);
            guestAttached = true;
          }
          return hostAttached && guestAttached;
        },
        8000));
    d.rig.advance(6000);
    EXPECT_EQ(guestSink.count(Type::Abort), 0u) << "late copies of pre-Start adverts must not end the match";
    EXPECT_EQ(hostSink.count(Type::Abort), 0u);
    EXPECT_TRUE(guestSink.silent.empty());
    EXPECT_TRUE(hostSink.silent.empty());
  }
}

TEST(LobbyHandshakeTest, ARestartedGuestIsCaughtByTheHostsSilence) {
  Duo d(1, Faults{});
  ASSERT_TRUE(d.run(5000));
  RecSink sink(d.rig.clock);
  d.host.handOff(sink);
  d.rig.net.setCarriedBudget(d.rig.net.carried() + RESTART_BUDGET);
  d.rig.advance(3000);
  // The guest's device comes up again on its address and says nothing (no ABORT).
  d.rig.untrack(d.guest.get());
  d.gl.restart();
  d.rig.advance(100);
  const uint64_t lastHeard = d.hostHeardAt;
  d.rig.advance(10500);
  ASSERT_EQ(sink.silent.size(), 1u);
  EXPECT_EQ(sink.silent[0], d.hl.handleOf(d.gl));
  EXPECT_GE(sink.silentAt[0], lastHeard + 10000);
  EXPECT_LE(sink.silentAt[0], lastHeard + 10000 + STEP_MS);
  EXPECT_FALSE(d.rig.net.overBudget()) << "carried " << d.rig.net.carried();
}

TEST(LobbyHandshakeTest, JoinsFromMatchedDevicesNeverEcho) {
  Duo d(1, Faults{});
  ASSERT_TRUE(d.run(5000));
  RecSink hostSink(d.rig.clock);
  RecSink guestSink(d.rig.clock);
  d.host.handOff(hostSink);
  d.guest->handOff(guestSink);
  RawPeer stranger(d.rig.net, d.rig.clock);
  stranger.connect(d.hl);
  d.rig.advance(500);
  const Bytes seatedJoin = RawPeer::encoded(joinFrameFor(d.guestGame), d.host.session(), 0, NO_ACK);
  ASSERT_FALSE(seatedJoin.empty());
  const PeerHandle toHost = d.gl.handleOf(d.hl);
  constexpr int ROUNDS = 100;
  constexpr int COPIES = 8;
  // The budget: what the storm itself puts on the air (ROUNDS x COPIES from each of the two sources), one ACK back per
  // copy of the seated guest's JOIN, at most one ABORT per refusalAnswerMs to the stranger over the 3 s, and
  // 100 for the steady keepalives and slack. A host that echoed every copy of both would roughly double the first term.
  const uint64_t budget = d.rig.net.carried() + 2u * ROUNDS * COPIES + ROUNDS * COPIES + 3000 / 200 + 1 + 100;
  d.rig.net.setCarriedBudget(budget);
  for (int round = 0; round < ROUNDS; round++) {
    for (int c = 0; c < COPIES; c++) {
      ASSERT_TRUE(d.gl.send(toHost, seatedJoin));
      ASSERT_TRUE(stranger.putBytes(RawPeer::encoded(joinFrameFor(d.guestGame), d.host.session(), 0, NO_ACK)));
    }
    d.rig.advance(30, [&] { stranger.drain(); });
  }
  d.rig.advance(500, [&] { stranger.drain(); });
  EXPECT_FALSE(d.rig.net.overBudget()) << "carried " << d.rig.net.carried() << " of " << budget;
  EXPECT_EQ(d.guest->ended().kind, LobbyEnd::Kind::None) << "no ABORT to the seated guest";
  EXPECT_EQ(d.host.ended().kind, LobbyEnd::Kind::None);
  EXPECT_EQ(guestSink.count(Type::Abort), 0u);
  EXPECT_LE(stranger.countAborts(AbortReason::Full), size_t{3500 / 200 + 1}) << "at most one per refusalAnswerMs";
  EXPECT_GE(stranger.countAborts(AbortReason::Full), 1u);
  EXPECT_EQ(stranger.count(Type::Ack), 0u);
  EXPECT_EQ(d.host.state(), HostState::Started);
  EXPECT_EQ(d.guest->state(), GuestState::Started);
}

// The host's seats are held across a rejoin; see Design Notes 7. `how` picks what the guest's ABORT meets.
enum class Leave { AbortDelivered, AbortLost, AbortLostAndSilence };

void rejoinAfterLeaving(const uint32_t seed, const Leave how) {
  SCOPED_TRACE(seed);
  Duo d(seed, hostile());
  ASSERT_TRUE(d.rig.until(
      [&] {
        GuestLobby& g = *d.guest;
        if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
        return g.state() == GuestState::Joined && d.host.seatsTaken() == 2;
      },
      10000));
  d.events();
  d.startWhenJoined = true;
  d.rig.advance(500);  // longer than any delay: no copy of the JOIN is still on its way to meet the ABORT
  if (how == Leave::AbortDelivered) {
    d.rig.net.setFaults(Faults{});  // the three copies get through
  } else {
    d.rig.net.blackhole(d.gl.index());  // every copy is lost
  }
  d.guest->cancel();
  d.rig.advance(500);  // longer than any delay: a late ABORT copy cannot meet the next JOIN
  if (how == Leave::AbortDelivered) {
    EXPECT_EQ(d.host.seatsTaken(), 1) << "the ABORT freed the seat";
  } else {
    EXPECT_EQ(d.host.seatsTaken(), 2) << "no ABORT, so the seat is still held";
  }
  if (how == Leave::AbortLostAndSilence) {
    d.rig.advance(11000);
    EXPECT_EQ(d.host.seatsTaken(), 1) << "the silence limit freed it";
  }
  d.rig.net.blackhole(d.gl.index(), false);
  d.rig.net.setFaults(hostile());
  d.replaceGuest(d.gl);
  ASSERT_TRUE(d.run(12000)) << "rejoin did not start";
  EXPECT_EQ(d.guest->seat(), 2);
  EXPECT_EQ(d.host.roster().seats, 2);
  EXPECT_EQ(d.host.peerForSeat(2), d.hl.handleOf(d.gl));
  const std::vector<HostEvent> events = d.events();
  size_t joins = 0;
  for (const HostEvent& e : events) joins += e.kind == HostEventKind::GuestJoined ? 1 : 0;
  // A kept record makes the new JOIN a duplicate (acked, not offered): one seat, one event. A freed one is seated
  // afresh.
  EXPECT_EQ(joins, how == Leave::AbortLost ? 0u : 1u);
}

TEST(LobbyHandshakeTest, AGuestThatLeavesAndRejoinsReachesAStartedRoster) {
  for (uint32_t seed = 1; seed <= 100; seed++) rejoinAfterLeaving(seed, Leave::AbortDelivered);
  for (uint32_t seed = 1; seed <= 100; seed++) rejoinAfterLeaving(seed, Leave::AbortLost);
  for (uint32_t seed = 1; seed <= 100; seed++) rejoinAfterLeaving(seed, Leave::AbortLostAndSilence);
}

TEST(LobbyHandshakeTest, AGuestThatRestartsOnItsAddressRejoins) {
  for (uint32_t seed = 1; seed <= 100; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, hostile());
    ASSERT_TRUE(d.rig.until(
        [&] {
          GuestLobby& g = *d.guest;
          if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
          return g.state() == GuestState::Joined && d.host.seatsTaken() == 2;
        },
        10000));
    d.events();
    d.rig.advance(500);
    // The device comes up again on its address: no ABORT, the old lobby is gone.
    d.rig.untrack(d.guest.get());
    d.gl.restart();
    d.replaceGuest(d.gl);
    const uint64_t restartedAt = d.rig.now();
    bool over = false;
    ASSERT_TRUE(d.rig.until(
        [&] {
          GuestLobby& g = *d.guest;
          if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
          if (d.host.state() == HostState::Waiting && g.state() == GuestState::Joined) d.host.start();
          over = over || d.host.seatsTaken() > 2;
          return d.bothStarted();
        },
        4000));
    EXPECT_LE(d.rig.now(), restartedAt + 4000) << "measured worst case over 100 seeds: 3,520 ms";
    EXPECT_FALSE(over);
    EXPECT_EQ(d.guest->seat(), 2);
    EXPECT_EQ(d.host.roster().seats, 2);
    EXPECT_EQ(d.host.peerForSeat(2), d.hl.handleOf(d.gl));
  }
}

TEST(LobbyHandshakeTest, AGuestThatRestartsOnAnotherAddressRejoinsAfterTheSilenceLimit) {
  for (uint32_t seed = 1; seed <= 50; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed, hostile());
    ASSERT_TRUE(d.rig.until(
        [&] {
          GuestLobby& g = *d.guest;
          if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
          return g.state() == GuestState::Joined && d.host.seatsTaken() == 2;
        },
        10000));
    d.events();
    d.rig.advance(500);
    // The old device is gone; the host last heard it at `lastHeard` (a frame in flight arrives within the delay).
    d.rig.untrack(d.guest.get());
    d.gl.end();
    d.rig.advance(HOSTILE_MAX_DELAY_MS + 50);
    const uint64_t lastHeard = d.hostHeardAt;
    FakeLink fresh(d.rig.net);  // a new address
    GuestLobby& g = d.replaceGuest(fresh);
    uint64_t lastRetry = d.rig.now();
    bool sawFull = false;
    bool started = false;
    for (uint32_t t = 0; t < 20000 && !started; t += STEP_MS) {
      d.rig.advance(STEP_MS);
      if (g.state() == GuestState::Refused) {
        sawFull = sawFull || g.refusal() == Refusal::Full;
        if (d.rig.now() >= lastRetry + 1000) {
          g.backToList();
          lastRetry = d.rig.now();
        }
      }
      if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
      if (d.host.state() == HostState::Waiting && g.state() == GuestState::Joined) {
        d.host.start();
        EXPECT_GE(d.rig.now(), lastHeard + 10000) << "the old seat is held for the silence limit";
      }
      started = d.host.state() == HostState::Started && g.state() == GuestState::Started;
    }
    ASSERT_TRUE(started);
    EXPECT_TRUE(sawFull) << "the new address is refused until the old seat frees";
    EXPECT_LE(d.rig.now(), lastHeard + 10000 + 4000);
    EXPECT_EQ(g.seat(), 2);
    EXPECT_EQ(d.host.peerForSeat(2), d.hl.handleOf(fresh));
  }
}

TEST(LobbyHandshakeTest, ARefusedGuestCanRetryWhenASeatFrees) {
  Duo d(1, Faults{});
  FakeLink gl2(d.rig.net);
  GuestLobby second(gl2, d.rig.clock, d.guestGame);
  d.rig.track(second);
  // The first guest joins and is seated; the second is refused.
  ASSERT_TRUE(d.rig.until(
      [&] {
        GuestLobby& g = *d.guest;
        if (g.state() == GuestState::Browsing && g.hostCount() > 0) g.join(g.host(0).handle, g.host(0).session);
        if (d.host.seatsTaken() == 2 && second.state() == GuestState::Browsing && second.hostCount() > 0) {
          second.join(second.host(0).handle, second.host(0).session);
        }
        return second.state() == GuestState::Refused && g.state() == GuestState::Joined;
      },
      5000));
  EXPECT_EQ(second.refusal(), Refusal::Full);
  // The first leaves; the second goes back to the list and joins again.
  d.guest->cancel();
  d.rig.advance(300);
  ASSERT_EQ(d.host.seatsTaken(), 1);
  second.backToList();
  ASSERT_TRUE(d.rig.until(
      [&] {
        if (second.state() == GuestState::Browsing && second.hostCount() > 0) {
          second.join(second.host(0).handle, second.host(0).session);
        }
        if (d.host.state() == HostState::Waiting && second.state() == GuestState::Joined) d.host.start();
        return d.host.state() == HostState::Started && second.state() == GuestState::Started;
      },
      5000));
  EXPECT_EQ(second.seat(), 2);
  d.rig.untrack(&second);
}

TEST(LobbyHandshakeTest, AStrangerWithTheSessionIdCannotExhaustTheHostsPeers) {
  Duo d(1, Faults{});
  std::deque<RawPeer> strangers;
  const Bytes snapshot = {1, 2};
  for (int i = 0; i < 12; i++) {
    strangers.emplace_back(d.rig.net, d.rig.clock);
    RawPeer& s = strangers.back();
    s.connect(d.hl);
    const uint32_t session = d.host.session();
    ASSERT_TRUE(s.put(acceptFrame(2, 2), session, 0, 0));
    ASSERT_TRUE(s.put(stateFrame(snapshot), session, 0, 0));
    Frame badJoin = joinFrameFor(d.guestGame);
    badJoin.join.pkgHash[0] ^= 0x7F;
    ASSERT_TRUE(s.put(badJoin, session));
    ASSERT_TRUE(s.put(abortFrame(AbortReason::Left), session));
    d.rig.advance(STEP_MS);
  }
  EXPECT_EQ(d.host.link().stats().peerTableFull, 0u);
  EXPECT_EQ(d.host.seatsTaken(), 1);
  ASSERT_TRUE(d.run(5000)) << "a real guest still joins and starts";
  EXPECT_EQ(d.host.link().stats().peerTableFull, 0u);
}

}  // namespace
