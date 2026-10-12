#pragma once

#include <EndSlot.h>
#include <GuestLobby.h>
#include <HostLobby.h>
#include <IClock.h>
#include <ILink.h>
#include <IRandom.h>
#include <Lobby.h>
#include <Protocol.h>
#include <ReliableLink.h>
#include <Roster.h>
#include <Session.h>
#include <StateQueue.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

#include "GameLink.h"

// Declared outside the class so the factories can default it: a nested struct with default member initializers cannot
// be a default argument of its own enclosing class.
struct NearbySessionOptions {
  uint32_t stepMs = GameLink::STEP_MS;  // the task's wait between steps
  uint32_t joinMs = GameLink::JOIN_MS;  // how long the destructor waits for the task to end
};

// One nearby match's link side (spine AD-18, R5, R7, R13), written against GameCore::ILink: it owns the link handed in,
// the lobby (and the ReliableLink the lobby owns), the match's half (sink gate, StateQueue, EndSlot), the PSRAM pool of
// snapshot buffers, and the GameLink task that steps them. It is passed by move into the match (AD-20).
//
// It never takes RenderLock and never touches activity state: the task only steps this object, which calls nothing in
// the match.
//
// ONE LOCK. Every call on the link, the lobby, the ReliableLink, the sink, the queue, and the pool is serialized by one
// std::mutex (the queue is not thread-safe). The task takes it for one step; each public call below takes it for its
// own short body; none holds it across a Lua call, a render, or a wait. EndSlot is lock-free and is read without it.
// "Lobby task" below means the activity's loop task that draws the lobby; "VM task" means the task that runs the match.
//
// THE THREE HANDOFFS.
//  - Receive (GameLink task to VM task): the sink gate queues the peer's STATEs; takeState / adoptNext take them.
//  - Send (VM task to GameLink task): ship() stages the Session's shipment; the task sends it and ship() answers true
//    once the link took it.
//  - End (GameLink task to VM task): the first reason the match is over (an ABORT, or a silent peer) is latched in the
//    EndSlot; takeEnd returns it once.
//
// Lifetime: the caller keeps the IClock (and the IRandom of a host) alive for the session's life. The destructor stops
// and joins the task, then ends the link and frees the pool; a task that will not stop leaks its state rather than
// freeing what it touches (logged). Call it on a task that may wait up to Options::joinMs. It sends no goodbye: in the
// lobby phase that is the lobby's cancel() (call it first when players are seated), in a match it is leave().
class NearbySession {
 public:
  using Options = NearbySessionOptions;
  enum class Role : uint8_t { Host, Guest };

  // One STATE taken from the queue. `peer.snapshot` aliases the pool buffer, which only the consumer touches until
  // releaseState() returns it.
  struct NearbyState {
    GameCore::PeerState peer;
    GameCore::QueuedState queued;
  };
  // What adoptNext did. `taken` false: nothing was taken (a move is pending, or the queue was empty).
  struct AdoptStep {
    bool taken = false;
    GameCore::Adoption result = GameCore::Adoption::NotPeer;
    GameCore::Outcome outcome = GameCore::Outcome::Ok;
    uint16_t ver = 0;
    uint8_t fromSeat = 0;
  };
  // What leave did: how long it waited for the frame to be acked, the reliable frames still unacked when the goodbye
  // was sent, and the copies of the ABORT the link took for every peer.
  struct LeaveResult {
    uint32_t waitedMs = 0;
    size_t unackedAtSend = 0;
    uint8_t copies = 0;
  };
  struct Counters {
    GameCore::ReliableLink::Stats link;
    size_t unacked = 0;
    uint32_t queueOverflows = 0;
    uint32_t queued = 0, replaced = 0, strangers = 0;  // the sink's
    uint32_t endIgnored = 0;                           // latch() calls after the first
    size_t queueSize = 0;
  };

  // RAII access to the lobby for the lobby screen: it holds the lock, so keep it only for a short read or call and
  // never across a render or a wait. Lobby task. host() is null for a guest and guest() for a host.
  class Lobby {
   public:
    Lobby(Lobby&&) = default;
    Lobby& operator=(Lobby&&) = delete;
    GameCore::HostLobby* host() const { return hostLobby; }
    GameCore::GuestLobby* guest() const { return guestLobby; }

   private:
    friend class NearbySession;
    Lobby(std::unique_lock<std::mutex> held, GameCore::HostLobby* host, GameCore::GuestLobby* guest)
        : held(std::move(held)), hostLobby(host), guestLobby(guest) {}
    std::unique_lock<std::mutex> held;
    GameCore::HostLobby* hostLobby;
    GameCore::GuestLobby* guestLobby;
  };

  // Bring-up, on a task that may wait (the link is already running). Null, with the link ended and a LOG_ERR, when the
  // link is null, memory runs out (the pool in PSRAM, the lobby), or the task cannot be created: no task was started.
  // `label` is the host's name in its ADVERT.
  static std::unique_ptr<NearbySession> openHost(std::unique_ptr<GameCore::ILink> link, const GameCore::IClock& clock,
                                                 GameCore::IRandom& random, const GameCore::LobbyGame& game,
                                                 std::span<const uint8_t> label,
                                                 const GameCore::LobbySettings& settings = {},
                                                 const Options& options = {});
  static std::unique_ptr<NearbySession> openGuest(std::unique_ptr<GameCore::ILink> link, const GameCore::IClock& clock,
                                                  const GameCore::LobbyGame& game,
                                                  const GameCore::LobbySettings& settings = {},
                                                  const Options& options = {});
  ~NearbySession();
  NearbySession(const NearbySession&) = delete;
  NearbySession& operator=(const NearbySession&) = delete;

  Role role() const;
  // The host's session id: 0 for a guest that has not joined.
  uint32_t session() const;
  bool taskRunning() const;

  // Lobby task. The lobby is stepped by the GameLink task; this is the screen's way to read and drive it.
  Lobby lobby();
  // Lobby task, host: HostLobby::start() and, when it started, beginMatch(), under one lock.
  GameCore::StartResult startHost();
  // Lobby task, guest (or the host after start): copies the roster, builds the table of the seats this device does not
  // play from peerForSeat, builds the sink gate, and attaches it behind the lobby, which replays an ABORT or silence it
  // latched. False unless the lobby is Started. Idempotent.
  bool beginMatch();
  bool matchBegun() const;
  // Valid once matchBegun().
  GameCore::Roster roster() const;
  uint8_t localSeat() const;

  // VM task. The oldest queued STATE of the peer; false when none. The snapshot aliases a pool buffer the caller owns
  // until releaseState().
  bool takeState(NearbyState& out);
  void releaseState(NearbyState& state);
  // VM task, once per step: does nothing while session.pending() (a taken entry adopt() answers Busy is lost),
  // otherwise takes one STATE, calls session.adopt() outside the lock, releases the buffer, and logs the verdict.
  AdoptStep adoptNext(GameCore::Session& session);
  // VM task, the pull call for Session::pendingShipment: the first call for a version stages the snapshot and returns
  // false; the GameLink task sends it (a refused send is retried every step); the call after the link took it returns
  // true once, and the match calls Session::shipped() then and only then. A staged `over` is never replaced by another
  // version; a newer staged one that is not over is (latest wins). False, logged once, for an empty snapshot or one
  // over the snapshot limit.
  bool ship(const GameCore::Shipment& shipment);
  // VM task, lock-free: the latched reason the match is over, once; and whether one is latched.
  bool takeEnd(GameCore::Protocol::AbortReason& reason);
  bool endLatched() const;
  // VM task: the goodbye. With flushMs > 0 waits (no lock held; the task keeps resending and acking meanwhile) until
  // nothing is unacked, flushMs of clock time passed, or flushMs / 5 + 2 polls were made; then sends ABORT(reason)
  // CANCEL_ABORT_COPIES times to every peer. With flushMs == 0 sends at once (the forced exit). Before beginMatch()
  // it sends nothing (the lobby's cancel() is the lobby phase's goodbye). The deadline is the caller's.
  LeaveResult leave(GameCore::Protocol::AbortReason reason, uint32_t flushMs);
  // Any task.
  Counters counters() const;

 private:
  struct Core;
  explicit NearbySession(std::unique_ptr<Core> core);
  static std::unique_ptr<NearbySession> finish(std::unique_ptr<Core> core, const Options& options);

  std::unique_ptr<Core> core;
  uint32_t joinMs = GameLink::JOIN_MS;
};
