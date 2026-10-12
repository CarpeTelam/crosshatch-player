#pragma once

// The rig the lobby suites share (LobbyTest.cpp, LobbyHandshakeTest.cpp): a ManualClock, a seeded FakeLinkNet, and the
// lobbies stepped together in fake time.
//
// Test doubles (orchestrated-epics.md, Build-agent brief):
// - RawPeer stands in for a remote device speaking Protocol by hand: an oracle independent of the lobbies and of
//   ReliableLink. It decodes what it receives and puts hand-built frames; it never acks, resends, or keeps alive on
//   its own. More permissive than a device: it has no peer table and no send queue.
// - FixedRandom stands in for the device's IRandom (a scripted list of values).
// - RecSink stands in for the match session's Sink behind a lobby hand-off: it records what it is given, with no
//   queue limit (the match session's queue is bounded) and a scripted verdict.
// - Rig::advance steps the clock 10 ms at a time and ticks every tracked lobby once per step, in the order they were
//   tracked: a stand-in for the GameLink task's loop (the device ticks as often as its task runs). Every bound in the
//   lobby suites is in fake milliseconds from LobbySettings, never host speed.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FakeLink.h"
#include "GuestLobby.h"
#include "HostLobby.h"
#include "Lobby.h"
#include "Protocol.h"

namespace GameCore::LobbyTest {

using namespace GameCore;
using namespace GameCore::LinkTest;

using Bytes = std::vector<uint8_t>;
using Protocol::AbortReason;
using Protocol::Frame;
using Protocol::Type;

inline constexpr uint32_t SURFACE = 0xEE049B84u;
inline constexpr uint16_t NO_ACK = 0xFFFF;  // what a peer that has received nothing puts in `ack`
inline constexpr uint32_t STEP_MS = 10;

inline LobbyGame testGame(const int32_t maxSeats = 2, const int32_t api = 1) {
  LobbyGame game;
  game.setGameId("ttt");
  for (size_t i = 0; i < game.pkgHash.size(); i++) game.pkgHash[i] = static_cast<uint8_t>(0xA0 + i);
  game.manifestApi = 1;
  game.seatsMin = 2;
  game.seatsMax = maxSeats;
  game.surface = SURFACE;
  game.apiPreview = true;
  game.caps.api = api;
  game.caps.minApi = 1;
  game.caps.maxSeats = maxSeats;
  game.caps.nearby = true;
  return game;
}

inline Faults hostile() {
  Faults f;
  f.lossPercent = 30;
  f.duplicatePercent = 20;
  f.reorderPercent = 30;
  f.delayMs = 20;
  f.jitterMs = 30;
  f.reorderExtraMs = 100;
  return f;
}
// The longest a copy waits on a hostile net: delay + jitter + the reorder extra.
inline constexpr uint32_t HOSTILE_MAX_DELAY_MS = 20 + 30 + 100;

inline Bytes bytesOf(const char* text) {
  Bytes out;
  for (; *text != '\0'; ++text) out.push_back(static_cast<uint8_t>(*text));
  return out;
}

class FixedRandom final : public IRandom {
 public:
  explicit FixedRandom(std::vector<uint32_t> values) : values_(std::move(values)) {}
  uint32_t next32() override {
    draws++;
    if (next_ < values_.size()) return values_[next_++];
    return values_.empty() ? 0 : values_.back();
  }
  size_t draws = 0;

 private:
  std::vector<uint32_t> values_;
  size_t next_ = 0;
};

class Rig {
 public:
  explicit Rig(const uint32_t seed = 1) : net(clock, seed) {}

  template <class Lobby>
  void track(Lobby& lobby) {
    tickers_.push_back({&lobby, [&lobby] { lobby.tick(); }});
  }
  void untrack(const void* lobby) {
    for (size_t i = 0; i < tickers_.size(); i++) {
      if (tickers_[i].first == lobby) {
        tickers_.erase(tickers_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
  }
  // Steps `ms` (a multiple of STEP_MS) of fake time; `each` runs after every step.
  void advance(const uint32_t ms, const std::function<void()>& each = {}) {
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
      clock.advance(STEP_MS);
      for (size_t i = 0; i < tickers_.size(); i++) tickers_[i].second();
      if (onStep) onStep();
      if (each) each();
    }
  }
  // Checks `pred` before every step, for at most `maxMs`; true once it held.
  bool until(const std::function<bool()>& pred, const uint32_t maxMs) {
    for (uint32_t t = 0; t <= maxMs; t += STEP_MS) {
      if (pred()) return true;
      advance(STEP_MS);
    }
    return false;
  }
  uint64_t now() const { return clock.nowMs(); }

  ManualClock clock;
  FakeLinkNet net;
  std::function<void()> onStep;  // after every step's ticks, for a suite that watches a link's counters

 private:
  std::vector<std::pair<const void*, std::function<void()>>> tickers_;
};

// What a RawPeer decoded, flattened.
struct Seen {
  Type type = Type::Ping;
  uint32_t session = 0;
  uint16_t seq = 0;
  uint16_t ack = 0;
  AbortReason reason = AbortReason::Left;
  uint8_t seat = 0;
  uint8_t n = 0;
  std::string gameId;
  std::array<uint8_t, Protocol::PKG_HASH_BYTES> pkgHash{};
  uint8_t manifestApi = 0;
  uint8_t hostApi = 0;
  uint32_t surface = 0;
  uint8_t seatsMax = 0;
  uint8_t seatsTaken = 0;
  Bytes label;
  uint64_t atMs = 0;
};

inline Frame abortFrame(const AbortReason reason) {
  Frame f;
  f.header.type = Type::Abort;
  f.abortReason = reason;
  return f;
}
inline Frame joinFrameFor(const LobbyGame& game) {
  Frame f;
  f.header.type = Type::Join;
  f.join.pkgHash = game.pkgHash;
  f.join.hostApi = static_cast<uint8_t>(game.caps.api);
  f.join.surface = game.surface;
  return f;
}
inline Frame acceptFrame(const uint8_t seat, const uint8_t n) {
  Frame f;
  f.header.type = Type::Accept;
  f.accept.seat = seat;
  f.accept.n = n;
  return f;
}
// `label` must outlive the frame.
inline Frame advertFrameFor(const LobbyGame& game, const uint8_t seatsTaken, const uint8_t seatsMax,
                            const Bytes& label = {}) {
  Frame f;
  f.header.type = Type::Advert;
  f.advert.gameId = std::string_view(game.gameId);
  f.advert.pkgHash = game.pkgHash;
  f.advert.manifestApi = game.manifestApi;
  f.advert.hostApi = static_cast<uint8_t>(game.caps.api);
  f.advert.surface = game.surface;
  f.advert.seatsMax = seatsMax;
  f.advert.seatsTaken = seatsTaken;
  f.advert.hostLabel = std::span<const uint8_t>(label.data(), label.size());
  return f;
}
// `snapshot` must outlive the frame.
inline Frame stateFrame(const Bytes& snapshot, const uint16_t ver = 1) {
  Frame f;
  f.header.type = Type::State;
  f.state.ver = ver;
  f.state.status.turn = 1;
  f.state.snapshot = std::span<const uint8_t>(snapshot.data(), snapshot.size());
  return f;
}

class RawPeer {
 public:
  RawPeer(FakeLinkNet& net, const ManualClock& c) : link(net), clock(c), buffer(Protocol::MAX_FRAME_BYTES) {}

  void connect(FakeLink& under) { to = link.handleOf(under); }

  // Takes everything waiting into `seen`.
  void drain() {
    PeerHandle from;
    size_t length = 0;
    while (link.receive(from, buffer, length)) {
      Frame f;
      if (Protocol::decode(std::span<const uint8_t>(buffer.data(), length), Protocol::ANY_SESSION, f) !=
          Protocol::DecodeStatus::Ok) {
        undecodable++;
        continue;
      }
      Seen s;
      s.type = f.header.type;
      s.session = f.header.session;
      s.seq = f.header.seq;
      s.ack = f.header.ack;
      s.atMs = clock.nowMs();
      if (f.header.type == Type::Abort) s.reason = f.abortReason;
      if (f.header.type == Type::Accept) {
        s.seat = f.accept.seat;
        s.n = f.accept.n;
      }
      if (f.header.type == Type::Advert) {
        s.gameId = std::string(f.advert.gameId);
        s.pkgHash = f.advert.pkgHash;
        s.manifestApi = f.advert.manifestApi;
        s.hostApi = f.advert.hostApi;
        s.surface = f.advert.surface;
        s.seatsMax = f.advert.seatsMax;
        s.seatsTaken = f.advert.seatsTaken;
        s.label.assign(f.advert.hostLabel.begin(), f.advert.hostLabel.end());
      }
      seen.push_back(s);
    }
  }
  std::vector<Seen> of(const Type type) const {
    std::vector<Seen> out;
    for (const Seen& s : seen) {
      if (s.type == type) out.push_back(s);
    }
    return out;
  }
  size_t count(const Type type) const { return of(type).size(); }
  size_t countAborts(const AbortReason reason) const {
    size_t n = 0;
    for (const Seen& s : seen) n += s.type == Type::Abort && s.reason == reason ? 1 : 0;
    return n;
  }
  size_t countAborts() const { return count(Type::Abort); }

  bool put(Frame f, const uint32_t session, const uint16_t seq = 0, const uint16_t ack = NO_ACK) {
    const Bytes bytes = encoded(std::move(f), session, seq, ack);
    return !bytes.empty() && link.send(to, bytes);
  }
  bool broadcast(Frame f, const uint32_t session) {
    const Bytes bytes = encoded(std::move(f), session, 0, 0);
    return !bytes.empty() && link.broadcast(bytes);
  }
  bool putBytes(const Bytes& bytes) { return link.send(to, bytes); }
  bool broadcastBytes(const Bytes& bytes) { return link.broadcast(bytes); }
  bool putAck(const uint32_t session, const uint16_t ack) {
    Frame f;
    f.header.type = Type::Ack;
    return put(f, session, 0, ack);
  }
  static Bytes encoded(Frame f, const uint32_t session, const uint16_t seq, const uint16_t ack) {
    f.header.session = session;
    f.header.seq = seq;
    f.header.ack = ack;
    Bytes out(Protocol::MAX_FRAME_BYTES);
    const size_t n = Protocol::encode(out, f);
    out.resize(n);
    return out;
  }

  FakeLink link;
  PeerHandle to;
  const ManualClock& clock;
  Bytes buffer;
  std::vector<Seen> seen;
  size_t undecodable = 0;
};

// Records what a lobby hands the match.
class RecSink final : public ReliableLink::Sink {
 public:
  struct Rec {
    Type type = Type::Ping;
    PeerHandle from;
    AbortReason reason = AbortReason::Left;
    uint16_t ver = 0;
    uint32_t session = 0;
    uint64_t atMs = 0;
  };
  using Verdict = ReliableLink::Verdict;
  explicit RecSink(const ManualClock& c) : clock(c) {}

  Verdict onFrame(const PeerHandle from, const Frame& frame) override {
    Rec r;
    r.type = frame.header.type;
    r.from = from;
    r.reason = frame.abortReason;
    r.ver = frame.state.ver;
    r.session = frame.header.session;
    r.atMs = clock.nowMs();
    frames.push_back(r);
    return Verdict::Taken;
  }
  void onPeerSilent(const PeerHandle peer) override {
    silent.push_back(peer);
    silentAt.push_back(clock.nowMs());
  }
  size_t count(const Type type) const {
    size_t n = 0;
    for (const Rec& r : frames) n += r.type == type ? 1 : 0;
    return n;
  }

  const ManualClock& clock;
  std::vector<Rec> frames;
  std::vector<PeerHandle> silent;
  std::vector<uint64_t> silentAt;
};

inline constexpr uint32_t HOST_SESSION = 0x1234ABCDu;

// A host lobby on a quiet net with hand-built guests.
struct HostRig {
  explicit HostRig(const int32_t maxSeats = 2, const Bytes& labelBytes = bytesOf("host"), const LobbySettings& s = {})
      : game(testGame(maxSeats)),
        link(rig.net),
        random({0x5E551011u}),
        host(link, rig.clock, random, game, labelBytes, s),
        observer(rig.net, rig.clock) {
    rig.track(host);
  }
  RawPeer& addPeer() {
    peers.emplace_back(rig.net, rig.clock);
    peers.back().connect(link);
    return peers.back();
  }
  void drainAll() {
    observer.drain();
    for (RawPeer& p : peers) p.drain();
  }
  void run(const uint32_t ms) {
    rig.advance(ms, [this] { drainAll(); });
  }
  uint32_t session() const { return host.session(); }
  bool join(RawPeer& peer, const uint16_t seq = 0) { return peer.put(joinFrameFor(game), session(), seq); }
  std::vector<HostEvent> events() {
    std::vector<HostEvent> out;
    HostEvent e;
    while (host.poll(e)) out.push_back(e);
    return out;
  }

  Rig rig;
  LobbyGame game;
  FakeLink link;
  FixedRandom random;
  HostLobby host;
  RawPeer observer;  // hears every broadcast, as a third device in range
  std::deque<RawPeer> peers;
};

// A guest lobby with hand-built hosts.
struct GuestRig {
  explicit GuestRig(const LobbyGame& g = testGame(), const LobbySettings& s = {})
      : game(g), link(rig.net), guest(link, rig.clock, game, s) {
    rig.track(guest);
  }
  RawPeer& addHost() {
    hosts.emplace_back(rig.net, rig.clock);
    hosts.back().connect(link);
    return hosts.back();
  }
  void drainAll() {
    for (RawPeer& p : hosts) p.drain();
  }
  void run(const uint32_t ms) {
    rig.advance(ms, [this] { drainAll(); });
  }
  void advertise(RawPeer& host, const uint32_t session, const uint8_t taken = 1, const Bytes& label = bytesOf("raw")) {
    ASSERT_TRUE(host.broadcast(advertFrameFor(game, taken, 2, label), session));
    run(STEP_MS);
  }
  // Lists `host`, joins it, and has it ack the JOIN: the guest is Joined.
  void joinAndAck(RawPeer& host, const uint32_t session) {
    advertise(host, session);
    ASSERT_GE(guest.hostCount(), 1u);
    ASSERT_EQ(guest.join(guest.host(0).handle, session), JoinResult::Sent);
    run(STEP_MS);
    ASSERT_TRUE(host.putAck(session, 0));
    run(STEP_MS);
    ASSERT_EQ(guest.state(), GuestState::Joined);
  }

  Rig rig;
  LobbyGame game;
  FakeLink link;
  GuestLobby guest;
  std::deque<RawPeer> hosts;
};

}  // namespace GameCore::LobbyTest
