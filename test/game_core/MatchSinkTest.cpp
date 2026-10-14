#include <gtest/gtest.h>

#include "Lobby.h"
#include "MatchSupport.h"

// MatchSink (plan 7, Design Note 4): its verdict for each frame and sender, the end slot it feeds, the NoRoom resend
// through a real ReliableLink, and the replay of an end the lobby latched before the match attached.

using namespace GameCore;
using namespace GameCore::MatchTest;

namespace {

using Verdict = ReliableLink::Verdict;
using Protocol::AbortReason;
using Protocol::Frame;
using Protocol::Type;

constexpr PeerHandle SEAT_ONE = PeerHandle::fromValue(0x101);
constexpr PeerHandle SEAT_THREE = PeerHandle::fromValue(0x103);
constexpr PeerHandle STRANGER = PeerHandle::fromValue(0x999);

Status playing(const uint8_t turn = 2) {
  Status s;
  s.turn = turn;
  return s;
}
Status overStatus() {
  Status s;
  s.over = true;
  s.winners = 1;
  return s;
}

Frame abortFrame(const AbortReason reason) {
  Frame f;
  f.header.type = Type::Abort;
  f.abortReason = reason;
  return f;
}

struct Rig {
  Rig() : queue(store), peers{SeatPeer{SEAT_ONE, 1}, SeatPeer{SEAT_THREE, 3}}, sink(queue, end, peers) {}
  Verdict state(const PeerHandle from, const uint16_t ver, const Status& status, const Bytes& snapshot) {
    return sink.onFrame(from, stateFrameOf(ver, status, snapshot));
  }
  ArrayStateStore store;
  StateQueue queue;
  EndSlot end;
  std::array<SeatPeer, 2> peers;
  MatchSink sink;
};

TEST(MatchSinkTest, AStateFromAPeerIsQueuedWithTheSeatOfItsLinkIdentity) {
  Rig rig;
  EXPECT_EQ(rig.state(SEAT_ONE, 1, playing(), Bytes{1, 2, 3}), Verdict::Taken);
  EXPECT_EQ(rig.state(SEAT_THREE, 2, playing(), Bytes{4}), Verdict::Taken);
  EXPECT_EQ(rig.sink.stats().queued, 2u);
  QueuedState q;
  ASSERT_TRUE(rig.queue.take(q));
  EXPECT_EQ(q.fromSeat, 1);
  EXPECT_EQ(q.ver, 1);
  EXPECT_EQ(rig.queue.bytes(q).size(), 3u);
  rig.queue.release(q);
  ASSERT_TRUE(rig.queue.take(q));
  EXPECT_EQ(q.fromSeat, 3);
  rig.queue.release(q);
}

TEST(MatchSinkTest, ANewerStateReplacesTheNewestOneAndIsStillTaken) {
  Rig rig;
  EXPECT_EQ(rig.state(SEAT_ONE, 1, playing(), Bytes{1}), Verdict::Taken);
  EXPECT_EQ(rig.state(SEAT_ONE, 2, playing(), Bytes{2}), Verdict::Taken);
  EXPECT_EQ(rig.sink.stats().queued, 1u);
  EXPECT_EQ(rig.sink.stats().replaced, 1u);
  EXPECT_EQ(rig.queue.size(), 1u);
}

TEST(MatchSinkTest, AFullQueueAnswersNoRoomSoTheLinkDoesNotAck) {
  Rig rig;
  for (uint16_t v = 1; v <= StateQueue::DEPTH; ++v) {
    ASSERT_EQ(rig.state(SEAT_ONE, v, overStatus(), Bytes{static_cast<uint8_t>(v)}), Verdict::Taken);
  }
  EXPECT_EQ(rig.state(SEAT_ONE, 5, playing(), Bytes{5}), Verdict::NoRoom);
  EXPECT_EQ(rig.queue.overflows(), 1u);
  EXPECT_EQ(rig.queue.size(), StateQueue::DEPTH);
}

TEST(MatchSinkTest, AStateTheQueueCannotHoldIsRefusedNotRetried) {
  Rig rig;
  EXPECT_EQ(rig.state(SEAT_ONE, 1, playing(), Bytes{}), Verdict::Refused);
  EXPECT_EQ(rig.state(SEAT_ONE, 1, playing(), Bytes(Protocol::MAX_SNAPSHOT_BYTES + 1, 0)), Verdict::Refused);
  EXPECT_EQ(rig.queue.size(), 0u);
}

TEST(MatchSinkTest, AStrangersFramesAreRefusedCountedAndReachNothing) {
  Rig rig;
  EXPECT_EQ(rig.state(STRANGER, 1, playing(), Bytes{1}), Verdict::Refused);
  EXPECT_EQ(rig.sink.onFrame(STRANGER, abortFrame(AbortReason::Left)), Verdict::Refused);
  EXPECT_EQ(rig.sink.onFrame(NO_PEER, abortFrame(AbortReason::Left)), Verdict::Refused);
  EXPECT_EQ(rig.sink.stats().strangers, 3u);
  EXPECT_EQ(rig.queue.size(), 0u);
  EXPECT_EQ(rig.store.acquired, 0u);
  EXPECT_FALSE(rig.end.latched());
  EXPECT_EQ(rig.sink.seatOf(STRANGER), 0);
  EXPECT_EQ(rig.sink.seatOf(NO_PEER), 0);
  EXPECT_EQ(rig.sink.seatOf(SEAT_ONE), 1);
  EXPECT_EQ(rig.sink.seatOf(SEAT_THREE), 3);
}

TEST(MatchSinkTest, TheLobbysFramesAreNotTheMatchs) {
  Rig rig;
  Frame join;
  join.header.type = Type::Join;
  Frame accept;
  accept.header.type = Type::Accept;
  Frame advert;
  advert.header.type = Type::Advert;
  EXPECT_EQ(rig.sink.onFrame(SEAT_ONE, join), Verdict::Refused);
  EXPECT_EQ(rig.sink.onFrame(SEAT_ONE, accept), Verdict::Refused);
  EXPECT_EQ(rig.sink.onFrame(SEAT_ONE, advert), Verdict::Refused);
  EXPECT_EQ(rig.queue.size(), 0u);
  EXPECT_FALSE(rig.end.latched());
}

TEST(MatchSinkTest, AnAbortOfAnyReasonFromAPeerLatchesTheFirstOnce) {
  for (const AbortReason r : {AbortReason::Left, AbortReason::ScriptError, AbortReason::Timeout, AbortReason::Full,
                              AbortReason::VersionMismatch}) {
    Rig rig;
    EXPECT_EQ(rig.sink.onFrame(SEAT_ONE, abortFrame(r)), Verdict::Taken);
    EXPECT_EQ(rig.sink.onFrame(SEAT_THREE, abortFrame(AbortReason::Left == r ? AbortReason::Full : AbortReason::Left)),
              Verdict::Taken);
    AbortReason got = AbortReason::Left;
    ASSERT_TRUE(rig.end.take(got));
    EXPECT_EQ(got, r) << "the first wins";
    EXPECT_FALSE(rig.end.take(got));
    EXPECT_EQ(rig.end.ignored(), 1u);
  }
}

TEST(MatchSinkTest, ASilentPeerLatchesTimeoutAndAStrangersSilenceIsIgnored) {
  Rig rig;
  rig.sink.onPeerSilent(STRANGER);
  EXPECT_FALSE(rig.end.latched());
  rig.sink.onPeerSilent(SEAT_THREE);
  AbortReason got = AbortReason::Left;
  ASSERT_TRUE(rig.end.take(got));
  EXPECT_EQ(got, AbortReason::Timeout);
  // An ABORT after the silence is the second reason: counted, ignored.
  EXPECT_EQ(rig.sink.onFrame(SEAT_ONE, abortFrame(AbortReason::Left)), Verdict::Taken);
  EXPECT_EQ(rig.end.ignored(), 1u);
  EXPECT_FALSE(rig.end.take(got));
}

TEST(MatchSinkTest, AtMostMaxSeatsPeersAreKept) {
  ArrayStateStore store;
  StateQueue queue(store);
  EndSlot end;
  std::array<SeatPeer, Roster::MAX_SEATS + 3> many{};
  for (size_t i = 0; i < many.size(); ++i)
    many[i] = SeatPeer{PeerHandle::fromValue(0x200 + i), static_cast<uint8_t>(i + 1)};
  MatchSink sink(queue, end, many);
  EXPECT_EQ(sink.seatOf(many[Roster::MAX_SEATS - 1].handle), Roster::MAX_SEATS);
  EXPECT_EQ(sink.seatOf(many[Roster::MAX_SEATS].handle), 0);
}

// A sender behind a real ReliableLink: its queue holds one STATE in flight and three waiting, so five over STATEs
// fill the receiver's four entries and leave the fifth unacked.
class NullSink final : public ReliableLink::Sink {
 public:
  Verdict onFrame(PeerHandle, const Frame&) override { return Verdict::Taken; }
  void onPeerSilent(PeerHandle) override {}
};

TEST(MatchSinkTest, ANoRoomStateIsResentAndDeliveredOnceRoomExistsWithNothingLost) {
  ManualClock clock;
  FakeLinkNet net(clock, 7);
  FakeLink senderLink(net), receiverLink(net);
  NullSink nullSink;
  ArrayStateStore store;
  StateQueue queue(store);
  EndSlot end;
  const std::array<SeatPeer, 1> peers{SeatPeer{receiverLink.handleOf(senderLink), 1}};
  MatchSink sink(queue, end, peers);
  ReliableLink sender(senderLink, clock, nullSink);
  ReliableLink receiver(receiverLink, clock, sink);
  sender.setSession(MATCH_SESSION);
  receiver.setSession(MATCH_SESSION);
  const PeerHandle toReceiver = senderLink.handleOf(receiverLink);

  auto step = [&](const uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
      clock.advance(1);
      sender.tick();
      receiver.tick();
    }
  };
  // Four over STATEs fill the queue; each is acked as it is taken.
  for (uint16_t v = 1; v <= 4; ++v) {
    const Bytes snap{static_cast<uint8_t>(v)};
    ASSERT_TRUE(sender.send(toReceiver, stateFrameOf(v, overStatus(), snap)));
  }
  step(50);
  ASSERT_EQ(queue.size(), StateQueue::DEPTH);
  ASSERT_EQ(sender.unacked(), 0u);
  // The fifth finds the queue full: not acked.
  const Bytes fifth{5};
  ASSERT_TRUE(sender.send(toReceiver, stateFrameOf(5, overStatus(), fifth)));
  step(100);
  EXPECT_EQ(sender.unacked(), 1u);
  EXPECT_GE(receiver.stats().noRoom, 1u);
  EXPECT_GE(queue.overflows(), 1u);
  EXPECT_EQ(queue.size(), StateQueue::DEPTH);

  // The consumer drains one entry; the resend (400 ms) delivers the fifth.
  QueuedState q;
  ASSERT_TRUE(queue.take(q));
  EXPECT_EQ(q.ver, 1);
  queue.release(q);
  step(ReliableLink::DEFAULT_RESEND_MS + 50);
  EXPECT_EQ(sender.unacked(), 0u);
  ASSERT_EQ(queue.size(), StateQueue::DEPTH);
  for (uint16_t v = 2; v <= 5; ++v) {
    ASSERT_TRUE(queue.take(q));
    EXPECT_EQ(q.ver, v) << "in order, each once";
    EXPECT_EQ(queue.bytes(q)[0], static_cast<uint8_t>(v));
    queue.release(q);
  }
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_EQ(store.inUse(), 0u);
  EXPECT_EQ(store.badReleases, 0u);
}

TEST(MatchSinkTest, AnEndTheLobbyLatchedBeforeTheMatchAttachedIsReplayedIntoTheEndSlot) {
  // An ABORT that arrived before attach.
  {
    Rig rig;
    LobbyHandOff handOff;
    EXPECT_EQ(handOff.forward(SEAT_ONE, abortFrame(AbortReason::ScriptError)), Verdict::Refused);
    // A STATE before attach is NoRoom: the peer resends it, and it reaches the queue after attach.
    EXPECT_EQ(handOff.forward(SEAT_ONE, stateFrameOf(1, playing(), Bytes{1})), Verdict::NoRoom);
    handOff.attach(rig.sink, MATCH_SESSION);
    AbortReason got = AbortReason::Left;
    ASSERT_TRUE(rig.end.take(got));
    EXPECT_EQ(got, AbortReason::ScriptError);
    EXPECT_EQ(handOff.forward(SEAT_ONE, stateFrameOf(1, playing(), Bytes{1})), Verdict::Taken);
    EXPECT_EQ(rig.queue.size(), 1u);
  }
  // A peer that went silent before attach.
  {
    Rig rig;
    LobbyHandOff handOff;
    handOff.forwardSilent(SEAT_ONE);
    handOff.attach(rig.sink, MATCH_SESSION);
    AbortReason got = AbortReason::Left;
    ASSERT_TRUE(rig.end.take(got));
    EXPECT_EQ(got, AbortReason::Timeout);
  }
  // After attach the hand-off forwards straight through.
  {
    Rig rig;
    LobbyHandOff handOff;
    handOff.attach(rig.sink, MATCH_SESSION);
    EXPECT_EQ(handOff.forward(SEAT_ONE, abortFrame(AbortReason::Left)), Verdict::Taken);
    handOff.forwardSilent(SEAT_ONE);
    EXPECT_EQ(rig.end.ignored(), 1u);
  }
}

}  // namespace
