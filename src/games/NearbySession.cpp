#if FREEINK_CAP_GAMES

#include "NearbySession.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <new>
#include <optional>
#include <utility>

#include "GameLinkLog.h"
#include "GameLinkSinkGate.h"
#include "GameLinkStore.h"

using namespace GameCore;

namespace {
// leave() polls the unacked count this often (ms), and at most `flushMs / LEAVE_POLL_MS + LEAVE_EXTRA_POLLS` times, so
// a clock that never moves cannot hang it.
constexpr uint32_t LEAVE_POLL_MS = 5;
constexpr uint32_t LEAVE_EXTRA_POLLS = 2;
}  // namespace

// Everything the GameLink task touches, behind a pointer so a task that will not stop can be leaked without freeing it.
// Members are declared in destruction-safe order (destroyed last to first): the link outlives the lobby that holds a
// reference to it, the pool outlives the queue that returns its buffers to it, and the task is destroyed first.
struct NearbySession::Core final : public GameLinkWork {
  Core(std::unique_ptr<ILink> linkIn, const IClock& clockIn, const Role roleIn)
      : link(std::move(linkIn)), clock(clockIn), role(roleIn) {}

  // ---- GameLinkWork: the GameLink task ----
  void linkStep() override;
  void linkStopped(uint32_t stackHighWaterBytes) override;

  // ---- everything below is touched only with `mutex` held, except where it says otherwise ----
  ReliableLink& reliable() { return host ? host->link() : guest->link(); }
  uint32_t sessionId() const { return host ? host->session() : guest->session(); }
  bool beginMatchLocked();
  void shipStep();
  void reportStats();

  // The slot ship() stages the next shipment in, in the pool's outbound buffer.
  struct Outbound {
    enum class State : uint8_t { Empty, Staged, Taken };
    State state = State::Empty;
    uint16_t ver = 0;
    Status status;
    size_t length = 0;
    uint32_t sentMask = 0;  // bit i: the link took the STATE for peers[i]
  };

  std::unique_ptr<ILink> link;
  const IClock& clock;
  const Role role;
  mutable std::mutex mutex;
  std::unique_ptr<GameLinkStore> store;
  std::unique_ptr<StateQueue> queue;
  EndSlot end;  // lock-free
  std::optional<GameLinkSinkGate> gate;
  std::unique_ptr<HostLobby> host;
  std::unique_ptr<GuestLobby> guest;
  Roster matchRoster;
  uint8_t matchSeat = 0;
  std::array<SeatPeer, Roster::MAX_SEATS> peers{};
  size_t peerCount = 0;
  Outbound out;
  ReliableLink::Stats reported;
  bool startLogged = false;
  bool shipRefusedLogged = false;
  std::atomic<bool> begun{false};
  std::atomic<uint32_t> sessionNow{0};  // the lobby's session id as of the last step: readable without the lock
  GameLink task{*this};
};

void NearbySession::Core::linkStep() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!startLogged) {
    startLogged = true;
    GameLinkLog::taskStarted(sessionId(), GameLink::TASK_STACK_BYTES);
  }
  if (host) {
    host->tick();
  } else {
    guest->tick();
  }
  sessionNow.store(sessionId(), std::memory_order_release);
  if (begun.load(std::memory_order_relaxed)) shipStep();
  reportStats();
}

void NearbySession::Core::linkStopped(const uint32_t stackHighWaterBytes) {
  std::lock_guard<std::mutex> lock(mutex);
  reportStats();
  GameLinkLog::taskStopped(sessionId(), stackHighWaterBytes);
}

bool NearbySession::Core::beginMatchLocked() {
  if (begun.load(std::memory_order_relaxed)) return true;
  uint8_t mySeat = 1;
  if (host) {
    if (host->state() != HostState::Started) return false;
    matchRoster = host->roster();
  } else {
    if (guest->state() != GuestState::Started) return false;
    matchRoster = guest->roster();
    mySeat = guest->seat();
  }
  matchSeat = mySeat;
  peerCount = 0;
  for (uint8_t seat = 1; seat <= matchRoster.seats && seat <= Roster::MAX_SEATS; ++seat) {
    if (seat == mySeat) continue;
    const PeerHandle handle = host ? host->peerForSeat(seat) : guest->peerForSeat(seat);
    if (handle.valid() && peerCount < peers.size()) peers[peerCount++] = SeatPeer{handle, seat};
  }
  gate.emplace(*queue, end, &reliable(), sessionId(), std::span<const SeatPeer>(peers.data(), peerCount));
  // Replays an ABORT or silence the lobby latched before this point.
  if (host) {
    host->handOff(*gate);
  } else {
    guest->handOff(*gate);
  }
  begun.store(true, std::memory_order_release);
  return true;
}

// GameLink task: offers the staged STATE to every peer the link has not yet taken it for.
void NearbySession::Core::shipStep() {
  if (out.state != Outbound::State::Staged) return;
  Protocol::Frame frame;
  frame.header.type = Protocol::Type::State;
  frame.state.ver = out.ver;
  frame.state.status = out.status;
  frame.state.snapshot = std::span<const uint8_t>(store->outbound().data(), out.length);
  ReliableLink& reliableLink = reliable();
  bool all = true;
  for (size_t i = 0; i < peerCount; ++i) {
    const uint32_t bit = uint32_t{1} << i;
    if ((out.sentMask & bit) != 0) continue;
    if (reliableLink.send(peers[i].handle, frame)) {
      out.sentMask |= bit;
    } else {
      all = false;  // an ordinary loss (the link's queue is full, or the adapter refused): retried next step
    }
  }
  if (!all) return;
  out.state = Outbound::State::Taken;
  GameLinkLog::stateSent(sessionId(), out.ver);
}

// Logs each counter of ReliableLink::Stats that moved since the last report, once.
void NearbySession::Core::reportStats() {
  const ReliableLink::Stats& now = reliable().stats();
  const uint32_t id = sessionId();
  if (now.resends != reported.resends) GameLinkLog::resent(id, now.resends - reported.resends, now.resends);
  if (now.silentReports != reported.silentReports) {
    GameLinkLog::silent(id, now.silentReports - reported.silentReports, now.silentReports);
  }
  if (now.abortsSent != reported.abortsSent) {
    GameLinkLog::abortSent(id, now.abortsSent - reported.abortsSent, now.abortsSent);
  }
  if (now.linkRefused != reported.linkRefused) {
    GameLinkLog::linkRefused(id, now.linkRefused - reported.linkRefused, now.linkRefused);
  }
  reported = now;
}

NearbySession::NearbySession(std::unique_ptr<Core> corePtr) : core(std::move(corePtr)) {}

NearbySession::~NearbySession() {
  if (!core) return;
  if (!core->task.stop(joinMs)) {
    // The task still runs and may hold the lock or the link: freeing either under it is worse than a leak. The link is
    // not ended either, for the same reason.
    GameLinkLog::taskLeaked(core->sessionNow.load(std::memory_order_acquire));
    (void)core.release();
    return;
  }
  core->link->end();
}

std::unique_ptr<NearbySession> NearbySession::finish(std::unique_ptr<Core> core, const Options& options) {
  ILink* const rawLink = core->link.get();
  std::unique_ptr<NearbySession> created(new (std::nothrow) NearbySession(std::move(core)));
  if (!created) {
    rawLink->end();  // the constructor did not run, so the Core moved in is destroyed with its link on return
    GameLinkLog::bringUpFailed(0, "no memory for the session");
    return nullptr;
  }
  created->joinMs = options.joinMs;
  if (!created->core->task.start(options.stepMs)) {
    GameLinkLog::bringUpFailed(created->core->sessionNow.load(std::memory_order_acquire), "cannot create the task");
    return nullptr;  // the destructor ends the link
  }
  return created;
}

std::unique_ptr<NearbySession> NearbySession::openHost(std::unique_ptr<ILink> link, const IClock& clock,
                                                       IRandom& random, const LobbyGame& game,
                                                       const std::span<const uint8_t> label,
                                                       const LobbySettings& settings, const Options& options) {
  if (!link) {
    GameLinkLog::bringUpFailed(0, "no link");
    return nullptr;
  }
  ILink* const rawLink = link.get();
  std::unique_ptr<Core> core = makeUniqueNoThrow<Core>(std::move(link), clock, Role::Host);
  if (!core) {
    rawLink->end();  // not consumed: the allocation failed before the constructor, so `link` still owns it
    GameLinkLog::bringUpFailed(0, "no memory for the session");
    return nullptr;
  }
  const char* failed = nullptr;
  core->store = GameLinkStore::create();
  if (!core->store || !core->store->ok()) {
    failed = "no PSRAM for the pool";
  } else {
    core->queue = makeUniqueNoThrow<StateQueue>(*core->store);
    if (!core->queue || !core->queue->ok()) {
      failed = "no memory for the queue";
    } else {
      core->host = makeUniqueNoThrow<HostLobby>(*core->link, clock, random, game, label, settings);
      if (!core->host || !core->host->ok()) failed = "no memory for the lobby";
    }
  }
  if (failed != nullptr) {
    core->link->end();
    GameLinkLog::bringUpFailed(0, failed);
    return nullptr;
  }
  core->sessionNow.store(core->host->session(), std::memory_order_release);
  return finish(std::move(core), options);
}

std::unique_ptr<NearbySession> NearbySession::openGuest(std::unique_ptr<ILink> link, const IClock& clock,
                                                        const LobbyGame& game, const LobbySettings& settings,
                                                        const Options& options) {
  if (!link) {
    GameLinkLog::bringUpFailed(0, "no link");
    return nullptr;
  }
  ILink* const rawLink = link.get();
  std::unique_ptr<Core> core = makeUniqueNoThrow<Core>(std::move(link), clock, Role::Guest);
  if (!core) {
    rawLink->end();
    GameLinkLog::bringUpFailed(0, "no memory for the session");
    return nullptr;
  }
  const char* failed = nullptr;
  core->store = GameLinkStore::create();
  if (!core->store || !core->store->ok()) {
    failed = "no PSRAM for the pool";
  } else {
    core->queue = makeUniqueNoThrow<StateQueue>(*core->store);
    if (!core->queue || !core->queue->ok()) {
      failed = "no memory for the queue";
    } else {
      core->guest = makeUniqueNoThrow<GuestLobby>(*core->link, clock, game, settings);
      if (!core->guest || !core->guest->ok()) failed = "no memory for the lobby";
    }
  }
  if (failed != nullptr) {
    core->link->end();
    GameLinkLog::bringUpFailed(0, failed);
    return nullptr;
  }
  return finish(std::move(core), options);
}

NearbySession::Role NearbySession::role() const { return core->role; }

uint32_t NearbySession::session() const { return core->sessionNow.load(std::memory_order_acquire); }

bool NearbySession::taskRunning() const { return core->task.running(); }

NearbySession::Lobby NearbySession::lobby() {
  std::unique_lock<std::mutex> lock(core->mutex);
  return Lobby(std::move(lock), core->host.get(), core->guest.get());
}

StartResult NearbySession::startHost() {
  std::lock_guard<std::mutex> lock(core->mutex);
  if (!core->host) return StartResult::NotWaiting;
  const StartResult result = core->host->start();
  if (result == StartResult::Started) core->beginMatchLocked();
  return result;
}

bool NearbySession::beginMatch() {
  std::lock_guard<std::mutex> lock(core->mutex);
  return core->beginMatchLocked();
}

bool NearbySession::matchBegun() const { return core->begun.load(std::memory_order_acquire); }

Roster NearbySession::roster() const {
  std::lock_guard<std::mutex> lock(core->mutex);
  return core->matchRoster;
}

uint8_t NearbySession::localSeat() const {
  std::lock_guard<std::mutex> lock(core->mutex);
  return core->matchSeat;
}

bool NearbySession::takeState(NearbyState& out) {
  std::lock_guard<std::mutex> lock(core->mutex);
  QueuedState taken;
  if (!core->queue->take(taken)) return false;
  out.queued = taken;
  out.peer = PeerState{taken.fromSeat, taken.ver, taken.status, core->queue->bytes(taken)};
  return true;
}

void NearbySession::releaseState(NearbyState& state) {
  std::lock_guard<std::mutex> lock(core->mutex);
  core->queue->release(state.queued);  // a no-op for a handle already returned
  state.peer.snapshot = {};
}

NearbySession::AdoptStep NearbySession::adoptNext(Session& matchSession) {
  AdoptStep step;
  if (matchSession.pending()) return step;
  NearbyState state;
  if (!takeState(state)) return step;
  step.taken = true;
  step.ver = state.peer.ver;
  step.fromSeat = state.peer.fromSeat;
  step.outcome = matchSession.adopt(state.peer, step.result);  // outside the lock: it may run rules
  releaseState(state);
  GameLinkLog::stateAdopted(session(), step.ver, step.fromSeat, step.result);
  return step;
}

bool NearbySession::ship(const Shipment& shipment) {
  std::lock_guard<std::mutex> lock(core->mutex);
  Core::Outbound& slot = core->out;
  if (shipment.snapshot.empty() || shipment.snapshot.size() > GameLinkStore::BUFFER_BYTES) {
    if (!core->shipRefusedLogged) {
      core->shipRefusedLogged = true;
      GameLinkLog::shipRefused(core->sessionId(), static_cast<uint32_t>(shipment.snapshot.size()));
    }
    return false;
  }
  if (slot.state == Core::Outbound::State::Taken && slot.ver == shipment.ver) {
    slot = Core::Outbound{};  // the link took it: answered once
    return true;
  }
  if (slot.state == Core::Outbound::State::Staged && (slot.ver == shipment.ver || slot.status.over)) {
    return false;  // already staged, or an `over` that no other version replaces
  }
  const std::span<uint8_t> buffer = core->store->outbound();
  std::copy(shipment.snapshot.begin(), shipment.snapshot.end(), buffer.begin());
  slot.state = Core::Outbound::State::Staged;
  slot.ver = shipment.ver;
  slot.status = shipment.status;
  slot.length = shipment.snapshot.size();
  slot.sentMask = 0;
  return false;
}

bool NearbySession::takeEnd(Protocol::AbortReason& reason) { return core->end.take(reason); }

bool NearbySession::endLatched() const { return core->end.latched(); }

NearbySession::LeaveResult NearbySession::leave(const Protocol::AbortReason reason, const uint32_t flushMs) {
  LeaveResult result;
  Core& c = *core;
  if (!c.begun.load(std::memory_order_acquire)) return result;
  if (flushMs > 0) {
    const uint64_t startedAt = c.clock.nowMs();
    const uint32_t polls = flushMs / LEAVE_POLL_MS + LEAVE_EXTRA_POLLS;
    for (uint32_t poll = 0; poll < polls; ++poll) {
      {
        std::lock_guard<std::mutex> lock(c.mutex);
        // Nothing unacked, and the last shipment is with the link (a staged one is not counted by unacked() yet).
        if (c.reliable().unacked() == 0 && c.out.state != Core::Outbound::State::Staged) break;
      }
      if (elapsed(c.clock.nowMs(), startedAt, flushMs)) break;
      GameLink::sleepMs(LEAVE_POLL_MS);
    }
    const uint64_t now = c.clock.nowMs();
    result.waitedMs = now > startedAt ? static_cast<uint32_t>(std::min<uint64_t>(now - startedAt, UINT32_MAX)) : 0;
  }
  std::lock_guard<std::mutex> lock(c.mutex);
  ReliableLink& link = c.reliable();
  result.unackedAtSend = link.unacked();
  Protocol::Frame abort;
  abort.header.type = Protocol::Type::Abort;
  abort.abortReason = reason;
  // ABORT is unsequenced and ignores anything unacked, so it is sent CANCEL_ABORT_COPIES times (crossplay
  // LinkSession.cpp kByeRepeats): the peer notices a lost one only as silence. Not to a seat whose ABORT the gate
  // heard: its session is over (per seat, never assuming the match has one other seat). A copy counts only when it went
  // to at least one peer.
  for (uint8_t copy = 0; copy < CANCEL_ABORT_COPIES; ++copy) {
    bool all = true;
    bool any = false;
    for (size_t i = 0; i < c.peerCount; ++i) {
      if (c.gate && c.gate->abortHeardFrom(c.peers[i].seat)) continue;
      any = true;
      all = link.send(c.peers[i].handle, abort) && all;
    }
    if (any && all) ++result.copies;
  }
  GameLinkLog::leaveSent(c.sessionId(), reason, result.waitedMs, static_cast<uint32_t>(result.unackedAtSend),
                         result.copies);
  c.reportStats();
  return result;
}

NearbySession::Counters NearbySession::counters() const {
  std::lock_guard<std::mutex> lock(core->mutex);
  Counters counts;
  counts.link = core->reliable().stats();
  counts.unacked = core->reliable().unacked();
  counts.queueOverflows = core->queue->overflows();
  counts.queueSize = core->queue->size();
  counts.endIgnored = core->end.ignored();
  if (core->gate) {
    counts.queued = core->gate->stats().queued;
    counts.replaced = core->gate->stats().replaced;
    counts.strangers = core->gate->stats().strangers;
  }
  return counts;
}

#endif  // FREEINK_CAP_GAMES
