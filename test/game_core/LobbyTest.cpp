#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "LobbySupport.h"

// GameCore::HostLobby and GuestLobby, each against hand-built peers (RawPeer, an oracle independent of both lobbies):
// the verdicts, the host's adverts and refusals, the guest's list and states, and the hand-off to the match. The two
// lobbies together are LobbyHandshakeTest.cpp. Every bound is in fake milliseconds from LobbySettings.

using namespace GameCore;
using namespace GameCore::LobbyTest;

namespace {

constexpr uint32_t FOREIGN = 0x0BADF00Du;

TEST(LobbyTest, RosterNearbyHasOneLocalSeat) {
  const Roster r = Roster::nearby(3, 2, 1);
  EXPECT_EQ(r.mode, Mode::Nearby);
  EXPECT_EQ(r.seats, 3);
  EXPECT_EQ(r.localSeats, 0b010);
  EXPECT_EQ(r.api, 1);
  EXPECT_TRUE(r.isLocal(2));
  EXPECT_FALSE(r.isLocal(1));
  EXPECT_FALSE(r.isLocal(3));
  EXPECT_EQ(r.firstLocalSeat(), 2);
  EXPECT_EQ(r.localSeatCount(), 1);
  EXPECT_EQ(Roster::nearby(2, 1, 7).api, 7);
  // A local seat outside 1..seats leaves no seat local, and nothing crashes.
  for (const uint8_t bad : {0, 4, 255}) {
    const Roster none = Roster::nearby(3, bad, 1);
    EXPECT_EQ(none.localSeats, 0) << int{bad};
    EXPECT_EQ(none.firstLocalSeat(), 0);
    EXPECT_EQ(none.localSeatCount(), 0);
  }
  // Seats above MAX_SEATS clamp the mask as Roster::pass does.
  EXPECT_EQ(Roster::nearby(20, 16, 1).localSeats, 0x8000);
  EXPECT_EQ(Roster::nearby(20, 17, 1).localSeats, 0);
}

TEST(LobbyTest, VersionVerdictTable) {
  LobbyGame own = testGame();
  auto hash = own.pkgHash;
  auto other = hash;
  other[7] ^= 1;
  // A different package hash is never joinable.
  for (const bool preview : {true, false}) {
    own.apiPreview = preview;
    EXPECT_EQ(versionVerdict(own, other, 1, own.surface), HostVerdict::DifferentVersion);
    EXPECT_EQ(versionVerdict(own, other, 2, own.surface + 1), HostVerdict::DifferentVersion);
  }
  // This device's level is a preview: the surface must match, whatever the peer's level.
  own.apiPreview = true;
  EXPECT_EQ(versionVerdict(own, hash, 1, own.surface), HostVerdict::Joinable);
  for (const uint8_t peerApi : {0, 1, 2}) {
    EXPECT_EQ(versionVerdict(own, hash, peerApi, own.surface + 1), HostVerdict::DifferentVersion) << int{peerApi};
  }
  // This device's level is frozen: a lower or equal peer is frozen too, so the surface is not compared; a newer peer
  // is taken as a preview, so it is.
  own.apiPreview = false;
  EXPECT_EQ(versionVerdict(own, hash, 0, own.surface + 1), HostVerdict::Joinable);
  EXPECT_EQ(versionVerdict(own, hash, 1, own.surface + 1), HostVerdict::Joinable);
  EXPECT_EQ(versionVerdict(own, hash, 2, own.surface + 1), HostVerdict::DifferentVersion);
  EXPECT_EQ(versionVerdict(own, hash, 2, own.surface), HostVerdict::Joinable);
}

TEST(LobbyTest, LobbyGameLimits) {
  LobbyGame g = testGame();
  struct Case {
    int32_t min, max, hostMax;
    uint8_t limit, start;
  };
  const Case cases[] = {
      {2, 2, 2, 2, 2},  {2, 4, 3, 3, 2},      {3, 4, 4, 4, 3},          {2, 4, 2, 2, 2},
      {0, 2, 4, 2, 2},  {1, 2, 4, 2, 2},      {2, 40, 40, 16, 2},       {2, 4, -1, 0, 2},
      {2, -3, 4, 0, 2}, {20, 40, 40, 16, 20}, {300, 400, 400, 16, 255}, {2, 2, 0, 0, 2},
  };
  for (const Case& c : cases) {
    g.seatsMin = c.min;
    g.seatsMax = c.max;
    g.caps.maxSeats = c.hostMax;
    EXPECT_EQ(g.seatLimit(), c.limit) << c.min << ".." << c.max << " host " << c.hostMax;
    EXPECT_EQ(g.startSeats(), c.start) << c.min << ".." << c.max << " host " << c.hostMax;
  }
  // The id is clamped at 32 bytes and stays terminated.
  g.setGameId(std::string(40, 'x'));
  EXPECT_EQ(std::string(g.gameId), std::string(32, 'x'));
  g.setGameId("ab");
  EXPECT_EQ(std::string(g.gameId), "ab");
  EXPECT_EQ(g.gameId[2], '\0');
  EXPECT_EQ(g.gameId[32], '\0');
}

TEST(LobbyTest, ASessionIsNeverZero) {
  FixedRandom skipsZeros({0, 0, 7});
  EXPECT_EQ(drawSession(skipsZeros), 7u);
  EXPECT_EQ(skipsZeros.draws, 3u);
  FixedRandom allZeros({0});
  EXPECT_EQ(drawSession(allZeros), 1u);
  EXPECT_EQ(allZeros.draws, 8u) << "at most 8 draws";
  FixedRandom plain({0xFFFFFFFFu});
  EXPECT_EQ(drawSession(plain), 0xFFFFFFFFu);
  // The lobby draws its session the same way.
  Rig rig;
  FakeLink link(rig.net);
  FixedRandom zerosThenNine({0, 9});
  HostLobby host(link, rig.clock, zerosThenNine, testGame(), bytesOf("h"));
  EXPECT_EQ(host.session(), 9u);
  EXPECT_EQ(host.link().session(), 9u);
}

TEST(LobbyTest, HostAdvertCarriesTheGameTheLabelAndTheSeats) {
  const Bytes label = {'A', 0x00, 'B', 0xFF};
  HostRig h(2, label);
  h.run(STEP_MS);
  ASSERT_EQ(h.observer.count(Type::Advert), 1u) << "at once, on the first tick";
  const Seen a = h.observer.of(Type::Advert)[0];
  EXPECT_EQ(a.session, h.session());
  EXPECT_EQ(a.gameId, "ttt");
  EXPECT_EQ(a.pkgHash, h.game.pkgHash);
  EXPECT_EQ(a.manifestApi, h.game.manifestApi);
  EXPECT_EQ(a.hostApi, h.game.caps.api);
  EXPECT_EQ(a.surface, h.game.surface);
  EXPECT_EQ(a.seatsMax, 2);
  EXPECT_EQ(a.seatsTaken, 1);
  EXPECT_EQ(a.label, label) << "opaque bytes, a 0x00 included";
  EXPECT_EQ(a.seq, 0);
  EXPECT_EQ(a.ack, 0);
  h.run(1500);
  std::vector<uint64_t> times;
  for (const Seen& s : h.observer.of(Type::Advert)) times.push_back(s.atMs);
  EXPECT_EQ(times, (std::vector<uint64_t>{10, 510, 1010, 1510}));
}

TEST(LobbyTest, HostAdvertClampsALongLabel) {
  HostRig h(2, Bytes(20, 'q'));
  h.run(STEP_MS);
  ASSERT_EQ(h.observer.count(Type::Advert), 1u);
  EXPECT_EQ(h.observer.of(Type::Advert)[0].label, Bytes(16, 'q'));
}

TEST(LobbyTest, ASeatChangeAdvertisesAtOnceAndNothingIsAdvertisedAfterStartOrCancel) {
  HostRig h(3);
  h.run(100);
  ASSERT_EQ(h.observer.count(Type::Advert), 1u);
  RawPeer& g1 = h.addPeer();
  ASSERT_TRUE(h.join(g1));
  h.run(STEP_MS);
  ASSERT_EQ(h.observer.count(Type::Advert), 2u) << "a seat change does not wait for the interval";
  EXPECT_EQ(h.observer.of(Type::Advert)[1].seatsTaken, 2);
  EXPECT_EQ(h.observer.of(Type::Advert)[1].seatsMax, 3);
  EXPECT_EQ(h.host.start(), StartResult::Started);
  const size_t atStart = h.observer.count(Type::Advert);
  h.run(2000);
  EXPECT_EQ(h.observer.count(Type::Advert), atStart) << "ADVERT stops at Start";

  HostRig c;
  c.run(100);
  c.host.cancel();
  const size_t atCancel = c.observer.count(Type::Advert);
  c.run(2000);
  EXPECT_EQ(c.observer.count(Type::Advert), atCancel) << "and at cancel";
  EXPECT_EQ(c.host.state(), HostState::Closed);
}

TEST(LobbyTest, HostRefusesAForeignHashAndAForeignSurfaceJoin) {
  HostRig h;
  h.run(STEP_MS);
  struct Variant {
    const char* name;
    std::function<void(Frame&)> change;
  };
  const Variant variants[] = {
      {"hash", [](Frame& f) { f.join.pkgHash[3] ^= 0x40; }},
      {"surface", [](Frame& f) { f.join.surface ^= 1; }},
  };
  for (const Variant& v : variants) {
    RawPeer& p = h.addPeer();
    Frame join = joinFrameFor(h.game);
    v.change(join);
    ASSERT_TRUE(p.put(join, h.session()));
    h.run(STEP_MS);
    EXPECT_EQ(p.countAborts(AbortReason::VersionMismatch), 1u) << v.name;
    EXPECT_EQ(p.countAborts(), 1u) << v.name;
  }
  EXPECT_EQ(h.host.seatsTaken(), 1);
  EXPECT_TRUE(h.events().empty());
  // The peer table never fills over 20 strangers.
  for (int i = 0; i < 20; i++) {
    RawPeer& p = h.addPeer();
    Frame join = joinFrameFor(h.game);
    join.join.pkgHash[0] ^= static_cast<uint8_t>(1 + i);
    ASSERT_TRUE(p.put(join, h.session()));
    h.run(STEP_MS);
    EXPECT_EQ(p.countAborts(AbortReason::VersionMismatch), 1u);
  }
  EXPECT_EQ(h.host.link().stats().peerTableFull, 0u);
  // A good guest is still seated afterwards.
  RawPeer& good = h.addPeer();
  ASSERT_TRUE(h.join(good));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.seatsTaken(), 2);
}

TEST(LobbyTest, AFullHostAnswersAbortFullAndKeepsItsGuest) {
  HostRig h;
  RawPeer& first = h.addPeer();
  ASSERT_TRUE(h.join(first));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 2);
  RawPeer& second = h.addPeer();
  ASSERT_TRUE(h.join(second));
  h.run(STEP_MS);
  EXPECT_EQ(second.countAborts(AbortReason::Full), 1u);
  EXPECT_EQ(first.countAborts(), 0u) << "the first guest is unaffected";
  EXPECT_EQ(h.host.seatsTaken(), 2);
  EXPECT_EQ(h.host.link().stats().peerTableFull, 0u);
  // The host still starts, with the first guest.
  ASSERT_EQ(h.host.start(), StartResult::Started);
  h.run(STEP_MS);
  ASSERT_EQ(first.count(Type::Accept), 1u);
  EXPECT_EQ(first.of(Type::Accept)[0].seat, 2);
  EXPECT_EQ(first.of(Type::Accept)[0].n, 2);
  EXPECT_EQ(second.count(Type::Accept), 0u);
  // The refused stranger was forgotten, so its resend is refused afresh and never acked.
  h.run(2000);
  EXPECT_EQ(second.count(Type::Ack), 0u);
}

TEST(LobbyTest, AStrangerSendingAcceptStateOrAdvertHoldsNoSlot) {
  HostRig h;  // two peer slots: one guest plus one free
  h.run(STEP_MS);
  const Bytes snapshot = {1, 2, 3};
  for (int i = 0; i < 6; i++) {
    RawPeer& p = h.addPeer();
    ASSERT_TRUE(p.put(acceptFrame(2, 2), h.session()));
    ASSERT_TRUE(p.put(stateFrame(snapshot), h.session()));
    ASSERT_TRUE(p.put(advertFrameFor(h.game, 1, 2), h.session()));
    h.run(STEP_MS);
    EXPECT_EQ(p.count(Type::Ack), 0u) << "a stranger's frame is not acked";
  }
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  RawPeer& late = h.addPeer();
  ASSERT_TRUE(h.join(late));
  h.run(STEP_MS);
  EXPECT_EQ(late.countAborts(AbortReason::Full), 1u) << "a free slot was left for the stranger to be answered in";
  EXPECT_EQ(h.host.seatsTaken(), 2);
  EXPECT_EQ(h.host.link().stats().peerTableFull, 0u);
}

TEST(LobbyTest, RefusalAnswersAreRateLimited) {
  HostRig h;
  RawPeer& first = h.addPeer();
  ASSERT_TRUE(h.join(first));
  h.run(STEP_MS);
  RawPeer& stranger = h.addPeer();
  for (int i = 0; i < 8; i++) ASSERT_TRUE(h.join(stranger));
  h.run(STEP_MS);
  EXPECT_EQ(stranger.countAborts(), 1u) << "8 copies at once, one answer (crossplay testJoinAnswersAreRateLimited)";
  h.run(150);
  ASSERT_TRUE(h.join(stranger));
  h.run(STEP_MS);
  EXPECT_EQ(stranger.countAborts(), 1u) << "still inside refusalAnswerMs";
  h.run(100);
  ASSERT_TRUE(h.join(stranger));
  h.run(STEP_MS);
  EXPECT_EQ(stranger.countAborts(), 2u) << "one more after the interval";
}

TEST(LobbyTest, GuestHeardMsIsZeroBeforeAGuestAndAtItsJoin) {
  HostRig h;
  EXPECT_EQ(h.host.guestHeardMs(1), 0u) << "the host's own seat";
  EXPECT_EQ(h.host.guestHeardMs(2), 0u) << "no guest yet";
  RawPeer& guest = h.addPeer();
  h.run(100);
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 2);
  const uint64_t heard = h.host.guestHeardMs(2);
  EXPECT_GT(heard, 100u) << "it starts at the JOIN";
  EXPECT_LE(heard, h.rig.now());
  EXPECT_EQ(h.host.guestHeardMs(3), 0u) << "an unknown seat";
  EXPECT_EQ(h.host.guestHeardMs(0), 0u);
}

TEST(LobbyTest, GuestHeardMsAdvancesWithKeepalivesAndOnlyThose) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  const uint64_t atJoin = h.host.guestHeardMs(2);
  h.run(2000);
  EXPECT_EQ(h.host.guestHeardMs(2), atJoin) << "the host's own keepalives do not count";
  ASSERT_TRUE(guest.putAck(h.session(), 0));
  h.run(STEP_MS);
  const uint64_t atAck = h.host.guestHeardMs(2);
  EXPECT_GT(atAck, atJoin + 1900) << "a guest's ACK";
  Frame ping;
  ping.header.type = Type::Ping;
  h.run(500);
  ASSERT_TRUE(guest.put(ping, h.session(), 0, 0));
  h.run(STEP_MS);
  EXPECT_GT(h.host.guestHeardMs(2), atAck + 400) << "a guest's PING";
  const uint64_t atPing = h.host.guestHeardMs(2);
  h.run(500);
  ASSERT_TRUE(guest.put(ping, h.session() + 1, 0, 0));  // another session
  h.run(STEP_MS);
  EXPECT_EQ(h.host.guestHeardMs(2), atPing) << "a frame of another session is not heard";
}

TEST(LobbyTest, GuestHeardMsOfASeatThatLeftIsZeroAndItIsPerSeat) {
  HostRig h(3);
  RawPeer& a = h.addPeer();
  RawPeer& b = h.addPeer();
  ASSERT_TRUE(h.join(a));
  h.run(200);
  ASSERT_TRUE(h.join(b));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 3);
  const uint64_t heardA = h.host.guestHeardMs(2);
  const uint64_t heardB = h.host.guestHeardMs(3);
  EXPECT_GT(heardA, 0u);
  EXPECT_GE(heardB, heardA + 200) << "per seat: B joined 200 ms after A";
  ASSERT_TRUE(a.put(abortFrame(AbortReason::Left), h.session()));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 2);
  EXPECT_EQ(h.host.guestHeardMs(3), 0u) << "the seat closed: B is now seat 2 and seat 3 is empty";
  EXPECT_GE(h.host.guestHeardMs(2), heardB) << "B keeps its own heard time in its new seat";
}

TEST(LobbyTest, StartNeedsASeatedGuest) {
  HostRig h;
  h.run(STEP_MS);
  const uint64_t before = h.rig.net.carried();
  EXPECT_EQ(h.host.start(), StartResult::NotEnoughSeats);
  EXPECT_EQ(h.rig.net.carried(), before) << "nothing sent";
  EXPECT_EQ(h.host.state(), HostState::Waiting);
  EXPECT_FALSE(h.host.canStart());

  RawPeer& leaver = h.addPeer();
  ASSERT_TRUE(h.join(leaver));
  h.run(STEP_MS);
  EXPECT_TRUE(h.host.canStart());
  ASSERT_TRUE(leaver.put(abortFrame(AbortReason::Left), h.session()));
  h.run(STEP_MS);
  const uint64_t beforeLeft = h.rig.net.carried();
  EXPECT_EQ(h.host.start(), StartResult::NotEnoughSeats);
  EXPECT_EQ(h.rig.net.carried(), beforeLeft);

  RawPeer& silent = h.addPeer();
  ASSERT_TRUE(h.join(silent));
  h.run(10100);
  EXPECT_EQ(h.host.seatsTaken(), 1);
  const uint64_t beforeSilent = h.rig.net.carried();
  EXPECT_EQ(h.host.start(), StartResult::NotEnoughSeats);
  EXPECT_EQ(h.rig.net.carried(), beforeSilent);
  // And once started it says so.
  RawPeer& stays = h.addPeer();
  ASSERT_TRUE(h.join(stays));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.start(), StartResult::Started);
  EXPECT_EQ(h.host.start(), StartResult::NotWaiting);
}

TEST(LobbyTest, AGuestsCancelFreesItsSeat) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 2);
  const size_t adverts = h.observer.count(Type::Advert);
  ASSERT_TRUE(guest.put(abortFrame(AbortReason::Left), h.session()));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.seatsTaken(), 1);
  ASSERT_EQ(h.observer.count(Type::Advert), adverts + 1) << "the new count is advertised at once";
  EXPECT_EQ(h.observer.of(Type::Advert).back().seatsTaken, 1);
  const std::vector<HostEvent> events = h.events();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].kind, HostEventKind::GuestJoined);
  EXPECT_EQ(events[0].seat, 2);
  EXPECT_EQ(events[1].kind, HostEventKind::GuestLeft);
  EXPECT_EQ(events[1].seat, 2);
  EXPECT_EQ(h.host.state(), HostState::Waiting) << "the host keeps waiting";
  // The link forgot the guest: a fresh JOIN at seq 0 seats it again (a kept record would call it a duplicate).
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.seatsTaken(), 2);
}

TEST(LobbyTest, ASilentGuestFreesItsSeat) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  const uint64_t joinedAt = h.rig.now();
  h.run(9900);
  EXPECT_EQ(h.host.seatsTaken(), 2) << "not before the silence limit";
  h.run(200);
  EXPECT_EQ(h.host.seatsTaken(), 1);
  const size_t adverts = h.observer.count(Type::Advert);
  ASSERT_GE(adverts, 1u);
  const std::vector<HostEvent> events = h.events();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[1].kind, HostEventKind::GuestSilent);
  EXPECT_EQ(events[1].seat, 2);
  EXPECT_GE(h.rig.now(), joinedAt + 10000);
  // Forgotten: the same address joins afresh at seq 0.
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.seatsTaken(), 2);
  // The advert after the freeing showed one seat taken.
  bool sawOne = false;
  for (const Seen& s : h.observer.of(Type::Advert)) sawOne = sawOne || (s.atMs > joinedAt + 9000 && s.seatsTaken == 1);
  EXPECT_TRUE(sawOne);
}

TEST(LobbyTest, ThreeSeatsGiveContiguousSeatsInArrivalOrder) {
  HostRig h(3);
  RawPeer& a = h.addPeer();
  RawPeer& b = h.addPeer();
  RawPeer& c = h.addPeer();
  ASSERT_TRUE(h.join(a));
  h.run(STEP_MS);
  ASSERT_TRUE(h.join(b));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 3);
  ASSERT_TRUE(a.put(abortFrame(AbortReason::Left), h.session()));  // A leaves between B and C joining
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 2);
  ASSERT_TRUE(h.join(c));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.seatsTaken(), 3);
  ASSERT_EQ(h.host.start(), StartResult::Started);
  h.run(STEP_MS);
  ASSERT_EQ(b.count(Type::Accept), 1u);
  ASSERT_EQ(c.count(Type::Accept), 1u);
  EXPECT_EQ(a.count(Type::Accept), 0u);
  EXPECT_EQ(b.of(Type::Accept)[0].seat, 2);
  EXPECT_EQ(b.of(Type::Accept)[0].n, 3);
  EXPECT_EQ(c.of(Type::Accept)[0].seat, 3);
  EXPECT_EQ(c.of(Type::Accept)[0].n, 3);
  EXPECT_EQ(h.host.roster().seats, 3);
  EXPECT_EQ(h.host.roster().localSeats, 1);
  EXPECT_EQ(h.host.roster().mode, Mode::Nearby);
  EXPECT_EQ(h.host.peerForSeat(1), NO_PEER);
  EXPECT_EQ(h.host.peerForSeat(2), h.link.handleOf(b.link));
  EXPECT_EQ(h.host.peerForSeat(3), h.link.handleOf(c.link));
  EXPECT_EQ(h.host.peerForSeat(4), NO_PEER);
  // The events kept their arrival-order seats: joined 2, joined 3, left 2, joined 3.
  const std::vector<HostEvent> events = h.events();
  ASSERT_EQ(events.size(), 4u);
  EXPECT_EQ(events[2].kind, HostEventKind::GuestLeft);
  EXPECT_EQ(events[2].seat, 2);
  EXPECT_EQ(events[3].seat, 3);
}

TEST(LobbyTest, StartNeedsAtLeastTheGamesMinimumSeats) {
  // seatsMin 3: two seats are not enough, three are.
  Rig rig2;
  FakeLink link(rig2.net);
  FixedRandom random({5});
  LobbyGame g = testGame(4);
  g.seatsMin = 3;
  HostLobby host(link, rig2.clock, random, g, bytesOf("h"));
  rig2.track(host);
  RawPeer a(rig2.net, rig2.clock);
  a.connect(link);
  ASSERT_TRUE(a.put(joinFrameFor(g), host.session()));
  rig2.advance(STEP_MS);
  EXPECT_EQ(host.start(), StartResult::NotEnoughSeats);
  RawPeer b(rig2.net, rig2.clock);
  b.connect(link);
  ASSERT_TRUE(b.put(joinFrameFor(g), host.session()));
  rig2.advance(STEP_MS);
  EXPECT_EQ(host.start(), StartResult::Started);
}

TEST(LobbyTest, HostCancelSendsAbortLeftThriceAndStopsAdvertising) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  h.host.cancel();
  h.run(STEP_MS);
  EXPECT_EQ(guest.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES});
  EXPECT_EQ(guest.countAborts(), size_t{CANCEL_ABORT_COPIES});
  EXPECT_EQ(h.host.state(), HostState::Closed);
  const size_t adverts = h.observer.count(Type::Advert);
  h.run(2000);
  EXPECT_EQ(h.observer.count(Type::Advert), adverts);
  EXPECT_EQ(h.host.start(), StartResult::NotWaiting);
  h.host.cancel();
  h.run(STEP_MS);
  EXPECT_EQ(guest.countAborts(), size_t{CANCEL_ABORT_COPIES}) << "a second cancel sends nothing more";
  // A JOIN to a closed host is answered ABORT(left).
  RawPeer& late = h.addPeer();
  ASSERT_TRUE(h.join(late));
  h.run(STEP_MS);
  EXPECT_EQ(late.countAborts(AbortReason::Left), 1u);
}

TEST(LobbyTest, ALateJoinToAStartedHostIsAnsweredFull) {
  HostRig h(3);
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.start(), StartResult::Started);
  RawPeer& late = h.addPeer();
  ASSERT_TRUE(h.join(late));
  h.run(STEP_MS);
  EXPECT_EQ(late.countAborts(AbortReason::Full), 1u) << "AD-13: a late JOIN gets ABORT(full), seats free or not";
  EXPECT_EQ(guest.countAborts(), 0u);
}

TEST(LobbyTest, GuestCancelSendsAbortLeftThrice) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  g.guest.cancel();
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.state(), GuestState::Cancelled);
  EXPECT_EQ(host.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES});
  EXPECT_EQ(host.countAborts(), size_t{CANCEL_ABORT_COPIES});
  // Forgotten: nothing more goes to the host, not even a keepalive.
  const size_t seen = host.seen.size();
  g.run(3000);
  EXPECT_EQ(host.seen.size(), seen);
  // Browsing cancels without sending.
  GuestRig b;
  b.guest.cancel();
  EXPECT_EQ(b.guest.state(), GuestState::Cancelled);
}

TEST(LobbyTest, GuestListsOnlyItsGameWithVerdictsLabelsAndSeats) {
  GuestRig g;
  RawPeer& same = g.addHost();
  RawPeer& otherGame = g.addHost();
  RawPeer& otherHash = g.addHost();
  RawPeer& otherSurface = g.addHost();
  g.advertise(same, 0x101, 1, bytesOf("same"));
  LobbyGame chess = testGame();
  chess.setGameId("chess");
  ASSERT_TRUE(otherGame.broadcast(advertFrameFor(chess, 1, 2, bytesOf("chess")), 0x102));
  LobbyGame hashed = testGame();
  hashed.pkgHash[2] ^= 0x10;
  ASSERT_TRUE(otherHash.broadcast(advertFrameFor(hashed, 2, 4, bytesOf("hash")), 0x103));
  LobbyGame surfaced = testGame();
  surfaced.surface ^= 0x100;
  ASSERT_TRUE(otherSurface.broadcast(advertFrameFor(surfaced, 1, 2, Bytes(16, 'z')), 0x104));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.hostCount(), 3u) << "the chess host is not listed";
  const HostEntry& a = g.guest.host(0);
  EXPECT_EQ(a.session, 0x101u);
  EXPECT_EQ(a.handle, g.link.handleOf(same.link));
  EXPECT_EQ(Bytes(a.label.begin(), a.label.begin() + a.labelLen), bytesOf("same"));
  EXPECT_EQ(a.seatsTaken, 1);
  EXPECT_EQ(a.seatsMax, 2);
  EXPECT_EQ(a.verdict, HostVerdict::Joinable);
  const HostEntry& b = g.guest.host(1);
  EXPECT_EQ(b.session, 0x103u);
  EXPECT_EQ(b.seatsTaken, 2);
  EXPECT_EQ(b.seatsMax, 4);
  EXPECT_EQ(b.verdict, HostVerdict::DifferentVersion);
  const HostEntry& c = g.guest.host(2);
  EXPECT_EQ(c.session, 0x104u);
  EXPECT_EQ(c.verdict, HostVerdict::DifferentVersion) << "this build's level is a preview";
  EXPECT_EQ(c.labelLen, 16);
  // A repeated advert refreshes the entry and does not reorder or duplicate it.
  ASSERT_TRUE(same.broadcast(advertFrameFor(g.game, 2, 2, bytesOf("same2")), 0x101));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.hostCount(), 3u);
  EXPECT_EQ(g.guest.host(0).seatsTaken, 2);
  EXPECT_EQ(g.guest.host(0).handle, g.link.handleOf(same.link));
}

TEST(LobbyTest, AHostOnAnotherProtoIsNeverListed) {
  GuestRig g;
  RawPeer& host = g.addHost();
  Bytes advert = RawPeer::encoded(advertFrameFor(g.game, 1, 2), HOST_SESSION, 0, 0);
  ASSERT_GT(advert.size(), 12u);
  ASSERT_EQ(advert[2], Protocol::PROTO);
  advert[2] = Protocol::PROTO + 1;
  ASSERT_TRUE(host.broadcastBytes(advert));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.hostCount(), 0u);
  EXPECT_EQ(g.guest.link().stats().badFrames, 1u);
  advert[2] = Protocol::PROTO;
  ASSERT_TRUE(host.broadcastBytes(advert));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.hostCount(), 1u) << "the same frame on this proto is listed";
}

TEST(LobbyTest, ListedHostsExpire) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.advertise(host, HOST_SESSION);
  ASSERT_EQ(g.guest.hostCount(), 1u);
  g.run(2900);
  EXPECT_EQ(g.guest.hostCount(), 1u);
  g.run(200);
  EXPECT_EQ(g.guest.hostCount(), 0u);
  // An advert refreshes the time.
  g.advertise(host, HOST_SESSION);
  g.run(2000);
  g.advertise(host, HOST_SESSION);
  g.run(2000);
  EXPECT_EQ(g.guest.hostCount(), 1u);
}

TEST(LobbyTest, ANewSessionForTheSameHandleReplacesTheEntry) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.advertise(host, 0x11);
  g.advertise(host, 0x22);
  ASSERT_EQ(g.guest.hostCount(), 1u);
  EXPECT_EQ(g.guest.host(0).session, 0x22u);
  EXPECT_EQ(g.guest.join(g.guest.host(0).handle, 0x11), JoinResult::UnknownHost) << "the old session is gone";
}

TEST(LobbyTest, TheTableDropsExtraHostsWhenFull) {
  GuestRig g;
  for (size_t i = 0; i < GuestLobby::MAX_HOSTS + 1; i++) {
    RawPeer& host = g.addHost();
    g.advertise(host, static_cast<uint32_t>(0x100 + i));
  }
  ASSERT_EQ(g.guest.hostCount(), GuestLobby::MAX_HOSTS);
  for (size_t i = 0; i < GuestLobby::MAX_HOSTS; i++) {
    EXPECT_EQ(g.guest.host(i).session, static_cast<uint32_t>(0x100 + i)) << "first heard first";
  }
  // Once the eight have expired, the ninth is listed on its next advert.
  g.run(3100);
  EXPECT_EQ(g.guest.hostCount(), 0u);
  g.advertise(g.hosts.back(), 0x108);
  ASSERT_EQ(g.guest.hostCount(), 1u);
  EXPECT_EQ(g.guest.host(0).session, 0x108u);
}

TEST(LobbyTest, JoinRefusesADifferentVersionWithoutSending) {
  GuestRig g;
  RawPeer& host = g.addHost();
  LobbyGame other = testGame();
  other.pkgHash[0] ^= 1;
  ASSERT_TRUE(host.broadcast(advertFrameFor(other, 1, 2), HOST_SESSION));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.hostCount(), 1u);
  const uint64_t carried = g.rig.net.carried();
  EXPECT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION), JoinResult::DifferentVersion);
  g.run(500);
  EXPECT_EQ(g.rig.net.carried(), carried);
  EXPECT_EQ(g.guest.state(), GuestState::Browsing);
  EXPECT_EQ(g.guest.session(), 0u);
}

TEST(LobbyTest, JoinOfAnUnknownHostIsRefused) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.advertise(host, HOST_SESSION);
  ASSERT_EQ(g.guest.hostCount(), 1u);
  const PeerHandle handle = g.guest.host(0).handle;
  EXPECT_EQ(g.guest.join(handle, HOST_SESSION + 1), JoinResult::UnknownHost);
  EXPECT_EQ(g.guest.join(NO_PEER, HOST_SESSION), JoinResult::UnknownHost);
  EXPECT_EQ(g.guest.join(PeerHandle::fromValue(0x7777), HOST_SESSION), JoinResult::UnknownHost);
  EXPECT_EQ(g.guest.state(), GuestState::Browsing);
  EXPECT_EQ(host.count(Type::Join), 0u);
}

TEST(LobbyTest, JoinWhenNotBrowsingIsRefused) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  const PeerHandle handle = g.guest.joinedHost().handle;
  EXPECT_EQ(g.guest.join(handle, HOST_SESSION), JoinResult::NotBrowsing);
  EXPECT_EQ(host.count(Type::Join), 1u);
}

TEST(LobbyTest, JoinSendsOneJoinCarryingTheGuestsVersionAndAResendKeepsItUntilAcked) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.advertise(host, HOST_SESSION);
  ASSERT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION), JoinResult::Sent);
  EXPECT_EQ(g.guest.state(), GuestState::Joining);
  EXPECT_EQ(g.guest.session(), HOST_SESSION);
  g.run(STEP_MS);
  ASSERT_EQ(host.count(Type::Join), 1u);
  EXPECT_EQ(host.of(Type::Join)[0].seq, 0);
  EXPECT_EQ(host.of(Type::Join)[0].session, HOST_SESSION);
  g.run(1000);
  EXPECT_GE(host.count(Type::Join), 3u) << "resent every 400 ms until acked";
  EXPECT_EQ(g.guest.state(), GuestState::Joining);
  EXPECT_EQ(g.guest.hostCount(), 0u) << "the list is dropped";
  ASSERT_TRUE(host.putAck(HOST_SESSION, 0));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.state(), GuestState::Joined);
  EXPECT_EQ(g.guest.joinedHost().session, HOST_SESSION);
}

TEST(LobbyTest, GuestReportsFullVersionMismatchHostLeftAndHostSilence) {
  struct Script {
    const char* name;
    AbortReason reason;
    GuestState state;
    Refusal refusal;
    GoneReason gone;
  };
  const Script scripts[] = {
      {"full", AbortReason::Full, GuestState::Refused, Refusal::Full, GoneReason::None},
      {"version", AbortReason::VersionMismatch, GuestState::Refused, Refusal::VersionMismatch, GoneReason::None},
      {"left", AbortReason::Left, GuestState::HostGone, Refusal::None, GoneReason::Left},
      {"script error", AbortReason::ScriptError, GuestState::HostGone, Refusal::None, GoneReason::Left},
      {"timeout", AbortReason::Timeout, GuestState::HostGone, Refusal::None, GoneReason::Silent},
  };
  for (const Script& s : scripts) {
    SCOPED_TRACE(s.name);
    GuestRig g;
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    ASSERT_TRUE(host.put(abortFrame(s.reason), HOST_SESSION));
    g.run(STEP_MS);
    EXPECT_EQ(g.guest.state(), s.state);
    EXPECT_EQ(g.guest.refusal(), s.refusal);
    EXPECT_EQ(g.guest.gone(), s.gone);
    // The link forgot the host: no keepalive goes to it any more.
    const size_t seen = host.seen.size();
    g.run(3000);
    EXPECT_EQ(host.seen.size(), seen);
    // Back to the list: empty, session 0, and a join sends a JOIN at seq 0 again.
    g.guest.backToList();
    EXPECT_EQ(g.guest.state(), GuestState::Browsing);
    EXPECT_EQ(g.guest.hostCount(), 0u);
    EXPECT_EQ(g.guest.session(), 0u);
    EXPECT_EQ(g.guest.refusal(), Refusal::None);
    EXPECT_EQ(g.guest.gone(), GoneReason::None);
    g.advertise(host, HOST_SESSION + 1);
    ASSERT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION + 1), JoinResult::Sent);
    g.run(STEP_MS);
    ASSERT_EQ(host.count(Type::Join), 2u);
    EXPECT_EQ(host.of(Type::Join)[1].seq, 0);
    EXPECT_EQ(host.of(Type::Join)[1].session, HOST_SESSION + 1);
  }
}

TEST(LobbyTest, BackToListOnlyWorksFromRefusedOrHostGone) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  g.guest.backToList();
  EXPECT_EQ(g.guest.state(), GuestState::Joined);
  g.guest.cancel();
  g.guest.backToList();
  EXPECT_EQ(g.guest.state(), GuestState::Cancelled);
}

TEST(LobbyTest, TheHostsSilenceEndsTheWaitAtTheLimit) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.advertise(host, HOST_SESSION);
  ASSERT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION), JoinResult::Sent);
  g.run(STEP_MS);
  ASSERT_TRUE(host.putAck(HOST_SESSION, 0));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.state(), GuestState::Joined);
  const uint64_t lastHeard = g.rig.now();
  g.run(9900);
  EXPECT_EQ(g.guest.state(), GuestState::Joined);
  g.run(200);
  EXPECT_EQ(g.guest.state(), GuestState::HostGone);
  EXPECT_EQ(g.guest.gone(), GoneReason::Silent);
  EXPECT_GE(g.rig.now(), lastHeard + 10000);
}

TEST(LobbyTest, GuestIgnoresFramesOfAnotherSession) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  const uint64_t bad = g.guest.link().stats().badFrames;
  const Bytes snapshot = {9};
  ASSERT_TRUE(host.put(abortFrame(AbortReason::Left), FOREIGN));
  ASSERT_TRUE(host.put(abortFrame(AbortReason::Full), FOREIGN));
  ASSERT_TRUE(host.put(acceptFrame(2, 2), FOREIGN));
  ASSERT_TRUE(host.put(stateFrame(snapshot), FOREIGN));
  ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), FOREIGN));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.state(), GuestState::Joined);
  EXPECT_EQ(g.guest.refusal(), Refusal::None);
  EXPECT_EQ(g.guest.gone(), GoneReason::None);
  EXPECT_EQ(g.guest.link().stats().badFrames, bad + 5);
}

TEST(LobbyTest, HostIgnoresFramesOfAnotherSession) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  const uint64_t bad = h.host.link().stats().badFrames;
  RawPeer& stranger = h.addPeer();
  ASSERT_TRUE(stranger.put(joinFrameFor(h.game), FOREIGN));
  ASSERT_TRUE(guest.put(abortFrame(AbortReason::Left), FOREIGN));
  ASSERT_TRUE(guest.put(joinFrameFor(h.game), FOREIGN, 1));
  ASSERT_TRUE(stranger.broadcast(advertFrameFor(h.game, 1, 2), FOREIGN));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.seatsTaken(), 2) << "the seat stays, and the stranger got none";
  EXPECT_EQ(stranger.countAborts(), 0u);
  EXPECT_EQ(h.host.link().stats().badFrames, bad + 4);
  EXPECT_EQ(h.events().size(), 1u);
}

// A host fault: the guest's wait is over (HostGone, Left), the host is forgotten, and a retry starts at seq 0.
void expectHostFaultEndsTheWait(GuestRig& g, RawPeer& host) {
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.state(), GuestState::HostGone);
  EXPECT_EQ(g.guest.gone(), GoneReason::Left);
  EXPECT_EQ(host.countAborts(AbortReason::Left), 3u) << "the host, already started, is told as a cancel tells it";
  const size_t seen = host.seen.size();
  g.run(3000);
  EXPECT_EQ(host.seen.size(), seen) << "the host is forgotten: not acked, no keepalive";
  g.guest.backToList();
  g.advertise(host, HOST_SESSION + 1);
  ASSERT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION + 1), JoinResult::Sent);
  g.run(STEP_MS);
  ASSERT_EQ(host.count(Type::Join), 2u);
  EXPECT_EQ(host.of(Type::Join)[1].seq, 0);
}

TEST(LobbyTest, AnAcceptOutsideTheRangeEndsTheWait) {
  // Seat 1 is the host's: a guest never takes it. Decode passes it, so the lobby judges it.
  {
    GuestRig g;
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    ASSERT_TRUE(host.put(acceptFrame(1, 2), HOST_SESSION, 0, 0));
    expectHostFaultEndsTheWait(g, host);
  }
  // Seat 1 of 1 is no match either.
  {
    GuestRig g;
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    ASSERT_TRUE(host.put(acceptFrame(1, 1), HOST_SESSION, 0, 0));
    expectHostFaultEndsTheWait(g, host);
  }
  // n above what the guest's game seats (here 2): the host is not speaking for this game.
  {
    GuestRig g;
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    ASSERT_TRUE(host.put(acceptFrame(2, 3), HOST_SESSION, 0, 0));
    expectHostFaultEndsTheWait(g, host);
  }
  // n below the game's fewest seats (here 3).
  {
    LobbyGame three = testGame(4);
    three.seatsMin = 3;
    GuestRig g(three);
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
    expectHostFaultEndsTheWait(g, host);
  }
  // A seat above n, and n above MAX_SEATS: Protocol::decode already refuses them (counted as a bad frame), so the
  // guest stays Joined. The frames are hand-patched past the encoder.
  for (const auto& [seat, n] : std::vector<std::pair<uint8_t, uint8_t>>{{3, 2}, {17, 17}, {2, 0}}) {
    GuestRig g;
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    Bytes bytes = RawPeer::encoded(acceptFrame(2, 2), HOST_SESSION, 0, 0);
    ASSERT_EQ(bytes.size(), Protocol::HEADER_BYTES + 2);
    bytes[Protocol::HEADER_BYTES] = seat;
    bytes[Protocol::HEADER_BYTES + 1] = n;
    const uint64_t bad = g.guest.link().stats().badFrames;
    ASSERT_TRUE(host.putBytes(bytes));
    g.run(STEP_MS);
    EXPECT_EQ(g.guest.state(), GuestState::Joined) << int{seat} << "/" << int{n};
    EXPECT_EQ(g.guest.link().stats().badFrames, bad + 1);
  }
  // A good one starts the match.
  GuestRig g(testGame(3));
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  ASSERT_TRUE(host.put(acceptFrame(3, 3), HOST_SESSION, 0, 0));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.state(), GuestState::Started);
  EXPECT_EQ(g.guest.seat(), 3);
  EXPECT_EQ(g.guest.roster().seats, 3);
  EXPECT_EQ(g.guest.roster().localSeats, 0b100);
  EXPECT_EQ(g.guest.roster().mode, Mode::Nearby);
  EXPECT_EQ(g.guest.roster().api, 1);
  EXPECT_EQ(g.guest.peerForSeat(1), g.link.handleOf(host.link));
  EXPECT_EQ(g.guest.peerForSeat(2), NO_PEER);
  EXPECT_EQ(g.guest.peerForSeat(3), NO_PEER);
}

TEST(LobbyTest, HandOffForwardsAfterStartAndLatchesBefore) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.state(), GuestState::Started);
  // A State before attach is NoRoom: not acked, so the host resends it.
  const Bytes snapshot = {4, 5, 6};
  ASSERT_TRUE(host.put(stateFrame(snapshot, 7), HOST_SESSION, 1, 0));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.link().stats().noRoom, 1u);
  const size_t acks = host.count(Type::Ack);
  RecSink sink(g.rig.clock);
  g.guest.handOff(sink);
  EXPECT_EQ(sink.frames.size(), 0u) << "nothing was latched yet";
  ASSERT_TRUE(host.put(stateFrame(snapshot, 7), HOST_SESSION, 1, 0));  // the resend
  g.run(STEP_MS);
  ASSERT_EQ(sink.count(Type::State), 1u);
  EXPECT_EQ(sink.frames[0].ver, 7);
  EXPECT_EQ(sink.frames[0].from, g.link.handleOf(host.link));
  EXPECT_GT(host.count(Type::Ack), acks) << "acked once taken";
  // After: an Abort from the host reaches the sink, one from another device does not.
  RawPeer& stranger = g.addHost();
  ASSERT_TRUE(stranger.put(abortFrame(AbortReason::Left), HOST_SESSION));
  g.run(STEP_MS);
  EXPECT_EQ(sink.count(Type::Abort), 0u);
  ASSERT_TRUE(host.put(abortFrame(AbortReason::ScriptError), HOST_SESSION));
  g.run(STEP_MS);
  ASSERT_EQ(sink.count(Type::Abort), 1u);
  EXPECT_EQ(sink.frames.back().reason, AbortReason::ScriptError);
}

TEST(LobbyTest, AnAbortAndASilenceBeforeAttachAreLatchedAndReplayedOnce) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.ended().kind, LobbyEnd::Kind::None);
  ASSERT_TRUE(host.put(abortFrame(AbortReason::Left), HOST_SESSION));
  ASSERT_TRUE(host.put(abortFrame(AbortReason::ScriptError), HOST_SESSION));  // the first wins
  g.run(STEP_MS);
  const LobbyEnd end = g.guest.ended();
  EXPECT_EQ(end.kind, LobbyEnd::Kind::Abort);
  EXPECT_EQ(end.reason, AbortReason::Left);
  EXPECT_EQ(end.peer, g.link.handleOf(host.link));
  RecSink sink(g.rig.clock);
  g.guest.handOff(sink);
  ASSERT_EQ(sink.frames.size(), 1u);
  EXPECT_EQ(sink.frames[0].type, Type::Abort);
  EXPECT_EQ(sink.frames[0].reason, AbortReason::Left);
  EXPECT_EQ(sink.frames[0].session, HOST_SESSION);
  EXPECT_EQ(sink.frames[0].from, g.link.handleOf(host.link));
  RecSink second(g.rig.clock);
  g.guest.handOff(second);
  EXPECT_EQ(second.frames.size() + sink.frames.size(), 1u) << "replayed once";
  EXPECT_EQ(g.guest.ended().kind, LobbyEnd::Kind::Abort) << "kept readable";

  // A silence before attach is latched the same way.
  GuestRig s;
  RawPeer& host2 = s.addHost();
  s.joinAndAck(host2, HOST_SESSION);
  ASSERT_TRUE(host2.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  s.run(10300);
  EXPECT_EQ(s.guest.ended().kind, LobbyEnd::Kind::Silent);
  RecSink silent(s.rig.clock);
  s.guest.handOff(silent);
  ASSERT_EQ(silent.silent.size(), 1u);
  EXPECT_EQ(silent.silent[0], s.link.handleOf(host2.link));
}

TEST(LobbyTest, TheHostsHandOffLatchesAndForwardsToo) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.start(), StartResult::Started);
  h.run(STEP_MS);
  const Bytes snapshot = {1};
  ASSERT_TRUE(guest.put(stateFrame(snapshot, 3), h.session(), 1, 0));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.link().stats().noRoom, 1u) << "a State before attach is not taken";
  RecSink sink(h.rig.clock);
  h.host.handOff(sink);
  ASSERT_TRUE(guest.put(stateFrame(snapshot, 3), h.session(), 1, 0));
  h.run(STEP_MS);
  ASSERT_EQ(sink.count(Type::State), 1u);
  EXPECT_EQ(sink.frames[0].ver, 3);
  // The guest leaves: the Abort reaches the sink; a stranger's does not.
  RawPeer& stranger = h.addPeer();
  ASSERT_TRUE(stranger.put(abortFrame(AbortReason::Left), h.session()));
  h.run(STEP_MS);
  EXPECT_EQ(sink.count(Type::Abort), 0u);
  ASSERT_TRUE(guest.put(abortFrame(AbortReason::Left), h.session()));
  h.run(STEP_MS);
  EXPECT_EQ(sink.count(Type::Abort), 1u);
  EXPECT_EQ(h.host.seatsTaken(), 2) << "a started roster does not shrink";
}

TEST(LobbyTest, TheHostLatchesAGuestsAbortBeforeAttach) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  ASSERT_EQ(h.host.start(), StartResult::Started);
  ASSERT_TRUE(guest.put(abortFrame(AbortReason::Left), h.session()));
  h.run(STEP_MS);
  EXPECT_EQ(h.host.ended().kind, LobbyEnd::Kind::Abort);
  RecSink sink(h.rig.clock);
  h.host.handOff(sink);
  ASSERT_EQ(sink.frames.size(), 1u);
  EXPECT_EQ(sink.frames[0].reason, AbortReason::Left);
  EXPECT_EQ(sink.frames[0].session, h.session());
}

TEST(LobbyTest, ALobbyWithoutSeatsForTheGameIsNotOk) {
  Rig rig;
  FakeLink link(rig.net);
  FixedRandom random({1});
  LobbyGame one = testGame();
  one.seatsMax = 1;
  HostLobby host(link, rig.clock, random, one, bytesOf("h"));
  EXPECT_FALSE(host.ok());
  EXPECT_EQ(host.start(), StartResult::NotWaiting);
  rig.track(host);
  RawPeer obs(rig.net, rig.clock);
  rig.advance(1000, [&] { obs.drain(); });
  EXPECT_EQ(obs.count(Type::Advert), 0u);
  GuestLobby guest(link, rig.clock, testGame());
  EXPECT_TRUE(guest.ok());
  // A game whose fewest seats exceed what this host seats could never start: not ok either.
  LobbyGame tooMany = testGame();
  tooMany.seatsMin = 3;
  FakeLink link2(rig.net);
  HostLobby host2(link2, rig.clock, random, tooMany, bytesOf("h"));
  EXPECT_FALSE(host2.ok());
  EXPECT_EQ(host2.start(), StartResult::NotWaiting);
  LobbyGame fits = testGame(3);
  fits.seatsMin = 3;
  FakeLink link3(rig.net);
  HostLobby host3(link3, rig.clock, random, fits, bytesOf("h"));
  EXPECT_TRUE(host3.ok());
}

TEST(LobbyTest, ACancelledHostAnswersAResentJoinAbortLeftAndHoldsNoSlot) {
  HostRig h;
  RawPeer& guest = h.addPeer();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  h.host.cancel();
  h.run(STEP_MS);
  ASSERT_EQ(guest.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES});
  // A guest that lost every ABORT copy resends its JOIN at seq 0.
  const size_t seen = guest.seen.size();
  ASSERT_TRUE(h.join(guest));
  h.run(STEP_MS);
  EXPECT_EQ(guest.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES} + 1);
  EXPECT_EQ(guest.countAborts(AbortReason::Full), 0u);
  h.run(3000);
  EXPECT_EQ(guest.seen.size(), seen + 1) << "only the ABORT: no ACK, no keepalive";
  EXPECT_EQ(h.host.link().stats().peerTableFull, 0u);
  EXPECT_EQ(h.host.state(), HostState::Closed);
}

TEST(LobbyTest, AStateAheadOfTheAcceptIsNotAckedAndTheAcceptStillStarts) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  const Bytes snapshot = {1, 2};
  ASSERT_TRUE(host.put(stateFrame(snapshot, 1), HOST_SESSION, 1, 0));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.link().stats().noRoom, 1u);
  EXPECT_EQ(host.count(Type::Ack), 0u) << "not acked";
  EXPECT_EQ(g.guest.state(), GuestState::Joined);
  ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.state(), GuestState::Started);
}

TEST(LobbyTest, ASteppedBackClockFiresNothingEarly) {
  // The guest's list keeps a host heard "in the future".
  {
    GuestRig g;
    RawPeer& host = g.addHost();
    g.run(2000);
    g.advertise(host, HOST_SESSION);
    g.run(1500);
    g.rig.clock.set(g.rig.now() - 1200);  // before the advert was heard
    g.run(500);
    EXPECT_EQ(g.guest.hostCount(), 1u);
  }
  // The host sends no extra ADVERT.
  {
    HostRig h;
    h.run(1100);
    ASSERT_EQ(h.observer.count(Type::Advert), 3u);  // 10, 510, 1010
    h.rig.clock.set(h.rig.now() - 600);
    h.run(300);
    EXPECT_EQ(h.observer.count(Type::Advert), 3u);
  }
  // A started guest forges no ABORT from a same-session ADVERT.
  {
    GuestRig g;
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    g.run(1000);
    ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
    g.run(STEP_MS);
    ASSERT_EQ(g.guest.state(), GuestState::Started);
    RecSink sink(g.rig.clock);
    g.guest.handOff(sink);
    g.rig.clock.set(g.rig.now() - 500);
    ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), HOST_SESSION));
    g.run(STEP_MS);
    EXPECT_EQ(sink.count(Type::Abort), 0u);
  }
  // The refusal limiter does not fire early.
  {
    HostRig h;
    RawPeer& first = h.addPeer();
    ASSERT_TRUE(h.join(first));
    h.run(STEP_MS);
    RawPeer& stranger = h.addPeer();
    ASSERT_TRUE(h.join(stranger));
    h.run(STEP_MS);
    ASSERT_EQ(stranger.countAborts(), 1u);
    h.run(100);
    h.rig.clock.set(h.rig.now() - 80);
    ASSERT_TRUE(h.join(stranger));
    h.run(STEP_MS);
    EXPECT_EQ(stranger.countAborts(), 1u);
  }
}

TEST(LobbyTest, ARefusedFirstTransmissionOfAJoinOrAnAcceptIsResentNotFailed) {
  // ReliableLink::send says true for a reliable frame it queued, whether or not the first transmission went out
  // (ILink.h: a false send is an ordinary loss). So a lobby's join() and start() succeed and the resend delivers.
  // JoinResult::LinkRefused and StartResult::LinkFailed are reachable only when ReliableLink cannot queue at all.
  {
    GuestRig g;
    RawPeer& host = g.addHost();
    g.advertise(host, HOST_SESSION);
    g.link.refuseSends(1);
    EXPECT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION), JoinResult::Sent);
    EXPECT_EQ(g.guest.state(), GuestState::Joining);
    g.run(STEP_MS);
    EXPECT_EQ(host.count(Type::Join), 0u);
    EXPECT_EQ(g.rig.net.refusedSends(), 1u);
    g.run(500);
    EXPECT_EQ(host.count(Type::Join), 1u) << "resent after resendMs";
    EXPECT_EQ(g.guest.session(), HOST_SESSION);
    // The guest keeps hearing hosts' frames: an ACK completes the join.
    ASSERT_TRUE(host.putAck(HOST_SESSION, 0));
    g.run(STEP_MS);
    EXPECT_EQ(g.guest.state(), GuestState::Joined);
  }
  {
    HostRig h;
    RawPeer& guest = h.addPeer();
    ASSERT_TRUE(h.join(guest));
    h.run(STEP_MS);
    h.link.refuseSends(1);
    EXPECT_EQ(h.host.start(), StartResult::Started);
    EXPECT_EQ(h.host.state(), HostState::Started);
    h.run(STEP_MS);
    EXPECT_EQ(guest.count(Type::Accept), 0u);
    h.run(500);
    EXPECT_EQ(guest.count(Type::Accept), 1u);
  }
}

TEST(LobbyTest, TheEventQueueKeepsTheNewestEightInOrderAcrossAWrappedRing) {
  HostRig h(4);
  RawPeer& p0 = h.addPeer();
  RawPeer& p1 = h.addPeer();
  RawPeer& p2 = h.addPeer();
  RawPeer* const peers[] = {&p0, &p1, &p2};
  std::vector<int> order;  // the seated peers, in arrival order
  std::vector<HostEvent> expected;
  auto joinPeer = [&](const int i) {
    ASSERT_TRUE(h.join(*peers[i]));
    h.run(STEP_MS);
    order.push_back(i);
    expected.push_back({HostEventKind::GuestJoined, static_cast<uint8_t>(order.size() + 1)});
  };
  auto leavePeer = [&](const int i) {
    ASSERT_TRUE(peers[i]->put(abortFrame(AbortReason::Left), h.session()));
    h.run(STEP_MS);
    size_t at = 0;
    while (order[at] != i) at++;
    expected.push_back({HostEventKind::GuestLeft, static_cast<uint8_t>(at + 2)});
    order.erase(order.begin() + static_cast<std::ptrdiff_t>(at));
  };
  joinPeer(0);
  joinPeer(1);
  joinPeer(2);
  leavePeer(1);
  joinPeer(1);
  leavePeer(0);
  joinPeer(0);
  leavePeer(2);
  joinPeer(2);
  leavePeer(1);
  joinPeer(1);
  leavePeer(0);
  ASSERT_GE(expected.size(), 12u);
  const std::vector<HostEvent> got = h.events();
  ASSERT_EQ(got.size(), HostLobby::EVENT_CAPACITY);
  const size_t first = expected.size() - HostLobby::EVENT_CAPACITY;
  for (size_t i = 0; i < got.size(); i++) {
    EXPECT_EQ(got[i].kind, expected[first + i].kind) << i;
    EXPECT_EQ(got[i].seat, expected[first + i].seat) << i;
  }
  HostEvent none;
  EXPECT_FALSE(h.host.poll(none));
  // And the ring keeps working once drained.
  joinPeer(0);
  const std::vector<HostEvent> again = h.events();
  ASSERT_EQ(again.size(), 1u);
  EXPECT_EQ(again[0].kind, HostEventKind::GuestJoined);
}

TEST(LobbyTest, ACancelFromJoiningStartedOrAHostsStartedSendsThreeAbortsLeft) {
  {  // guest, Joining
    GuestRig g;
    RawPeer& host = g.addHost();
    g.advertise(host, HOST_SESSION);
    ASSERT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION), JoinResult::Sent);
    g.run(STEP_MS);
    ASSERT_EQ(g.guest.state(), GuestState::Joining);
    g.guest.cancel();
    g.run(STEP_MS);
    EXPECT_EQ(g.guest.state(), GuestState::Cancelled);
    EXPECT_EQ(host.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES});
  }
  {  // guest, Started
    GuestRig g;
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
    g.run(STEP_MS);
    ASSERT_EQ(g.guest.state(), GuestState::Started);
    g.guest.cancel();
    g.run(STEP_MS);
    EXPECT_EQ(g.guest.state(), GuestState::Cancelled);
    EXPECT_EQ(host.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES});
  }
  {  // host, Started
    HostRig h(3);
    RawPeer& a = h.addPeer();
    RawPeer& b = h.addPeer();
    ASSERT_TRUE(h.join(a));
    h.run(STEP_MS);
    ASSERT_TRUE(h.join(b));
    h.run(STEP_MS);
    ASSERT_EQ(h.host.start(), StartResult::Started);
    h.host.cancel();
    h.run(STEP_MS);
    EXPECT_EQ(h.host.state(), HostState::Closed);
    EXPECT_EQ(a.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES});
    EXPECT_EQ(b.countAborts(AbortReason::Left), size_t{CANCEL_ABORT_COPIES});
  }
}

// Each LobbySettings field changes the behaviour it names, with bounds derived from the settings.
LobbySettings fastSettings() {
  LobbySettings s;
  s.advertMs = 100;
  s.hostExpireMs = 1000;
  s.advertGraceMs = 300;
  s.refusalAnswerMs = 50;
  s.link.resendMs = 100;
  s.link.silenceMs = 2000;
  s.link.keepaliveMs = 200;
  return s;
}

TEST(LobbyTest, EveryLobbySettingChangesTheBehaviourItNames) {
  const LobbySettings s = fastSettings();
  {  // advertMs: one ADVERT every 100 ms (the first on the first tick)
    HostRig h(2, bytesOf("h"), s);
    h.run(1000);
    EXPECT_EQ(h.observer.count(Type::Advert), 1000u / s.advertMs);
  }
  {  // link.resendMs: an unacked ACCEPT is resent every 100 ms
    HostRig h(2, bytesOf("h"), s);
    RawPeer& guest = h.addPeer();
    ASSERT_TRUE(h.join(guest));
    h.run(STEP_MS);
    ASSERT_EQ(h.host.start(), StartResult::Started);
    h.run(1000);
    const size_t n = guest.count(Type::Accept);
    EXPECT_GE(n, 1000u / s.link.resendMs - 1);
    EXPECT_LE(n, 1000u / s.link.resendMs + 2);
  }
  {  // link.silenceMs: a silent guest's seat is freed at 2 s, not before
    HostRig h(2, bytesOf("h"), s);
    RawPeer& guest = h.addPeer();
    ASSERT_TRUE(h.join(guest));
    h.run(STEP_MS);
    h.run(s.link.silenceMs - 2 * STEP_MS);
    EXPECT_EQ(h.host.seatsTaken(), 2);
    h.run(3 * STEP_MS);
    EXPECT_EQ(h.host.seatsTaken(), 1);
  }
  {  // refusalAnswerMs: a refused peer is answered again after 50 ms, not before
    HostRig h(2, bytesOf("h"), s);
    RawPeer& first = h.addPeer();
    ASSERT_TRUE(h.join(first));
    h.run(STEP_MS);
    RawPeer& stranger = h.addPeer();
    ASSERT_TRUE(h.join(stranger));
    h.run(STEP_MS);
    ASSERT_EQ(stranger.countAborts(), 1u);
    h.run(s.refusalAnswerMs / 2);
    ASSERT_TRUE(h.join(stranger));
    h.run(STEP_MS);
    EXPECT_EQ(stranger.countAborts(), 1u);
    h.run(s.refusalAnswerMs);
    ASSERT_TRUE(h.join(stranger));
    h.run(STEP_MS);
    EXPECT_EQ(stranger.countAborts(), 2u);
  }
  {  // hostExpireMs: a host not heard for 1 s leaves the list
    GuestRig g(testGame(), s);
    RawPeer& host = g.addHost();
    g.advertise(host, HOST_SESSION);
    g.run(s.hostExpireMs - 3 * STEP_MS);
    EXPECT_EQ(g.guest.hostCount(), 1u);
    g.run(4 * STEP_MS);
    EXPECT_EQ(g.guest.hostCount(), 0u);
  }
  {  // link.resendMs on the guest: an unacked JOIN is resent every 100 ms
    GuestRig g(testGame(), s);
    RawPeer& host = g.addHost();
    g.advertise(host, HOST_SESSION);
    ASSERT_EQ(g.guest.join(g.guest.host(0).handle, HOST_SESSION), JoinResult::Sent);
    g.run(1000);
    const size_t n = host.count(Type::Join);
    EXPECT_GE(n, 1000u / s.link.resendMs - 1);
    EXPECT_LE(n, 1000u / s.link.resendMs + 2);
  }
  {  // link.keepaliveMs: a joined guest pings every 200 ms
    GuestRig g(testGame(), s);
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    const size_t before = host.count(Type::Ping) + host.count(Type::Ack);
    g.run(1000);
    const size_t n = host.count(Type::Ping) + host.count(Type::Ack) - before;
    EXPECT_GE(n, 1000u / s.link.keepaliveMs - 1);
    EXPECT_LE(n, 1000u / s.link.keepaliveMs + 1);
  }
  {  // link.silenceMs on the guest: a host that says nothing is gone at 2 s
    GuestRig g(testGame(), s);
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    g.run(s.link.silenceMs - 3 * STEP_MS);
    EXPECT_EQ(g.guest.state(), GuestState::Joined);
    g.run(4 * STEP_MS);
    EXPECT_EQ(g.guest.state(), GuestState::HostGone);
  }
  {  // advertGraceMs: an ADVERT 200 ms after ACCEPT is a late copy, one after 300 ms ends the match
    GuestRig g(testGame(), s);
    RawPeer& host = g.addHost();
    g.joinAndAck(host, HOST_SESSION);
    ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
    g.run(STEP_MS);
    ASSERT_EQ(g.guest.state(), GuestState::Started);
    RecSink sink(g.rig.clock);
    g.guest.handOff(sink);
    g.run(s.advertGraceMs - 100);
    ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), HOST_SESSION));
    g.run(STEP_MS);
    EXPECT_EQ(sink.count(Type::Abort), 0u);
    g.run(2 * STEP_MS + 100);
    ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), HOST_SESSION));
    g.run(STEP_MS);
    EXPECT_EQ(sink.count(Type::Abort), 1u);
  }
}

// Follow-up review: a waiting guest only hears an ABORT from the host it joined.
TEST(LobbyTest, AnAbortFromAnotherDeviceDoesNotEndAWaitingGuest) {
  GuestRig g;
  RawPeer& host = g.addHost();
  RawPeer& other = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  // The ReliableLink delivers an ABORT of the lobby's session from any device; the guest ignores all but its host's.
  ASSERT_TRUE(other.put(abortFrame(AbortReason::Full), HOST_SESSION));
  ASSERT_TRUE(other.put(abortFrame(AbortReason::Left), HOST_SESSION));
  ASSERT_TRUE(other.put(abortFrame(AbortReason::VersionMismatch), HOST_SESSION));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.state(), GuestState::Joined);
  EXPECT_EQ(g.guest.refusal(), Refusal::None);
  EXPECT_EQ(g.guest.gone(), GoneReason::None);
  ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  g.run(STEP_MS);
  EXPECT_EQ(g.guest.state(), GuestState::Started) << "the real host still starts it";
}

// Follow-up review: an expiry in the middle of the list keeps the survivors in order and hides the expired host.
TEST(LobbyTest, AMidListExpiryKeepsTheSurvivorsInOrder) {
  GuestRig g;
  RawPeer& a = g.addHost();
  RawPeer& b = g.addHost();
  RawPeer& c = g.addHost();
  g.advertise(a, 0xA1);
  g.run(1000);
  g.advertise(b, 0xB2);
  g.advertise(c, 0xC3);
  ASSERT_EQ(g.guest.hostCount(), 3u);
  const PeerHandle aHandle = g.guest.host(0).handle;
  g.run(2100);  // A is 3,100 ms old; B and C are 2,100 ms old
  ASSERT_EQ(g.guest.hostCount(), 2u);
  EXPECT_EQ(g.guest.host(0).session, 0xB2u);
  EXPECT_EQ(g.guest.host(1).session, 0xC3u);
  EXPECT_EQ(g.guest.join(aHandle, 0xA1), JoinResult::UnknownHost) << "the expired host is not joinable";
  EXPECT_EQ(g.guest.join(g.guest.host(1).handle, 0xC3), JoinResult::Sent);
}

// Follow-up review: the lobby's own end of the match (the host advertising again) is latched before the match attaches
// and replayed on attach, as an ABORT of the lobby's session.
TEST(LobbyTest, ALateAdvertBeforeAttachEndsTheMatchOnAttach) {
  GuestRig g;
  RawPeer& host = g.addHost();
  g.joinAndAck(host, HOST_SESSION);
  ASSERT_TRUE(host.put(acceptFrame(2, 2), HOST_SESSION, 0, 0));
  g.run(STEP_MS);
  ASSERT_EQ(g.guest.state(), GuestState::Started);
  g.run(1100);  // past advertGraceMs
  ASSERT_TRUE(host.broadcast(advertFrameFor(g.game, 1, 2), HOST_SESSION));
  g.run(STEP_MS);
  const LobbyEnd end = g.guest.ended();
  EXPECT_EQ(end.kind, LobbyEnd::Kind::Abort);
  EXPECT_EQ(end.reason, AbortReason::Left);
  EXPECT_EQ(end.peer, g.link.handleOf(host.link));
  RecSink sink(g.rig.clock);
  g.guest.handOff(sink);
  ASSERT_EQ(sink.frames.size(), 1u);
  EXPECT_EQ(sink.frames[0].type, Type::Abort);
  EXPECT_EQ(sink.frames[0].reason, AbortReason::Left);
  EXPECT_EQ(sink.frames[0].session, HOST_SESSION);
  EXPECT_EQ(sink.frames[0].from, g.link.handleOf(host.link));
}

// Follow-up review: the refusal limiter holds one answer per interval for each of several strangers whose JOIN resends
// interleave (the ring remembers the four most recent refused peers).
TEST(LobbyTest, RefusalAnswersAreRateLimitedPerStrangerWhenTheyInterleave) {
  HostRig h(2);
  RawPeer& seated = h.addPeer();
  ASSERT_TRUE(h.join(seated));
  h.run(STEP_MS);
  std::deque<RawPeer*> strangers;
  for (int i = 0; i < 4; i++) strangers.push_back(&h.addPeer());
  // Four rounds of interleaved JOINs, 40 ms apart, all inside one refusalAnswerMs of the first.
  for (int round = 0; round < 4; round++) {
    for (RawPeer* s : strangers) ASSERT_TRUE(h.join(*s));
    h.run(4 * STEP_MS);
  }
  for (RawPeer* s : strangers) EXPECT_EQ(s->countAborts(), 1u) << "one answer each inside the interval";
  h.run(200);
  for (RawPeer* s : strangers) ASSERT_TRUE(h.join(*s));
  h.run(STEP_MS);
  for (RawPeer* s : strangers) EXPECT_EQ(s->countAborts(), 2u) << "one more each after it";
}

}  // namespace
