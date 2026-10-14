#pragma once

// The rig the GameLink / NearbySession suites share (GameLinkTest, GameLinkStoreTest, GameLinkLogTest,
// NearbySessionTest, NearbyLeaveTest): two NearbySessions on their own GameLink tasks over a FakeLinkNet, a thread
// that stands in for the VM task, and the waits that move fake time.
//
// Test doubles (orchestrated-epics.md, Build-agent brief):
// - FakeRtosClock stands in for GameClock (src/games/GameClock.cpp, esp_timer_get_time / 1000): an IClock over
//   fakertos::millis(), the clock FakeRtos.h moves by hand and by every vTaskDelay.
//   GameLinkClockTest.FakeRtosClockAgreesWithGameClock pins that it reads the same milliseconds as GameClock itself.
//   More permissive than the device: it runs faster than real time while tasks sleep (every sleeping task adds its
//   wait, and waitFor adds 2 ms per real millisecond), so a bound in fake milliseconds is shorter in real time than on
//   a device.
// - ObservedLink stands in for EspNowLink (device) and UdpLink (simulator): it forwards to a FakeLink the test keeps,
//   records every end() and every ABORT frame sent, and refuses the next `refuseSends` sends like a full send queue
//   (ILink.h: send returns false, nothing sent). It also records each STATE frame the link took to send and each one it
//   received (ver and snapshot bytes, LinkProbe::statesSent and statesReceived): what a device's radio carried, which
//   the device shows only as `state sent` / `state recv` lines, so a test can compare the snapshot one device sent
//   with the one the other received (NearbyMatchTest.ARecordedStateSentIsTheStateThePeerReceived pins that the two
//   records of one frame are equal). NearbySessionTest pins that a destroyed session ends it. More
//   permissive than the device: FakeLink's (its header lists them), and a refusal is by count, not by a queue that
//   fills and drains. Thread-safe where FakeLink is not: the session's lock is the only caller of the inner link while
//   a session runs, and the probe's fields are atomic.
// - MatchDriver stands in for the VM task of one device: the test thread calling takeEnd, adoptNext, a Session over
//   CounterRules (test/game_core/MatchSupport.h), and ship, as MatchPeer::step does in one thread. More permissive than
//   the device: no Lua, no overlay, no loop task, and a step as often as the test calls it.
// - RawHostRig puts a LobbyTest::RawPeer (an oracle that speaks Protocol by hand, never acks or resends on its own) in
//   the host's place, so a test hands the guest exactly the frames it wants in the order it wants. More permissive than
//   a device: the host never resends, and its clock is a ManualClock the test leaves alone.
// - Duo is the two devices: a host and a guest NearbySession on one FakeLinkNet, brought up, joined, and started by the
//   lobby calls the lobby screen makes.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "../../game_core/FakeLink.h"
#include "../../game_core/LobbySupport.h"
#include "../../game_core/MatchSupport.h"
#include "ActivityManager.h"
#include "FakeRtos.h"
#include "GameLinkLog.h"
#include "HalMemoryStub.h"
#include "Logging.h"
#include "NearbySession.h"
#include "RenderLockProbe.h"

namespace nearby {

using namespace GameCore;
using GameCore::LinkTest::FakeLink;
using GameCore::LinkTest::FakeLinkNet;
using GameCore::LinkTest::Faults;
using GameCore::LobbyTest::FixedRandom;
using GameCore::MatchTest::CounterRules;

// Waits for `pred` for at most `capRealMs` of real time, polling every real millisecond and moving the fake clock by
// 2 ms on each poll, so a wait that counts fake time ends; never a fixed sleep. True once it held.
inline bool waitFor(const std::function<bool()>& pred, const int capRealMs = 10000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(capRealMs);
  while (!pred()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    fakertos::advance(2);
  }
  return true;
}

class FakeRtosClock final : public IClock {
 public:
  uint64_t nowMs() const override { return fakertos::millis(); }
};

// One STATE frame as the link carried it.
struct StateFrame {
  uint16_t ver = 0;
  std::vector<uint8_t> snapshot;
};

struct LinkProbe {
  std::atomic<int> endCalls{0};
  std::atomic<int> abortFrames{0};  // ABORT frames handed to send() or broadcast(), taken or refused
  std::atomic<int> sentFrames{0};   // frames the inner link took
  std::atomic<int> refused{0};      // sends refused by refuseSends
  std::atomic<size_t> refuseSends{0};

  // The STATE frames the inner link took to send (resends included) and the ones received, in order; copies, since
  // the GameLink task appends while the test reads.
  std::vector<StateFrame> statesSent() const {
    std::lock_guard<std::mutex> lock(statesMutex);
    return sent;
  }
  std::vector<StateFrame> statesReceived() const {
    std::lock_guard<std::mutex> lock(statesMutex);
    return received;
  }
  // GameLink task: records `frame` when it is a STATE.
  void noteState(const std::span<const uint8_t> frame, const bool wasSent) {
    Protocol::Frame decoded;
    if (Protocol::decode(frame, Protocol::ANY_SESSION, decoded) != Protocol::DecodeStatus::Ok ||
        decoded.header.type != Protocol::Type::State) {
      return;
    }
    StateFrame state;
    state.ver = decoded.state.ver;
    state.snapshot.assign(decoded.state.snapshot.begin(), decoded.state.snapshot.end());
    std::lock_guard<std::mutex> lock(statesMutex);
    (wasSent ? sent : received).push_back(std::move(state));
  }

 private:
  mutable std::mutex statesMutex;
  std::vector<StateFrame> sent;
  std::vector<StateFrame> received;
};

class ObservedLink final : public ILink {
 public:
  ObservedLink(FakeLink& inner, LinkProbe& probe) : inner(inner), probe(probe) {}

  bool send(const PeerHandle to, const std::span<const uint8_t> frame) override {
    noteAbort(frame);
    if (takeRefusal()) return false;
    const bool taken = inner.send(to, frame);
    if (taken) {
      ++probe.sentFrames;
      probe.noteState(frame, true);
    }
    return taken;
  }
  bool broadcast(const std::span<const uint8_t> frame) override {
    noteAbort(frame);
    if (takeRefusal()) return false;
    const bool taken = inner.broadcast(frame);
    if (taken) {
      ++probe.sentFrames;
      probe.noteState(frame, true);
    }
    return taken;
  }
  bool receive(PeerHandle& from, const std::span<uint8_t> buffer, size_t& length) override {
    const bool got = inner.receive(from, buffer, length);
    if (got) probe.noteState(buffer.first(length), false);
    return got;
  }
  void end() override {
    ++probe.endCalls;
    inner.end();
  }

 private:
  void noteAbort(const std::span<const uint8_t> frame) {
    Protocol::Frame decoded;
    if (Protocol::decode(frame, Protocol::ANY_SESSION, decoded) == Protocol::DecodeStatus::Ok &&
        decoded.header.type == Protocol::Type::Abort) {
      ++probe.abortFrames;
    }
  }
  bool takeRefusal() {
    size_t left = probe.refuseSends.load();
    while (left > 0 && !probe.refuseSends.compare_exchange_weak(left, left - 1)) {
    }
    if (left == 0) return false;
    ++probe.refused;
    return true;
  }

  FakeLink& inner;
  LinkProbe& probe;
};

// A STATE shipment with a one-byte snapshot, for a test that drives ship() by hand.
struct TestShipment {
  uint8_t byte = 0;
  Shipment shipment;
  TestShipment(const uint16_t ver, const bool over, const uint8_t turn = 1) : byte(static_cast<uint8_t>(ver)) {
    shipment.ver = ver;
    shipment.status.over = over;
    shipment.status.turn = over ? 0 : turn;
    shipment.status.winners = over ? 1 : 0;
    shipment.snapshot = std::span<const uint8_t>(&byte, 1);
  }
};

// The VM task of one device (see the header comment).
class MatchDriver {
 public:
  MatchDriver(NearbySession& session, const uint32_t seed, const uint16_t limit = 20)
      : net(session), rules(), match(session.roster(), rules), seat(session.localSeat()) {
    rules.rngState = seed * 2654435761u + 1u;
    rules.limit = limit;
  }

  void step() {
    Protocol::AbortReason reason;
    if (net.takeEnd(reason)) {
      ended = true;
      endReason = reason;
    }
    const NearbySession::AdoptStep adopted = net.adoptNext(match);
    if (adopted.taken) verdicts.push_back(adopted.result);
    if (playing && match.hasState() && !match.status().over && match.status().turn == seat && !match.pending()) {
      match.handle(MatchTest::tapEvent(), seat);
      match.applyPending();
      ++moves;
    }
    Shipment shipment;
    if (match.pendingShipment(shipment) && net.ship(shipment)) {
      match.shipped();
      ++shipments;
    }
  }
  // Seat 1 starts the round; deferred while a shipment is pending (entry 7, plan C6).
  bool startRound() {
    if (match.shipmentPending()) return false;
    match.start();
    return true;
  }

  NearbySession& net;
  CounterRules rules;
  Session match;
  uint8_t seat;
  bool playing = true;
  bool ended = false;
  Protocol::AbortReason endReason = Protocol::AbortReason::Left;
  uint32_t moves = 0, shipments = 0;
  std::vector<Adoption> verdicts;
};

inline bool sameState(const Session& a, const Session& b) {
  return a.hasState() && b.hasState() && a.ver() == b.ver() && a.status().over == b.status().over &&
         a.status().turn == b.status().turn && a.status().winners == b.status().winners &&
         std::equal(a.snapshot().begin(), a.snapshot().end(), b.snapshot().begin(), b.snapshot().end());
}

// The lines the GameLink task and NearbySession logged with the LINK origin.
inline size_t linkLines(const std::string& part = "") {
  size_t count = 0;
  for (const std::string& line : fakelog::snapshot()) {
    if (line.find("LINK:") != std::string::npos && line.find(part) != std::string::npos) ++count;
  }
  return count;
}

inline std::string sessionHex(const uint32_t session) {
  char text[16];
  std::snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(session));
  return text;
}

// No RenderLock taken or tried from any task thread, and no ActivityManager call (spine AD-18).
inline void expectNoRenderLockFromTasks() {
  EXPECT_EQ(fakelock::taskAcquisitions.load(), 0) << "a task thread took the RenderLock";
  EXPECT_EQ(fakelock::taskAttempts.load(), 0) << "a task thread tried the RenderLock";
  EXPECT_TRUE(activityManager.calls.empty()) << "an ActivityManager call was made";
}

// Settings for the suites: a silence limit and keepalive well inside a test's real time budget (fake milliseconds).
inline LobbySettings fastSettings(const uint32_t silenceMs = 2000) {
  LobbySettings settings;
  settings.link.silenceMs = silenceMs;
  settings.link.keepaliveMs = silenceMs / 4;
  return settings;
}
inline NearbySession::Options fastOptions() {
  NearbySession::Options options;
  options.stepMs = 2;
  return options;
}

// Two devices on one FakeLinkNet. Every reset the suites need happens in the constructor; the sessions are destroyed
// first (declared last), then the links and the net.
class Duo {
 public:
  explicit Duo(const uint32_t seed = 1) : net(clock, seed), hostEnd(net), guestEnd(net), random({0x5E551011u}) {
    fakertos::reset();
    fakepsram::reset();
    fakelog::clearLines();
    activityManager.reset();  // also resets the RenderLock probe
    game = LobbyTest::testGame();
  }
  ~Duo() {
    host.reset();
    guest.reset();
    fakertos::waitNoTasks();
  }

  // Both sessions, brought up with the lobby calls the screens would make.
  void openHost(const LobbySettings& settings = fastSettings(), const NearbySession::Options& options = fastOptions()) {
    host = NearbySession::openHost(std::make_unique<ObservedLink>(hostEnd, hostProbe), clock, random, game,
                                   LobbyTest::bytesOf("host"), settings, options);
  }
  void openGuest(const LobbySettings& settings = fastSettings(),
                 const NearbySession::Options& options = fastOptions()) {
    guest =
        NearbySession::openGuest(std::make_unique<ObservedLink>(guestEnd, guestProbe), clock, game, settings, options);
  }
  void openBoth(const LobbySettings& settings = fastSettings(), const NearbySession::Options& options = fastOptions()) {
    openHost(settings, options);
    openGuest(settings, options);
  }

  // The guest lists the host (an ADVERT), joins it, and the host seats it. False on a timeout.
  bool join() {
    if (!waitFor([&] { return guest->lobby().guest()->hostCount() >= 1; })) return false;
    {
      auto lobby = guest->lobby();
      GuestLobby& g = *lobby.guest();
      if (g.join(g.host(0).handle, g.host(0).session) != JoinResult::Sent) return false;
    }
    return waitFor([&] { return host->lobby().host()->seatsTaken() == 2; });
  }
  // The host starts, and the guest sees Start and begins its match. False on a timeout or a refused start.
  bool start() {
    if (host->startHost() != StartResult::Started) return false;
    if (!waitFor([&] { return guest->lobby().guest()->state() == GuestState::Started; })) return false;
    return guest->beginMatch();
  }
  bool joinAndStart() { return join() && start(); }

  FakeRtosClock clock;
  FakeLinkNet net;
  FakeLink hostEnd;
  FakeLink guestEnd;
  LinkProbe hostProbe, guestProbe;
  FixedRandom random;
  LobbyGame game;
  std::unique_ptr<NearbySession> host;
  std::unique_ptr<NearbySession> guest;
};

// A guest NearbySession whose host is a RawPeer: the guest lists the raw host, joins it, and is started by it. The raw
// host's frames after ACCEPT carry seq 1, 2, ... in the order putState is called.
class RawHostRig {
 public:
  static constexpr uint32_t SESSION = 0x48570001u;

  explicit RawHostRig(const uint32_t silenceMs = 600000)
      : net(clock), guestEnd(net), host(net, rawClock), silence(silenceMs) {
    fakertos::reset();
    fakepsram::reset();
    fakelog::clearLines();
    activityManager.reset();
    game = LobbyTest::testGame();
  }
  ~RawHostRig() {
    guest.reset();
    fakertos::waitNoTasks();
  }

  bool open() {
    guest = NearbySession::openGuest(std::make_unique<ObservedLink>(guestEnd, guestProbe), clock, game,
                                     fastSettings(silence), fastOptions());
    return guest != nullptr;
  }
  // Lists the raw host, joins it, has it ack the JOIN and accept; with `begin` the guest then begins its match.
  bool joinAndStart(const bool begin = true) {
    host.connect(guestEnd);
    const bool listed = waitFor([&] {
      host.broadcast(LobbyTest::advertFrameFor(game, 1, 2, LobbyTest::bytesOf("raw")), SESSION);
      return guest->lobby().guest()->hostCount() >= 1;
    });
    if (!listed) return false;
    {
      auto lobby = guest->lobby();
      GuestLobby& g = *lobby.guest();
      if (g.join(g.host(0).handle, g.host(0).session) != JoinResult::Sent) return false;
    }
    if (!waitFor([&] {
          host.drain();
          return host.count(Protocol::Type::Join) >= 1;
        })) {
      return false;
    }
    host.putAck(SESSION, 0);
    if (!waitFor([&] { return guest->lobby().guest()->state() == GuestState::Joined; })) return false;
    host.put(LobbyTest::acceptFrame(2, 2), SESSION, 0, 0);
    if (!waitFor([&] { return guest->lobby().guest()->state() == GuestState::Started; })) return false;
    return !begin || guest->beginMatch();
  }
  // A STATE from the raw host: a one-byte snapshot per `ver` when `snapshot` is empty.
  bool putState(const uint16_t ver, const bool over, const uint8_t turn = 2, std::vector<uint8_t> snapshot = {}) {
    if (snapshot.empty()) snapshot = CounterRules::stateOf(ver, turn, 1);
    Protocol::Frame frame;
    frame.header.type = Protocol::Type::State;
    frame.state.ver = ver;
    frame.state.status.over = over;
    frame.state.status.turn = over ? 0 : turn;
    frame.state.status.winners = over ? 1 : 0;
    frame.state.snapshot = std::span<const uint8_t>(snapshot.data(), snapshot.size());
    return host.put(frame, SESSION, nextSeq++);
  }
  bool putAbort(const Protocol::AbortReason reason) { return host.put(LobbyTest::abortFrame(reason), SESSION); }

  FakeRtosClock clock;
  LinkTest::ManualClock rawClock;
  FakeLinkNet net;
  FakeLink guestEnd;
  LinkProbe guestProbe;
  LobbyTest::RawPeer host;
  LobbyGame game;
  uint32_t silence;
  uint16_t nextSeq = 1;
  std::unique_ptr<NearbySession> guest;
};

}  // namespace nearby
