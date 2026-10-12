#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <set>
#include <span>
#include <vector>

#include "FakeLink.h"
#include "Protocol.h"
#include "ReliableLink.h"

// GameCore::ReliableLink over the seeded hostile FakeLink: delivery, the resend clock, silence, seq wrap,
// replacement, the sink's verdicts, and the keepalive bound. Every bound is in fake milliseconds from the settings
// (a ManualClock the tests advance), never host speed. RawPeer stands in for a remote device speaking Protocol by
// hand, an oracle independent of ReliableLink; RecSink stands in for the lobby's and the match session's code and is
// more permissive than the match session's queue (unbounded recording, a scripted verdict instead of a 4-entry queue).

using namespace GameCore;
using namespace GameCore::LinkTest;

namespace {

using Bytes = std::vector<uint8_t>;
using Protocol::Frame;
using Protocol::Type;
using Verdict = ReliableLink::Verdict;
using Settings = ReliableLink::Settings;

constexpr uint32_t SESSION = 0xC0FFEE01u;
constexpr uint16_t NO_ACK = 0xFFFF;  // what a peer that has received nothing puts in `ack`

Bytes snapshotFor(const uint16_t ver) {
  Bytes b(1 + ver % 40u);
  for (size_t i = 0; i < b.size(); i++) b[i] = static_cast<uint8_t>(ver * 31u + i * 7u);
  return b;
}

Frame stateFrame(const Bytes& snapshot, const uint16_t ver, const bool over = false) {
  Frame f;
  f.header.type = Type::State;
  f.state.ver = ver;
  f.state.status.over = over;
  f.state.status.turn = over ? 0 : static_cast<uint8_t>(1 + ver % 2);
  f.state.status.winners = over ? 1 : 0;
  f.state.snapshot = snapshot;
  return f;
}

Frame joinFrame(const uint32_t tag) {
  Frame f;
  f.header.type = Type::Join;
  f.join.surface = tag;
  f.join.hostApi = 1;
  f.join.pkgHash[0] = 7;
  return f;
}

Frame acceptFrame(const uint8_t seat = 1, const uint8_t n = 2) {
  Frame f;
  f.header.type = Type::Accept;
  f.accept.seat = seat;
  f.accept.n = n;
  return f;
}

Frame abortFrame() {
  Frame f;
  f.header.type = Type::Abort;
  f.abortReason = Protocol::AbortReason::Left;
  return f;
}

Frame advertFrame() {
  Frame f;
  f.header.type = Type::Advert;
  f.advert.gameId = "game";
  f.advert.seatsMax = 2;
  f.advert.seatsTaken = 1;
  return f;
}

Frame plainFrame(const Type type) {
  Frame f;
  f.header.type = type;
  return f;
}

uint32_t keyOf(const Frame& f) {
  const uint32_t type = static_cast<uint32_t>(f.header.type) << 24;
  switch (f.header.type) {
    case Type::Join:
      return type | f.join.surface;
    case Type::Accept:
      return type | (static_cast<uint32_t>(f.accept.seat) << 8) | f.accept.n;
    case Type::State:
      return type | f.state.ver;
    case Type::Abort:
      return type | static_cast<uint32_t>(f.abortReason);
    default:
      return type;
  }
}

struct Rec {
  Type type = Type::Ping;
  uint32_t key = 0;
  bool over = false;
  PeerHandle from;
  uint64_t atMs = 0;
  Bytes snapshot;
};

// Records what the link hands the session. `script` gives the verdicts of the next reliable offers, then `fallback`.
class RecSink final : public ReliableLink::Sink {
 public:
  explicit RecSink(const ManualClock& c) : clock(c) {}

  Verdict onFrame(const PeerHandle from, const Frame& frame) override {
    Rec r;
    r.type = frame.header.type;
    r.key = keyOf(frame);
    r.from = from;
    r.atMs = clock.nowMs();
    if (frame.header.type == Type::State) {
      r.over = frame.state.status.over;
      r.snapshot.assign(frame.state.snapshot.begin(), frame.state.snapshot.end());
      if (r.snapshot != snapshotFor(frame.state.ver)) badContent = true;
    }
    Verdict verdict = Verdict::Taken;
    const bool reliable =
        frame.header.type == Type::Join || frame.header.type == Type::Accept || frame.header.type == Type::State;
    if (reliable) {
      if (!script.empty()) {
        verdict = script.front();
        script.pop_front();
      } else {
        verdict = fallback;
      }
      offered.push_back(r);
    }
    if (!reliable || verdict == Verdict::Taken) frames.push_back(r);
    if (hook) hook(from, frame);
    return verdict;
  }
  void onPeerSilent(const PeerHandle peer) override {
    silent.push_back(peer);
    silentAt.push_back(clock.nowMs());
    if (silentHook) silentHook(peer);
  }

  std::vector<uint32_t> keys() const {
    std::vector<uint32_t> out;
    for (const Rec& r : frames) out.push_back(r.key);
    return out;
  }
  size_t count(const Type type) const {
    size_t n = 0;
    for (const Rec& r : frames) n += r.type == type ? 1 : 0;
    return n;
  }

  const ManualClock& clock;
  std::vector<Rec> frames;   // delivered: Taken, and every unsequenced frame
  std::vector<Rec> offered;  // every reliable offer, whatever the verdict
  std::deque<Verdict> script;
  Verdict fallback = Verdict::Taken;
  std::vector<PeerHandle> silent;
  std::vector<uint64_t> silentAt;
  std::function<void(PeerHandle, const Frame&)> hook;
  std::function<void(PeerHandle)> silentHook;
  bool badContent = false;
};

struct Wire {
  Type type = Type::Ping;
  uint16_t seq = 0;
  uint16_t ack = 0;
  uint16_t ver = 0;
  bool over = false;
  uint64_t atMs = 0;
};

// A remote device that speaks Protocol by hand.
class RawPeer {
 public:
  RawPeer(FakeLinkNet& net, const ManualClock& c) : link(net), clock(c), buffer(Protocol::MAX_FRAME_BYTES) {}

  void connect(FakeLink& under) { toUnder = link.handleOf(under); }

  // Takes everything waiting into `seen`.
  void drain() {
    PeerHandle from;
    size_t length = 0;
    while (link.receive(from, buffer, length)) {
      Frame f;
      if (Protocol::decode(std::span<const uint8_t>(buffer.data(), length), 0, f) != Protocol::DecodeStatus::Ok) {
        undecodable++;
        continue;
      }
      Wire w;
      w.type = f.header.type;
      w.seq = f.header.seq;
      w.ack = f.header.ack;
      w.ver = f.state.ver;
      w.over = f.state.status.over;
      w.atMs = clock.nowMs();
      seen.push_back(w);
    }
  }
  std::vector<Wire> of(const Type type) const {
    std::vector<Wire> out;
    for (const Wire& w : seen) {
      if (w.type == type) out.push_back(w);
    }
    return out;
  }
  std::vector<uint64_t> timesOf(const Type type) const {
    std::vector<uint64_t> out;
    for (const Wire& w : seen) {
      if (w.type == type) out.push_back(w.atMs);
    }
    return out;
  }
  bool put(Frame f, const uint16_t seq, const uint16_t ack, const uint32_t session = SESSION) {
    f.header.session = session;
    f.header.seq = seq;
    f.header.ack = ack;
    Bytes out(Protocol::MAX_FRAME_BYTES);
    const size_t n = Protocol::encode(out, f);
    if (n == 0) return false;
    return link.send(toUnder, std::span<const uint8_t>(out.data(), n));
  }
  bool putType(const Type type, const uint16_t seq, const uint16_t ack) { return put(plainFrame(type), seq, ack); }
  bool putAck(const uint16_t ack) { return putType(Type::Ack, 0, ack); }

  FakeLink link;
  PeerHandle toUnder;
  const ManualClock& clock;
  Bytes buffer;
  std::vector<Wire> seen;
  size_t undecodable = 0;
};

// The link under test and one raw peer.
struct Solo {
  explicit Solo(const Settings& settings = {}, const uint32_t seed = 1)
      : net(clock, seed), under(net), raw(net, clock), sink(clock), rl(under, clock, sink, settings) {
    rl.setSession(SESSION);
    raw.connect(under);
    toRaw = under.handleOf(raw.link);
  }
  void step(const uint32_t ms = 1) {
    for (uint32_t i = 0; i < ms; i++) {
      clock.advance(1);
      rl.tick();
      raw.drain();
    }
  }
  // Puts one frame to the link and lets it run one tick.
  void deliver(const Frame& f, const uint16_t seq, const uint16_t ack) {
    ASSERT_TRUE(raw.put(f, seq, ack));
    rl.tick();
    raw.drain();
  }

  ManualClock clock;
  FakeLinkNet net;
  FakeLink under;
  RawPeer raw;
  RecSink sink;
  ReliableLink rl;
  PeerHandle toRaw;
};

// Two linked devices.
struct Duo {
  explicit Duo(const uint32_t seed = 1, const Settings& settings = {})
      : net(clock, seed),
        la(net),
        lb(net),
        sa(clock),
        sb(clock),
        a(la, clock, sa, settings),
        b(lb, clock, sb, settings) {
    a.setSession(SESSION);
    b.setSession(SESSION);
    toB = la.handleOf(lb);
    toA = lb.handleOf(la);
  }
  void step(const uint32_t ms = 1) {
    for (uint32_t i = 0; i < ms; i++) {
      clock.advance(1);
      a.tick();
      b.tick();
    }
  }
  void blockAToB(const bool on) {
    Faults f;
    f.lossPercent = on ? 100 : 0;
    net.setFaults(la.index(), lb.index(), f);
  }

  ManualClock clock;
  FakeLinkNet net;
  FakeLink la;
  FakeLink lb;
  RecSink sa;
  RecSink sb;
  ReliableLink a;
  ReliableLink b;
  PeerHandle toB;
  PeerHandle toA;
};

Faults hostile() {
  Faults f;
  f.lossPercent = 30;
  f.duplicatePercent = 20;
  f.reorderPercent = 30;
  f.delayMs = 20;
  f.jitterMs = 30;
  f.reorderExtraMs = 100;
  return f;
}

struct Rng {
  explicit Rng(const uint32_t seed) : state(seed * 2654435761u + 1u) {}
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  uint32_t below(const uint32_t n) { return next() % n; }
  uint32_t state;
};

struct Item {
  Type type = Type::State;
  uint16_t tag = 0;  // a State's ver, a Join's surface
  bool over = false;
};

uint32_t keyOfItem(const Item& it) {
  Frame f;
  f.header.type = it.type;
  f.join.surface = it.tag;
  f.accept.seat = 1;
  f.accept.n = 2;
  f.state.ver = it.tag;
  return keyOf(f);
}

bool sendItem(ReliableLink& link, const PeerHandle to, const Item& it) {
  switch (it.type) {
    case Type::Join:
      return link.send(to, joinFrame(it.tag));
    case Type::Accept:
      return link.send(to, acceptFrame());
    default: {
      const Bytes snap = snapshotFor(it.tag);
      return link.send(to, stateFrame(snap, it.tag, it.over));
    }
  }
}

// `count` States (about one in seven ends the round) with one Join and one Accept among the first few.
std::vector<Item> makeItems(Rng& rng, const size_t count) {
  std::vector<Item> items;
  for (size_t i = 0; i < count; i++) {
    Item it;
    it.tag = static_cast<uint16_t>(i + 1);
    it.over = rng.below(7) == 0;
    items.push_back(it);
  }
  Item join;
  join.type = Type::Join;
  join.tag = 0xA001;
  Item accept;
  accept.type = Type::Accept;
  items.insert(items.begin() + static_cast<std::ptrdiff_t>(rng.below(3)), join);
  items.insert(items.begin() + static_cast<std::ptrdiff_t>(rng.below(4)), accept);
  return items;
}

bool isSubsequence(const std::vector<uint32_t>& part, const std::vector<uint32_t>& whole) {
  size_t i = 0;
  for (const uint32_t k : whole) {
    if (i < part.size() && part[i] == k) i++;
  }
  return i == part.size();
}

TEST(ReliableLinkTest, DefaultsAre400And10000AndAKeepalive) {
  const Settings s;
  EXPECT_EQ(s.resendMs, 400u);
  EXPECT_EQ(s.silenceMs, 10000u);
  EXPECT_EQ(s.keepaliveMs, 1000u);
  EXPECT_EQ(s.resendMs, ReliableLink::DEFAULT_RESEND_MS);
  EXPECT_EQ(s.silenceMs, ReliableLink::DEFAULT_SILENCE_MS);
  EXPECT_EQ(s.keepaliveMs, ReliableLink::DEFAULT_KEEPALIVE_MS);
  EXPECT_EQ(s.maxPeers, ReliableLink::DEFAULT_MAX_PEERS);
  EXPECT_LT(s.keepaliveMs, s.silenceMs);
  Duo d;
  EXPECT_TRUE(d.a.ok());
  EXPECT_EQ(d.a.session(), SESSION);
}

TEST(ReliableLinkTest, EveryFrameSentOneAtATimeIsDeliveredOnceInOrder) {
  for (uint32_t seed = 1; seed <= 200; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed);
    d.net.setFaults(hostile());
    Rng rng(seed);
    const std::vector<Item> fromA = makeItems(rng, 12);
    const std::vector<Item> fromB = makeItems(rng, 12);
    size_t ia = 0;
    size_t ib = 0;
    uint32_t ms = 0;
    for (; ms < 120000; ms++) {
      if (ia < fromA.size() && d.a.unacked() == 0) {
        ASSERT_TRUE(sendItem(d.a, d.toB, fromA[ia++]));
      }
      if (ib < fromB.size() && d.b.unacked() == 0) {
        ASSERT_TRUE(sendItem(d.b, d.toA, fromB[ib++]));
      }
      d.step();
      if (ia == fromA.size() && ib == fromB.size() && d.a.unacked() == 0 && d.b.unacked() == 0) break;
    }
    ASSERT_LT(ms, 120000u) << "no progress within the time cap";
    d.step(2000);  // late copies of old frames arrive now and must not be delivered again
    std::vector<uint32_t> wantAtB;
    std::vector<uint32_t> wantAtA;
    for (const Item& it : fromA) wantAtB.push_back(keyOfItem(it));
    for (const Item& it : fromB) wantAtA.push_back(keyOfItem(it));
    EXPECT_EQ(d.sb.keys(), wantAtB);
    EXPECT_EQ(d.sa.keys(), wantAtA);
    EXPECT_FALSE(d.sa.badContent);
    EXPECT_FALSE(d.sb.badContent);
    EXPECT_TRUE(d.sa.silent.empty());
    EXPECT_TRUE(d.sb.silent.empty());
  }
}

TEST(ReliableLinkTest, BurstsDeliverAnInOrderSubsequenceHoldingEveryKeptFrame) {
  for (uint32_t seed = 1; seed <= 200; seed++) {
    SCOPED_TRACE(seed);
    Duo d(seed);
    d.net.setFaults(hostile());
    Rng rng(seed + 1000);
    const std::vector<Item> fromA = makeItems(rng, 40);
    const std::vector<Item> fromB = makeItems(rng, 40);
    std::vector<uint64_t> timeA;
    std::vector<uint64_t> timeB;
    uint64_t ta = 0;
    uint64_t tb = 0;
    for (size_t i = 0; i < fromA.size(); i++) {
      ta += rng.below(301);
      tb += rng.below(301);
      timeA.push_back(ta);
      timeB.push_back(tb);
    }
    std::vector<uint32_t> acceptedA;
    std::vector<uint32_t> acceptedB;
    std::set<uint32_t> mustA;
    std::set<uint32_t> mustB;
    size_t ia = 0;
    size_t ib = 0;
    uint64_t ms = 0;
    const uint64_t lastSend = std::max(ta, tb);
    for (; ms < lastSend + 120000; ms++) {
      while (ia < fromA.size() && timeA[ia] <= ms) {
        const Item& it = fromA[ia++];
        if (sendItem(d.a, d.toB, it)) {
          acceptedA.push_back(keyOfItem(it));
          if (it.type != Type::State || it.over) mustA.insert(keyOfItem(it));
        }
      }
      while (ib < fromB.size() && timeB[ib] <= ms) {
        const Item& it = fromB[ib++];
        if (sendItem(d.b, d.toA, it)) {
          acceptedB.push_back(keyOfItem(it));
          if (it.type != Type::State || it.over) mustB.insert(keyOfItem(it));
        }
      }
      d.step();
      if (ms > lastSend && d.a.unacked() == 0 && d.b.unacked() == 0) break;
    }
    ASSERT_LT(ms, lastSend + 120000) << "no progress within the time cap";
    d.step(2000);
    ASSERT_FALSE(acceptedA.empty());
    ASSERT_FALSE(acceptedB.empty());
    const std::vector<uint32_t> gotAtB = d.sb.keys();
    const std::vector<uint32_t> gotAtA = d.sa.keys();
    EXPECT_TRUE(isSubsequence(gotAtB, acceptedA)) << "in order, no repeat, nothing invented";
    EXPECT_TRUE(isSubsequence(gotAtA, acceptedB));
    const std::set<uint32_t> uniqueB(gotAtB.begin(), gotAtB.end());
    const std::set<uint32_t> uniqueA(gotAtA.begin(), gotAtA.end());
    EXPECT_EQ(uniqueB.size(), gotAtB.size()) << "a repeat";
    EXPECT_EQ(uniqueA.size(), gotAtA.size()) << "a repeat";
    for (const uint32_t k : mustA) EXPECT_TRUE(uniqueB.count(k) == 1) << "lost a kept frame " << std::hex << k;
    for (const uint32_t k : mustB) EXPECT_TRUE(uniqueA.count(k) == 1) << "lost a kept frame " << std::hex << k;
    EXPECT_TRUE(uniqueB.count(acceptedA.back()) == 1) << "the last frame";
    EXPECT_TRUE(uniqueA.count(acceptedB.back()) == 1) << "the last frame";
    EXPECT_FALSE(d.sa.badContent);
    EXPECT_FALSE(d.sb.badContent);
  }
}

TEST(ReliableLinkTest, ResendsFollowTheConfiguredInterval) {
  for (const uint32_t resend : {ReliableLink::DEFAULT_RESEND_MS, 150u, 1234u}) {
    SCOPED_TRACE(resend);
    Settings s;
    s.resendMs = resend;
    Solo x(s);
    const Bytes snap = snapshotFor(3);
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 3)));
    x.raw.drain();
    x.step(5 * resend + resend - 1);
    const std::vector<uint64_t> want = {0, resend, 2ull * resend, 3ull * resend, 4ull * resend, 5ull * resend};
    EXPECT_EQ(x.raw.timesOf(Type::State), want);
    for (const Wire& w : x.raw.of(Type::State)) EXPECT_EQ(w.seq, 0u) << "a resend reuses the seq";
    EXPECT_EQ(x.rl.stats().resends, 5u);
    EXPECT_EQ(x.rl.stats().framesSent, 1u);
  }
}

void runSilence(const Settings& s) {
  SCOPED_TRACE(s.silenceMs);
  const uint64_t limit = s.silenceMs;
  {
    // Heard once at t = 0, then nothing.
    Solo x(s);
    x.deliver(joinFrame(1), 0, NO_ACK);
    ASSERT_EQ(x.sink.frames.size(), 1u);
    x.step(static_cast<uint32_t>(limit - 1));
    EXPECT_TRUE(x.sink.silent.empty()) << "no report before the limit";
    x.step();
    ASSERT_EQ(x.sink.silent.size(), 1u) << "exactly one at the limit";
    EXPECT_EQ(x.sink.silentAt[0], limit);
    EXPECT_EQ(x.sink.silent[0], x.toRaw);
    x.step(static_cast<uint32_t>(3 * limit));
    EXPECT_EQ(x.sink.silent.size(), 1u) << "none later";
    EXPECT_EQ(x.rl.stats().silentReports, 1u);
  }
  {
    // Heard again every 900 ms: never reported.
    Solo x(s);
    x.deliver(joinFrame(1), 0, NO_ACK);
    for (uint32_t t = 0; t < 3 * limit; t += 900) {
      x.step(900);
      ASSERT_TRUE(x.raw.putType(Type::Ping, 0, NO_ACK));
    }
    EXPECT_TRUE(x.sink.silent.empty());
  }
  {
    // Only sent to, never heard: the limit runs from the first send.
    Solo x(s);
    x.step(100);
    const Bytes snap = snapshotFor(1);
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 1)));
    x.step(static_cast<uint32_t>(limit - 1));
    EXPECT_TRUE(x.sink.silent.empty());
    x.step();
    ASSERT_EQ(x.sink.silent.size(), 1u);
    EXPECT_EQ(x.sink.silentAt[0], 100 + limit);
    x.step(static_cast<uint32_t>(3 * limit));
    EXPECT_EQ(x.sink.silent.size(), 1u);
  }
}

TEST(ReliableLinkTest, SilenceIsReportedOnceAtTheLimit) {
  runSilence(Settings{});
  Settings a;
  a.silenceMs = 2500;
  a.keepaliveMs = 500;
  runSilence(a);
  Settings b;
  b.silenceMs = 30000;
  b.keepaliveMs = 2000;
  runSilence(b);
}

TEST(ReliableLinkTest, AReportedPeerIsDroppedUntilForgotten) {
  Solo x;
  const Bytes snap = snapshotFor(1);
  x.deliver(joinFrame(1), 0, NO_ACK);
  ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 1)));
  EXPECT_EQ(x.rl.unacked(), 1u);
  x.step(10000);
  ASSERT_EQ(x.sink.silent.size(), 1u);
  EXPECT_EQ(x.rl.unacked(), 0u) << "its queue is dropped";
  EXPECT_EQ(x.rl.unacked(x.toRaw), 0u);
  EXPECT_FALSE(x.rl.send(x.toRaw, stateFrame(snap, 2))) << "a lost peer takes no reliable frame";
  EXPECT_FALSE(x.rl.send(x.toRaw, joinFrame(2)));

  const size_t seenBefore = x.raw.seen.size();
  const size_t delivered = x.sink.frames.size();
  x.deliver(stateFrame(snap, 5), 1, NO_ACK);
  x.deliver(joinFrame(9), 1, NO_ACK);
  x.deliver(plainFrame(Type::Ping), 0, NO_ACK);
  x.step(2000);
  EXPECT_EQ(x.sink.frames.size(), delivered) << "a lost peer's frames are ignored";
  EXPECT_EQ(x.raw.seen.size(), seenBefore) << "and nothing goes out to it";
  EXPECT_EQ(x.sink.silent.size(), 1u);

  x.deliver(abortFrame(), 0, NO_ACK);
  ASSERT_EQ(x.sink.frames.size(), delivered + 1);
  EXPECT_EQ(x.sink.frames.back().type, Type::Abort) << "an ABORT from it is still delivered";
  ASSERT_TRUE(x.rl.send(x.toRaw, abortFrame())) << "and one can still be sent to it";
  x.raw.drain();
  EXPECT_EQ(x.raw.of(Type::Abort).size(), 1u);

  x.rl.forget(x.toRaw);
  x.deliver(joinFrame(3), 0, NO_ACK);
  ASSERT_EQ(x.sink.frames.size(), delivered + 2) << "forget lets it register again, from seq 0";
  EXPECT_EQ(x.sink.frames.back().key, keyOf(joinFrame(3)));
  EXPECT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 4)));
  x.step(9999);
  EXPECT_EQ(x.sink.silent.size(), 1u);
  x.step(1);
  EXPECT_EQ(x.sink.silent.size(), 2u) << "and can go silent again";
}

TEST(ReliableLinkTest, SeqWrapsAt65535) {
  // A raw peer acks each frame by hand: 66,000 `over` States one at a time, so none is replaced.
  Solo x;
  for (uint32_t i = 0; i < 66000; i++) {
    const uint16_t ver = static_cast<uint16_t>(i);
    const Bytes snap = snapshotFor(ver);
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, ver, true))) << i;
    x.raw.drain();
    const std::vector<Wire> states = x.raw.of(Type::State);
    ASSERT_EQ(states.size(), 1u) << i;
    ASSERT_EQ(states[0].seq, static_cast<uint16_t>(i)) << "the wire seq runs to 65,535, then 0, then 1";
    ASSERT_EQ(states[0].ver, ver);
    ASSERT_TRUE(x.raw.putAck(states[0].seq));
    x.raw.seen.clear();
    x.clock.advance(1);
    x.rl.tick();
    ASSERT_EQ(x.rl.unacked(), 0u) << i;
  }
  EXPECT_EQ(x.rl.stats().framesSent, 66000u);
  EXPECT_EQ(x.rl.stats().resends, 0u);
  EXPECT_TRUE(x.sink.silent.empty());
}

TEST(ReliableLinkTest, SeqWrapDeliversEveryFrameOnceInOrder) {
  // The same wrap seen from the receiving side: each frame delivered once, in order, across the wrap.
  Duo d;
  for (uint32_t i = 0; i < 66000; i++) {
    const uint16_t ver = static_cast<uint16_t>(1 + i % 60000);
    const Bytes snap = snapshotFor(ver);
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(snap, ver, true))) << i;
    d.step(2);
    ASSERT_EQ(d.a.unacked(), 0u) << i;
    ASSERT_EQ(d.sb.frames.size(), 1u) << i;
    ASSERT_EQ(d.sb.frames[0].key, keyOf(stateFrame(snap, ver, true))) << i;
    d.sb.frames.clear();
  }
  EXPECT_EQ(d.sb.offered.size(), 66000u);
  EXPECT_EQ(d.b.stats().duplicates, 0u);
}

TEST(ReliableLinkTest, UnsequencedFramesConsumeNoSeq) {
  Solo x;
  const Bytes s1 = snapshotFor(1);
  const Bytes s2 = snapshotFor(2);
  const Bytes s3 = snapshotFor(3);
  const auto ackLast = [&x] {
    x.raw.drain();
    uint16_t seq = 0;
    uint64_t at = 0;
    bool any = false;
    for (const Wire& w : x.raw.seen) {
      if ((w.type == Type::State || w.type == Type::Join) && (!any || w.atMs >= at)) {
        seq = w.seq;
        at = w.atMs;
        any = true;
      }
    }
    ASSERT_TRUE(any);
    ASSERT_TRUE(x.raw.putAck(seq));
    x.rl.tick();
  };
  ASSERT_TRUE(x.rl.send(x.toRaw, joinFrame(1)));  // seq 0
  ackLast();
  ASSERT_TRUE(x.rl.send(x.toRaw, abortFrame()));
  ASSERT_TRUE(x.rl.broadcast(advertFrame()));
  x.step(1500);                                        // a PING: nothing received yet
  ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s1, 1)));  // seq 1
  ackLast();
  x.deliver(joinFrame(5), 0, NO_ACK);  // now an ACK is what it sends
  x.step(1500);
  ASSERT_TRUE(x.rl.send(x.toRaw, abortFrame()));
  ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s2, 2)));  // seq 2
  ackLast();
  ASSERT_TRUE(x.rl.broadcast(advertFrame()));
  ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s3, 3, true)));  // seq 3
  x.raw.drain();

  std::vector<uint16_t> reliable;
  size_t aborts = 0;
  size_t adverts = 0;
  size_t pings = 0;
  size_t acks = 0;
  for (const Wire& w : x.raw.seen) {
    switch (w.type) {
      case Type::Join:
      case Type::State:
        if (reliable.empty() || reliable.back() != w.seq) reliable.push_back(w.seq);
        break;
      case Type::Abort:
        aborts++;
        EXPECT_EQ(w.seq, 0u);
        break;
      case Type::Advert:
        adverts++;
        EXPECT_EQ(w.seq, 0u);
        break;
      case Type::Ping:
        pings++;
        EXPECT_EQ(w.seq, 0u);
        break;
      case Type::Ack:
        acks++;
        EXPECT_EQ(w.seq, 0u);
        break;
      default:
        break;
    }
  }
  EXPECT_EQ(reliable, (std::vector<uint16_t>{0, 1, 2, 3})) << "reliable seq runs on without a gap";
  EXPECT_EQ(aborts, 2u);
  EXPECT_EQ(adverts, 2u);
  EXPECT_GE(pings, 1u);
  EXPECT_GE(acks, 1u);

  EXPECT_FALSE(x.rl.send(x.toRaw, plainFrame(Type::Ping)));
  EXPECT_FALSE(x.rl.send(x.toRaw, plainFrame(Type::Ack)));
  EXPECT_FALSE(x.rl.send(x.toRaw, advertFrame()));
  EXPECT_FALSE(x.rl.broadcast(joinFrame(1)));
  EXPECT_FALSE(x.rl.broadcast(stateFrame(s1, 1)));
  EXPECT_FALSE(x.rl.broadcast(abortFrame()));
  EXPECT_FALSE(x.rl.broadcast(plainFrame(Type::Ping)));
}

TEST(ReliableLinkTest, AnAbortBehindAnUnackedStateIsDeliveredAtOnce) {
  Duo d;
  const Bytes snap = snapshotFor(1);
  d.blockAToB(true);
  ASSERT_TRUE(d.a.send(d.toB, stateFrame(snap, 1)));  // lost
  d.blockAToB(false);
  ASSERT_TRUE(d.a.send(d.toB, abortFrame()));
  d.step();
  ASSERT_EQ(d.sb.frames.size(), 1u);
  EXPECT_EQ(d.sb.frames[0].type, Type::Abort) << "no wait for the State's resend";
  EXPECT_EQ(d.sb.frames[0].atMs, 1u);
  EXPECT_EQ(d.a.unacked(), 1u) << "the State is still unacked";
  EXPECT_EQ(d.sb.offered.size(), 0u);
  d.step(400);
  EXPECT_EQ(d.sb.count(Type::State), 1u) << "and arrives on its resend";
  EXPECT_EQ(d.a.unacked(), 0u);
}

TEST(ReliableLinkTest, AReplacedStateIsNeverDelivered) {
  {
    // The wire: v1 goes out as seq 0, v2 as seq 1 (the abandoned seq is never reused).
    Solo x;
    const Bytes s1 = snapshotFor(1);
    const Bytes s2 = snapshotFor(2);
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s1, 1)));
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s2, 2)));
    x.raw.drain();
    const std::vector<Wire> states = x.raw.of(Type::State);
    ASSERT_EQ(states.size(), 2u);
    EXPECT_EQ(states[0].ver, 1u);
    EXPECT_EQ(states[0].seq, 0u);
    EXPECT_EQ(states[1].ver, 2u);
    EXPECT_EQ(states[1].seq, 1u);
    EXPECT_EQ(x.rl.unacked(), 1u);
    x.step(2000);
    size_t v1 = 0;
    for (const Wire& w : x.raw.of(Type::State)) v1 += w.ver == 1 ? 1 : 0;
    EXPECT_EQ(v1, 1u) << "the abandoned frame is never resent";
    EXPECT_GE(x.raw.of(Type::State).size(), 4u) << "the newer one is";
  }
  {
    // v1 lost, v2 sent after: only v2 is delivered.
    Duo d;
    const Bytes s1 = snapshotFor(1);
    const Bytes s2 = snapshotFor(2);
    d.blockAToB(true);
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s1, 1)));
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s2, 2)));
    d.blockAToB(false);
    d.step(3000);
    EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(s2, 2))}));
    EXPECT_EQ(d.sb.offered.size(), 1u);
    EXPECT_EQ(d.a.unacked(), 0u);
  }
  {
    // v1 and v2 queued behind an unacked ACCEPT: ACCEPT then v2.
    Duo d;
    const Bytes s1 = snapshotFor(1);
    const Bytes s2 = snapshotFor(2);
    d.blockAToB(true);
    ASSERT_TRUE(d.a.send(d.toB, acceptFrame()));
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s1, 1)));
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s2, 2)));
    EXPECT_EQ(d.a.unacked(), 2u) << "v2 took v1's place";
    d.blockAToB(false);
    d.step(3000);
    EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(acceptFrame()), keyOf(stateFrame(s2, 2))}));
    EXPECT_EQ(d.a.unacked(), 0u);
  }
}

TEST(ReliableLinkTest, AnOverStateIsKeptAndNewerStatesFollowIt) {
  const Bytes s1 = snapshotFor(1);
  const Bytes s2 = snapshotFor(2);
  const Bytes s3 = snapshotFor(3);
  {
    Duo d;
    d.blockAToB(true);
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s1, 1, true)));
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s2, 2, true)));
    EXPECT_EQ(d.a.unacked(), 2u);
    d.blockAToB(false);
    d.step(3000);
    EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(s1, 1)), keyOf(stateFrame(s2, 2))}));
  }
  {
    Duo d;
    d.blockAToB(true);
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s1, 1, false)));
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s2, 2, true)));
    EXPECT_EQ(d.a.unacked(), 1u);
    d.blockAToB(false);
    d.step(3000);
    EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(s2, 2))}));
    ASSERT_EQ(d.sb.frames.size(), 1u);
    EXPECT_TRUE(d.sb.frames[0].over);
  }
  {
    Duo d;
    d.blockAToB(true);
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s1, 1, true)));
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s2, 2, true)));
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(s3, 3, false)));
    d.blockAToB(false);
    d.step(5000);
    EXPECT_EQ(d.sb.keys(),
              (std::vector<uint32_t>{keyOf(stateFrame(s1, 1)), keyOf(stateFrame(s2, 2)), keyOf(stateFrame(s3, 3))}));
  }
}

TEST(ReliableLinkTest, AStaleOrEarlyAckNeverAcksTheInFlightFrame) {
  const Bytes s1 = snapshotFor(1);
  const Bytes s2 = snapshotFor(2);
  {
    // seq 0 abandoned for seq 1.
    Solo x;
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s1, 1)));
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s2, 2)));
    ASSERT_TRUE(x.raw.putAck(0));  // the abandoned frame, acked late
    x.rl.tick();
    EXPECT_EQ(x.rl.unacked(), 1u);
    ASSERT_TRUE(x.raw.putAck(NO_ACK));
    x.rl.tick();
    EXPECT_EQ(x.rl.unacked(), 1u);
    for (const uint16_t early : {2, 3, 100, 0x7FFF, 0x8000, 0xFFFE}) {
      ASSERT_TRUE(x.raw.putAck(early)) << early;  // never issued
      x.rl.tick();
      EXPECT_EQ(x.rl.unacked(), 1u) << early;
    }
    ASSERT_TRUE(x.raw.putType(Type::Ping, 0, 0));
    x.rl.tick();
    EXPECT_EQ(x.rl.unacked(), 1u);
    ASSERT_TRUE(x.raw.putAck(1));
    x.rl.tick();
    EXPECT_EQ(x.rl.unacked(), 0u) << "the right ack pops it";
  }
  {
    // The initial ack on the wire, 0xFFFF, against seq 0.
    Solo x;
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s1, 1)));
    x.deliver(joinFrame(1), 0, NO_ACK);
    EXPECT_EQ(x.rl.unacked(), 1u);
    ASSERT_TRUE(x.raw.putAck(0));
    x.rl.tick();
    EXPECT_EQ(x.rl.unacked(), 0u);
  }
  {
    // The same on a State frame's own ack field.
    Solo x;
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(s1, 1)));
    x.deliver(stateFrame(s2, 2), 0, 7);
    EXPECT_EQ(x.rl.unacked(), 1u);
    x.deliver(stateFrame(s2, 3), 1, 0);
    EXPECT_EQ(x.rl.unacked(), 0u);
  }
}

TEST(ReliableLinkTest, AFrameRefusedByRuleIsAckedAndNotResent) {
  Duo d;
  d.sb.script = {Verdict::Refused};
  const Bytes snap = snapshotFor(1);
  ASSERT_TRUE(d.a.send(d.toB, stateFrame(snap, 1)));
  d.step(5000);
  EXPECT_EQ(d.a.unacked(), 0u) << "a Refused frame is acked";
  EXPECT_EQ(d.sb.offered.size(), 1u);
  EXPECT_EQ(d.sb.frames.size(), 0u);
  EXPECT_EQ(d.a.stats().resends, 0u);
  EXPECT_GE(d.b.stats().acksSent, 1u);
  // The next frame goes on from there.
  const Bytes s2 = snapshotFor(2);
  ASSERT_TRUE(d.a.send(d.toB, stateFrame(s2, 2)));
  d.step(100);
  EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(s2, 2))}));
}

TEST(ReliableLinkTest, AFrameWithNoRoomIsLeftUnackedAndDeliveredOnALaterResend) {
  Duo d;
  d.sb.script = {Verdict::NoRoom, Verdict::NoRoom, Verdict::NoRoom};
  const Bytes snap = snapshotFor(1);
  ASSERT_TRUE(d.a.send(d.toB, stateFrame(snap, 1)));
  ASSERT_TRUE(d.a.send(d.toB, acceptFrame()));  // waits behind it
  d.step(1199);
  EXPECT_EQ(d.b.stats().acksSent, 0u) << "no ACK for a frame that found no room";
  EXPECT_EQ(d.sb.offered.size(), 3u);
  EXPECT_EQ(d.sb.offered[0].atMs, 1u) << "B first ticks at t = 1";
  EXPECT_EQ(d.sb.offered[1].atMs, 400u);
  EXPECT_EQ(d.sb.offered[2].atMs, 800u);
  EXPECT_EQ(d.sb.frames.size(), 0u);
  EXPECT_EQ(d.b.stats().noRoom, 3u);
  EXPECT_EQ(d.a.unacked(), 2u);
  d.step(1);
  ASSERT_EQ(d.sb.offered.size(), 4u);
  EXPECT_EQ(d.sb.offered[3].atMs, 1200u);
  d.step(5);
  EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(snap, 1)), keyOf(acceptFrame())}))
      << "delivered once, and the frame behind it follows";
  EXPECT_EQ(d.a.unacked(), 0u);
  d.step(3000);
  EXPECT_EQ(d.sb.frames.size(), 2u);
  EXPECT_EQ(d.sb.offered.size(), 5u) << "three refusals, the State, and the Accept behind it";
}

TEST(ReliableLinkTest, ASideThatAcksEveryTickStillResendsOnSchedule) {
  Solo x;
  const Bytes snap = snapshotFor(1);
  ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 1)));
  x.raw.drain();
  for (uint32_t t = 0; t < 3000; t++) {
    ASSERT_TRUE(x.raw.put(joinFrame(1), 0, NO_ACK));  // the same frame again, every tick
    x.step();
  }
  EXPECT_EQ(x.sink.frames.size(), 1u) << "delivered once";
  EXPECT_EQ(x.rl.stats().duplicates, 2999u);
  EXPECT_EQ(x.raw.of(Type::Ack).size(), 3000u) << "every copy is acked";
  EXPECT_EQ(x.raw.timesOf(Type::State), (std::vector<uint64_t>{0, 400, 800, 1200, 1600, 2000, 2400, 2800}));
}

TEST(ReliableLinkTest, ALinkRefusalIsAnOrdinaryLoss) {
  Duo d;
  d.la.refuseSends(2);
  const Bytes snap = snapshotFor(1);
  ASSERT_TRUE(d.a.send(d.toB, stateFrame(snap, 1))) << "queued, though the link refused the first transmission";
  d.step(399);
  EXPECT_EQ(d.sb.frames.size(), 0u);
  d.step(1);  // the first resend, refused too
  d.step(399);
  EXPECT_EQ(d.sb.frames.size(), 0u);
  d.step(1);  // the second resend gets through
  EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(snap, 1))}));
  EXPECT_EQ(d.a.stats().linkRefused, 2u);
  EXPECT_EQ(d.net.refusedSends(), 2u);
  d.step(10);
  EXPECT_EQ(d.a.unacked(), 0u);
}

void runQuietMatch(const uint32_t keepaliveMs) {
  SCOPED_TRACE(keepaliveMs);
  Settings s;
  s.keepaliveMs = keepaliveMs;
  Duo d(1, s);
  const uint64_t budget = 4 + 2 * (60000 / keepaliveMs + 1);
  d.net.setCarriedBudget(budget);
  ASSERT_TRUE(d.a.send(d.toB, joinFrame(1)));
  ASSERT_TRUE(d.b.send(d.toA, acceptFrame()));
  d.step(60000);
  EXPECT_FALSE(d.net.overBudget()) << "carried " << d.net.carried() << " of at most " << budget;
  EXPECT_TRUE(d.sa.silent.empty());
  EXPECT_TRUE(d.sb.silent.empty());
  EXPECT_GE(d.net.carried(), 2 * (60000 / keepaliveMs - 1) + 4ull) << "the keepalives really flow";
  EXPECT_EQ(d.a.unacked(), 0u);
  EXPECT_EQ(d.b.unacked(), 0u);
  EXPECT_EQ(d.a.stats().resends, 0u);
}

TEST(ReliableLinkTest, AQuietMatchStaysUnderTheFrameBound) {
  runQuietMatch(ReliableLink::DEFAULT_KEEPALIVE_MS);
  runQuietMatch(250);
}

TEST(ReliableLinkTest, KeepaliveIsAnAckOnceAFrameWasReceivedAndAPingBefore) {
  Solo x;
  ASSERT_TRUE(x.rl.send(x.toRaw, joinFrame(1)));
  x.raw.drain();
  x.raw.putAck(0);
  x.rl.tick();
  x.step(3500);
  EXPECT_EQ(x.raw.of(Type::Ack).size(), 0u);
  EXPECT_EQ(x.raw.timesOf(Type::Ping), (std::vector<uint64_t>{1000, 2000, 3000}))
      << "the keepalive is a PING until a frame has been received";
  x.raw.seen.clear();
  x.deliver(joinFrame(5), 0, 0);  // the link acks it
  x.raw.seen.clear();
  const uint64_t base = x.clock.nowMs();
  x.step(3500);
  EXPECT_EQ(x.raw.of(Type::Ping).size(), 0u);
  EXPECT_EQ(x.raw.timesOf(Type::Ack), (std::vector<uint64_t>{base + 1000, base + 2000, base + 3000}))
      << "afterwards it is an ACK carrying the last seq received";
  for (const Wire& w : x.raw.of(Type::Ack)) EXPECT_EQ(w.ack, 0u);
}

TEST(ReliableLinkTest, OtherSessionsAndUnsetSessionAreIgnored) {
  const Bytes snap = snapshotFor(1);
  {
    Solo x;
    constexpr uint32_t OTHER = SESSION + 1;
    ASSERT_TRUE(x.raw.put(joinFrame(1), 0, NO_ACK, OTHER));
    ASSERT_TRUE(x.raw.put(stateFrame(snap, 1), 0, NO_ACK, OTHER));
    ASSERT_TRUE(x.raw.put(advertFrame(), 0, 0, OTHER));
    ASSERT_TRUE(x.raw.put(abortFrame(), 0, 0, OTHER));
    x.step(11000);
    EXPECT_EQ(x.sink.frames.size(), 0u);
    EXPECT_EQ(x.sink.offered.size(), 0u);
    EXPECT_EQ(x.rl.stats().badFrames, 4u);
    EXPECT_EQ(x.raw.seen.size(), 0u) << "not acked, and no peer to send a keepalive to";
    EXPECT_TRUE(x.sink.silent.empty()) << "no peer was registered";
  }
  {
    Solo x;
    x.rl.setSession(Protocol::ANY_SESSION);
    EXPECT_FALSE(x.rl.send(x.toRaw, stateFrame(snap, 1))) << "no session, no reliable send";
    EXPECT_FALSE(x.rl.send(x.toRaw, abortFrame()));
    EXPECT_FALSE(x.rl.broadcast(advertFrame()));
    ASSERT_TRUE(x.raw.put(advertFrame(), 0, 0, 0x1234));
    ASSERT_TRUE(x.raw.put(stateFrame(snap, 1), 0, NO_ACK, 0x1234));
    ASSERT_TRUE(x.raw.put(joinFrame(1), 0, NO_ACK, 0x1234));
    ASSERT_TRUE(x.raw.put(abortFrame(), 0, 0, 0x1234));
    x.rl.tick();
    x.raw.drain();
    ASSERT_EQ(x.sink.frames.size(), 1u);
    EXPECT_EQ(x.sink.frames[0].type, Type::Advert) << "with the session unset only an ADVERT is processed";
    EXPECT_EQ(x.rl.stats().strayFrames, 3u);
    EXPECT_EQ(x.raw.seen.size(), 0u);
  }
}

TEST(ReliableLinkTest, ADuplicateIsAckedAndNeverDeliveredTwice) {
  Solo x;
  const Bytes s1 = snapshotFor(1);
  const Bytes s2 = snapshotFor(2);
  x.deliver(joinFrame(1), 0, NO_ACK);
  x.deliver(joinFrame(1), 0, NO_ACK);
  x.deliver(joinFrame(1), 0, NO_ACK);
  x.deliver(stateFrame(s1, 1), 1, NO_ACK);
  x.deliver(stateFrame(s1, 1), 1, NO_ACK);
  x.deliver(joinFrame(1), 0, NO_ACK);
  EXPECT_EQ(x.sink.keys(), (std::vector<uint32_t>{keyOf(joinFrame(1)), keyOf(stateFrame(s1, 1))}));
  EXPECT_EQ(x.sink.offered.size(), 2u);
  EXPECT_EQ(x.rl.stats().duplicates, 4u);
  const std::vector<Wire> acks = x.raw.of(Type::Ack);
  ASSERT_EQ(acks.size(), 6u) << "every copy is acked";
  EXPECT_EQ(acks[0].ack, 0u);
  EXPECT_EQ(acks[1].ack, 0u);
  EXPECT_EQ(acks[3].ack, 1u);
  EXPECT_EQ(acks[5].ack, 1u);
}

TEST(ReliableLinkTest, AnAheadJoinIsDroppedUnacked) {
  Solo x;
  x.deliver(joinFrame(1), 0, NO_ACK);
  x.raw.seen.clear();
  x.deliver(joinFrame(2), 5, NO_ACK);  // seq 1 is next
  x.deliver(acceptFrame(), 2, NO_ACK);
  EXPECT_EQ(x.sink.frames.size(), 1u);
  EXPECT_EQ(x.sink.offered.size(), 1u);
  EXPECT_EQ(x.rl.stats().aheadDropped, 2u);
  EXPECT_EQ(x.raw.of(Type::Ack).size(), 0u);
  x.deliver(joinFrame(2), 1, NO_ACK);
  EXPECT_EQ(x.sink.frames.size(), 2u);
  EXPECT_EQ(x.raw.of(Type::Ack).size(), 1u);
}

TEST(ReliableLinkTest, AStateAheadOfAGapIsTakenAsASkip) {
  Solo x;
  const Bytes s3 = snapshotFor(3);
  x.deliver(stateFrame(s3, 3), 4, NO_ACK);
  ASSERT_EQ(x.sink.frames.size(), 1u);
  x.raw.drain();
  ASSERT_EQ(x.raw.of(Type::Ack).size(), 1u);
  EXPECT_EQ(x.raw.of(Type::Ack)[0].ack, 4u);
}

TEST(ReliableLinkTest, AdvertBroadcastReachesTheSinkAndRegistersNoPeer) {
  Duo d;
  d.b.setSession(Protocol::ANY_SESSION);
  ASSERT_TRUE(d.a.broadcast(advertFrame()));
  d.step();
  ASSERT_EQ(d.sb.frames.size(), 1u);
  EXPECT_EQ(d.sb.frames[0].type, Type::Advert);
  EXPECT_EQ(d.sb.frames[0].from, d.toA);
  d.step(11000);
  EXPECT_TRUE(d.sa.silent.empty());
  EXPECT_TRUE(d.sb.silent.empty());
  EXPECT_EQ(d.b.unacked(), 0u);
  EXPECT_EQ(d.net.carried(), 1u) << "no peer, so no keepalive either";
  EXPECT_EQ(d.a.stats().framesSent, 1u);
}

TEST(ReliableLinkTest, ASinkMayReplyInsideOnFrame) {
  {
    Duo d;
    const Bytes snap = snapshotFor(1);
    d.sb.hook = [&d](const PeerHandle from, const Frame& f) {
      if (f.header.type == Type::State) {
        EXPECT_TRUE(d.b.send(from, acceptFrame()));
      }
    };
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(snap, 1)));
    d.step(50);
    EXPECT_EQ(d.sa.keys(), (std::vector<uint32_t>{keyOf(acceptFrame())})) << "the reply went out";
    EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(snap, 1))}));
    EXPECT_EQ(d.a.unacked(), 0u);
    EXPECT_EQ(d.b.unacked(), 0u);
  }
  {
    // A forget inside the callback is safe: no ack for a peer that is gone, and the resend registers it afresh.
    Duo d;
    const Bytes snap = snapshotFor(1);
    bool first = true;
    d.sb.hook = [&](const PeerHandle from, const Frame&) {
      if (first) {
        first = false;
        d.b.forget(from);
      }
    };
    ASSERT_TRUE(d.a.send(d.toB, stateFrame(snap, 1)));
    d.step(50);
    EXPECT_EQ(d.b.stats().acksSent, 0u);
    EXPECT_EQ(d.a.unacked(), 1u);
    d.step(400);
    EXPECT_EQ(d.a.unacked(), 0u);
    EXPECT_EQ(d.sb.offered.size(), 2u);
  }
}

TEST(ReliableLinkTest, TwoGuestsHaveIndependentSeqAndSilence) {
  Settings s;
  s.maxPeers = 2;
  ManualClock clock;
  FakeLinkNet net(clock, 3);
  FakeLink host(net);
  RawPeer g1(net, clock);
  RawPeer g2(net, clock);
  RawPeer g3(net, clock);
  RecSink sink(clock);
  ReliableLink rl(host, clock, sink, s);
  rl.setSession(SESSION);
  g1.connect(host);
  g2.connect(host);
  g3.connect(host);
  const PeerHandle h1 = host.handleOf(g1.link);
  const PeerHandle h2 = host.handleOf(g2.link);
  const Bytes snap = snapshotFor(1);
  const auto tick = [&](const uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
      clock.advance(1);
      rl.tick();
      g1.drain();
      g2.drain();
      g3.drain();
    }
  };

  ASSERT_TRUE(g1.put(joinFrame(1), 0, NO_ACK));
  ASSERT_TRUE(g2.put(joinFrame(2), 0, NO_ACK));
  tick(1);
  ASSERT_EQ(sink.frames.size(), 2u);
  EXPECT_NE(sink.frames[0].from, sink.frames[1].from);
  ASSERT_TRUE(g3.put(joinFrame(3), 0, NO_ACK));
  tick(1);
  EXPECT_EQ(sink.frames.size(), 2u) << "a full table drops the next new peer";
  EXPECT_EQ(rl.stats().peerTableFull, 1u);
  const PeerHandle h3 = host.handleOf(g3.link);
  EXPECT_FALSE(rl.send(h3, acceptFrame(1, 2))) << "a full table has no slot for a send to a new peer either";
  EXPECT_EQ(rl.stats().peerTableFull, 2u);
  EXPECT_EQ(rl.unacked(h3), 0u);
  tick(1);
  EXPECT_TRUE(g3.of(Type::Accept).empty()) << "and nothing went out to it";

  ASSERT_TRUE(rl.send(h1, acceptFrame(1, 2)));
  ASSERT_TRUE(rl.send(h2, acceptFrame(2, 2)));
  tick(1);
  ASSERT_EQ(g1.of(Type::Accept).size(), 1u);
  ASSERT_EQ(g2.of(Type::Accept).size(), 1u);
  EXPECT_EQ(g1.of(Type::Accept)[0].seq, 0u);
  EXPECT_EQ(g2.of(Type::Accept)[0].seq, 0u) << "each peer has its own seq";
  EXPECT_EQ(g1.of(Type::Accept)[0].ack, 0u) << "and its own ack: g1 sent seq 0";
  g1.putAck(0);
  tick(1);
  EXPECT_EQ(rl.unacked(h1), 0u);
  EXPECT_EQ(rl.unacked(h2), 1u);
  ASSERT_TRUE(rl.send(h1, stateFrame(snap, 1)));
  tick(1);
  ASSERT_EQ(g1.of(Type::State).size(), 1u);
  EXPECT_EQ(g1.of(Type::State)[0].seq, 1u);

  // g2 goes quiet, g1 keeps talking.
  for (uint32_t t = 0; t < 12000; t += 900) {
    tick(900);
    g1.putType(Type::Ping, 0, NO_ACK);
  }
  ASSERT_EQ(sink.silent.size(), 1u);
  EXPECT_EQ(sink.silent[0], h2);
  EXPECT_EQ(sink.silentAt[0], 10001u) << "g2 was last heard at t = 1";
  EXPECT_FALSE(rl.send(h2, stateFrame(snap, 2)));
  EXPECT_EQ(rl.unacked(h2), 0u);
  EXPECT_EQ(rl.unacked(h1), 1u) << "g1 is unaffected";
  EXPECT_TRUE(rl.send(h1, abortFrame()));

  // QUEUE_DEPTH queued Joins make the next send false.
  rl.forget(h2);
  ASSERT_TRUE(rl.send(h2, joinFrame(1)));
  for (uint16_t i = 2; i <= ReliableLink::QUEUE_DEPTH; i++) EXPECT_TRUE(rl.send(h2, joinFrame(i)));
  EXPECT_EQ(rl.unacked(h2), ReliableLink::QUEUE_DEPTH);
  EXPECT_FALSE(rl.send(h2, joinFrame(99)));
  EXPECT_EQ(rl.stats().queueFull, 1u);
  EXPECT_EQ(rl.unacked(h2), ReliableLink::QUEUE_DEPTH);
}

TEST(ReliableLinkTest, SendRefusesWhatEncodeRefuses) {
  Solo x;
  const PeerHandle none;
  Bytes big(Protocol::MAX_SNAPSHOT_BYTES + 1, 5);
  Bytes max(Protocol::MAX_SNAPSHOT_BYTES, 5);
  Bytes empty;
  const Bytes one(1, 1);

  EXPECT_FALSE(x.rl.send(x.toRaw, stateFrame(big, 1)));
  EXPECT_FALSE(x.rl.send(x.toRaw, stateFrame(empty, 1)));
  Frame badStatus = stateFrame(one, 1);
  badStatus.state.status.over = true;  // over needs turn 0
  EXPECT_FALSE(x.rl.send(x.toRaw, badStatus));
  EXPECT_FALSE(x.rl.send(x.toRaw, acceptFrame(3, 2)));
  EXPECT_FALSE(x.rl.send(none, stateFrame(one, 1)));
  EXPECT_FALSE(x.rl.send(none, abortFrame()));
  Frame badAbort = abortFrame();
  badAbort.abortReason = static_cast<Protocol::AbortReason>(0);
  EXPECT_FALSE(x.rl.send(x.toRaw, badAbort));
  EXPECT_EQ(x.rl.unacked(), 0u);
  x.raw.drain();
  EXPECT_EQ(x.raw.seen.size(), 0u);
  EXPECT_EQ(x.net.carried(), 0u);

  ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(max, 1)));
  x.raw.drain();
  ASSERT_EQ(x.raw.of(Type::State).size(), 1u);
  EXPECT_EQ(x.rl.unacked(), 1u);
  EXPECT_EQ(x.raw.undecodable, 0u);

  // A refused send to a new peer registers nothing: it is never reported silent.
  Solo y;
  EXPECT_FALSE(y.rl.send(y.toRaw, stateFrame(big, 1)));
  y.step(11000);
  EXPECT_TRUE(y.sink.silent.empty());
}

TEST(ReliableLinkTest, AnAbortToAnUnknownPeerCarriesTheNothingReceivedAck) {
  {
    Solo x;
    ASSERT_TRUE(x.rl.send(x.toRaw, abortFrame()));
    x.raw.drain();
    ASSERT_EQ(x.raw.of(Type::Abort).size(), 1u);
    EXPECT_EQ(x.raw.of(Type::Abort)[0].ack, NO_ACK) << "0 would ack a peer's in-flight seq 0";
  }
  {
    // A host with no room for the guest rejects its Join with an Abort; that must not pop the guest's Join.
    Settings none;
    none.maxPeers = 0;
    ManualClock clock;
    FakeLinkNet net(clock, 1);
    FakeLink la(net);
    FakeLink lb(net);
    RecSink sa(clock);
    RecSink sb(clock);
    ReliableLink a(la, clock, sa);
    ReliableLink b(lb, clock, sb, none);
    a.setSession(SESSION);
    b.setSession(SESSION);
    ASSERT_TRUE(a.send(la.handleOf(lb), joinFrame(1)));
    for (int i = 0; i < 3; i++) {
      clock.advance(1);
      a.tick();
      b.tick();
    }
    EXPECT_EQ(b.stats().peerTableFull, 1u);
    EXPECT_EQ(sb.frames.size(), 0u);
    ASSERT_TRUE(b.send(lb.handleOf(la), abortFrame()));
    for (int i = 0; i < 3; i++) {
      clock.advance(1);
      a.tick();
      b.tick();
    }
    ASSERT_EQ(sa.frames.size(), 1u);
    EXPECT_EQ(sa.frames[0].type, Type::Abort);
    EXPECT_EQ(a.unacked(), 1u) << "the Join is still awaiting its ack";
  }
}

TEST(ReliableLinkTest, AForgottenPeerThatKeepsItsSeqIsNeverRegisteredAndTheSenderGoesSilent) {
  {
    // Receiver side: a Join at seq 8 from a peer it has no record of.
    Solo x;
    x.deliver(joinFrame(1), 8, NO_ACK);
    x.deliver(acceptFrame(), 9, NO_ACK);
    x.deliver(joinFrame(1), 0x9000, NO_ACK);  // behind, to a peer with no record
    EXPECT_EQ(x.sink.offered.size(), 0u);
    EXPECT_EQ(x.rl.stats().aheadDropped, 2u);
    EXPECT_EQ(x.rl.stats().duplicates, 1u);
    EXPECT_EQ(x.rl.stats().peerTableFull, 0u);
    x.step(11000);
    EXPECT_EQ(x.raw.seen.size(), 0u) << "unacked, and no keepalive to a peer that was never registered";
    EXPECT_TRUE(x.sink.silent.empty());
    x.deliver(joinFrame(1), 0, NO_ACK);
    EXPECT_EQ(x.sink.frames.size(), 1u) << "a Join at seq 0 registers it";
  }
  {
    // Both sides: B forgets A, A keeps its seq.
    Duo d;
    for (uint16_t i = 1; i <= 9; i++) {
      ASSERT_TRUE(d.a.send(d.toB, joinFrame(i)));
      d.step(3);
      ASSERT_EQ(d.a.unacked(), 0u);
    }
    ASSERT_EQ(d.sb.frames.size(), 9u);
    d.b.forget(d.toA);
    const uint32_t acksBefore = d.b.stats().acksSent + d.b.stats().pingsSent;
    const uint64_t forgotAt = d.clock.nowMs();
    ASSERT_TRUE(d.a.send(d.toB, joinFrame(10)));  // seq 9
    d.step(12000);
    EXPECT_EQ(d.sb.frames.size(), 9u) << "dropped as ahead, every time";
    EXPECT_GE(d.b.stats().aheadDropped, 2u);
    EXPECT_EQ(d.b.stats().peerTableFull, 0u);
    EXPECT_EQ(d.b.stats().acksSent + d.b.stats().pingsSent, acksBefore) << "B sends A nothing";
    EXPECT_TRUE(d.sb.silent.empty()) << "B has no peer to report";
    ASSERT_EQ(d.sa.silent.size(), 1u) << "A reports B silent";
    EXPECT_GT(d.sa.silentAt[0], forgotAt);
    EXPECT_LE(d.sa.silentAt[0], forgotAt + 10000);
    EXPECT_EQ(d.a.unacked(), 0u);
  }
}

TEST(ReliableLinkTest, ChangingTheSessionDropsEveryPeersState) {
  const Bytes snap = snapshotFor(1);
  {
    Solo x;
    ASSERT_TRUE(x.rl.send(x.toRaw, joinFrame(1)));
    x.raw.drain();
    x.rl.setSession(SESSION);  // unchanged: nothing is dropped
    EXPECT_EQ(x.rl.unacked(), 1u);
    x.rl.setSession(SESSION + 1);
    EXPECT_EQ(x.rl.session(), SESSION + 1);
    EXPECT_EQ(x.rl.unacked(), 0u);
    x.step(3000);
    EXPECT_EQ(x.raw.of(Type::Join).size(), 1u) << "the old Join is not resent under the new id";
    EXPECT_EQ(x.raw.seen.size(), 1u) << "and the dropped peer gets no keepalive either";
  }
  {
    // A lost peer is cleared too, and the new session numbers its frames from 0.
    Solo x;
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 1)));
    x.step(10000);
    ASSERT_EQ(x.sink.silent.size(), 1u);
    EXPECT_FALSE(x.rl.send(x.toRaw, stateFrame(snap, 2)));
    x.rl.setSession(SESSION + 1);
    x.raw.seen.clear();
    ASSERT_TRUE(x.rl.send(x.toRaw, joinFrame(2)));
    x.raw.drain();
    ASSERT_EQ(x.raw.of(Type::Join).size(), 1u);
    EXPECT_EQ(x.raw.of(Type::Join)[0].seq, 0u);
  }
}

TEST(ReliableLinkTest, ASinkMayCallSendAndForgetInsideOnPeerSilent) {
  {
    Solo x;
    const Bytes snap = snapshotFor(1);
    x.deliver(joinFrame(1), 0, NO_ACK);
    int calls = 0;
    x.sink.silentHook = [&](const PeerHandle peer) {
      calls++;
      EXPECT_FALSE(x.rl.send(peer, stateFrame(snap, 1))) << "a lost peer takes nothing";
    };
    x.step(10000);
    EXPECT_EQ(calls, 1);
    x.step(5000);
    EXPECT_EQ(calls, 1);
  }
  {
    Solo x;
    x.deliver(joinFrame(1), 0, NO_ACK);
    x.sink.silentHook = [&](const PeerHandle peer) { x.rl.forget(peer); };
    x.step(10000);
    ASSERT_EQ(x.sink.silent.size(), 1u);
    x.step(1000);  // the tick loop survived
    x.deliver(joinFrame(2), 0, NO_ACK);
    ASSERT_EQ(x.sink.frames.size(), 2u) << "a later frame registers the peer afresh";
    EXPECT_EQ(x.sink.frames[1].key, keyOf(joinFrame(2)));
    x.step(9999);
    EXPECT_EQ(x.sink.silent.size(), 1u);
    x.step(1);
    EXPECT_EQ(x.sink.silent.size(), 2u);
  }
}

TEST(ReliableLinkTest, AnAbortFromALivePeerRefreshesLivenessAndAppliesItsAck) {
  {
    Solo x;
    x.deliver(joinFrame(1), 0, NO_ACK);
    for (uint32_t t = 0; t < 25000; t += 900) {
      x.step(900);
      ASSERT_TRUE(x.raw.put(abortFrame(), 0, NO_ACK));
    }
    EXPECT_TRUE(x.sink.silent.empty()) << "Aborts under the silence limit keep the peer alive";
    EXPECT_GE(x.sink.count(Type::Abort), 27u);
  }
  {
    Solo x;
    const Bytes snap = snapshotFor(1);
    ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 1)));
    EXPECT_EQ(x.rl.unacked(), 1u);
    x.raw.put(abortFrame(), 0, 5);
    x.rl.tick();
    EXPECT_EQ(x.rl.unacked(), 1u);
    x.raw.put(abortFrame(), 0, 0);
    x.rl.tick();
    EXPECT_EQ(x.rl.unacked(), 0u) << "an Abort carrying ack == the in-flight seq pops it";
    EXPECT_EQ(x.sink.count(Type::Abort), 2u);
  }
}

TEST(ReliableLinkTest, AClockSteppedBackwardsCausesNothingUntilItPassesTheOldMark) {
  Solo x;
  const Bytes snap = snapshotFor(1);
  x.clock.set(5000);
  ASSERT_TRUE(x.rl.send(x.toRaw, stateFrame(snap, 1)));
  x.raw.drain();
  x.clock.set(3000);
  x.step(1999);  // clock 3000 .. 4999
  EXPECT_EQ(x.raw.seen.size(), 1u) << "no resend, no keepalive while the clock is behind its marks";
  EXPECT_TRUE(x.sink.silent.empty());
  x.step(399);  // 5398
  EXPECT_EQ(x.raw.of(Type::State).size(), 1u);
  x.step(1);  // 5399
  EXPECT_EQ(x.raw.of(Type::State).size(), 1u);
  x.step(1);  // 5400: the resend resumes on schedule from the old mark
  EXPECT_EQ(x.raw.of(Type::State).size(), 2u);
  x.step(9599);  // 14999
  EXPECT_TRUE(x.sink.silent.empty());
  x.step(1);  // 15000
  ASSERT_EQ(x.sink.silent.size(), 1u);
  EXPECT_EQ(x.sink.silentAt[0], 15000u);
}

TEST(ReliableLinkTest, AReplacedStateThatAlreadyArrivedIsStillDelivered) {
  Duo d;
  const Bytes s1 = snapshotFor(1);
  const Bytes s2 = snapshotFor(2);
  Faults deaf;
  deaf.lossPercent = 100;
  d.net.setFaults(d.lb.index(), d.la.index(), deaf);  // B hears A, A hears nothing back
  ASSERT_TRUE(d.a.send(d.toB, stateFrame(s1, 1)));
  d.step(2);
  EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(s1, 1))}));
  EXPECT_EQ(d.a.unacked(), 1u) << "v1's ack was lost";
  ASSERT_TRUE(d.a.send(d.toB, stateFrame(s2, 2)));  // replaces v1: its resends stop, but it was not recalled
  d.net.setFaults(d.lb.index(), d.la.index(), Faults{});
  d.step(3000);
  EXPECT_EQ(d.sb.keys(), (std::vector<uint32_t>{keyOf(stateFrame(s1, 1)), keyOf(stateFrame(s2, 2))}))
      << "v1 then v2, once each, in order";
  EXPECT_EQ(d.a.unacked(), 0u);
}

TEST(ReliableLinkTest, AnAbortFromAnUnknownPeerIsDeliveredAndRegistersNoPeer) {
  Solo x;
  ASSERT_TRUE(x.raw.put(abortFrame(), 0, NO_ACK));
  x.rl.tick();
  ASSERT_EQ(x.sink.frames.size(), 1u);
  EXPECT_EQ(x.sink.frames[0].type, Type::Abort);
  EXPECT_EQ(x.sink.frames[0].from, x.toRaw);
  x.step(11000);
  EXPECT_TRUE(x.sink.silent.empty()) << "no peer was registered to go silent";
  EXPECT_TRUE(x.raw.of(Type::Ping).empty());
  EXPECT_TRUE(x.raw.of(Type::Ack).empty()) << "and no keepalive goes to it";
}

TEST(ReliableLinkTest, APingOrAckFromAnUnknownPeerIsStrayAndRegistersNoPeer) {
  Solo x;
  ASSERT_TRUE(x.raw.putType(Type::Ping, 0, NO_ACK));
  ASSERT_TRUE(x.raw.putAck(NO_ACK));
  x.rl.tick();
  EXPECT_EQ(x.rl.stats().strayFrames, 2u);
  EXPECT_EQ(x.rl.stats().framesReceived, 0u);
  EXPECT_TRUE(x.sink.frames.empty()) << "neither is ever handed to the sink";
  x.step(11000);
  EXPECT_TRUE(x.sink.silent.empty()) << "no peer was registered to go silent";
  EXPECT_TRUE(x.raw.of(Type::Ping).empty());
  EXPECT_TRUE(x.raw.of(Type::Ack).empty()) << "and no keepalive goes to it";
}

TEST(ReliableLinkTest, AtMostMaxFramesPerTickAreTakenAndTheRestWaitForTheNextTick) {
  Solo x;
  const size_t flood = 2 * ReliableLink::MAX_FRAMES_PER_TICK + 5;
  for (size_t i = 0; i < flood; i++) ASSERT_TRUE(x.raw.putType(Type::Ping, 0, NO_ACK));
  x.rl.tick();
  EXPECT_EQ(x.rl.stats().strayFrames, ReliableLink::MAX_FRAMES_PER_TICK);
  x.rl.tick();
  EXPECT_EQ(x.rl.stats().strayFrames, 2 * ReliableLink::MAX_FRAMES_PER_TICK);
  x.rl.tick();
  EXPECT_EQ(x.rl.stats().strayFrames, flood);
}

}  // namespace
