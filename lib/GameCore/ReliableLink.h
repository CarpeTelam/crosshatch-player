#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "IClock.h"
#include "ILink.h"
#include "Protocol.h"

namespace GameCore {

// Reliable, in-order delivery over an ILink (spine AD-13, R2): the owner of seq, ack, resends, keepalives, and the
// silent-peer report. Protocol stays the only encoder and decoder of the header: this class names a frame's type,
// session, seq, and ack as Protocol::Frame fields and never writes a byte itself. A peer is named only by its
// PeerHandle. Time comes only from the IClock. Pure logic: no log, no static state.
//
// One task calls it at a time (as ILink); there is no locking. The sink runs inside tick().
//
// What the wire gives the session:
// - JOIN, ACCEPT, and STATE are reliable and in order, one frame awaiting its ack per peer at a time (not one per
//   resend). A STATE that has not been acked may be skipped: a newer STATE replaces it (latest wins), unless it ends
//   the round (`over`). The replacing STATE is transmitted at once with a fresh seq. Replacement stops the old
//   STATE's resends; it does not recall it, so one that had already reached the peer is still delivered, then the
//   newer one. An `over` STATE, every JOIN, and every ACCEPT are never skipped.
// - ABORT and ADVERT are unsequenced and go out at once; PING and ACK are the link's own keepalive and are never
//   handed to the sink.
// - A peer that sends nothing valid for `silenceMs` is reported once through Sink::onPeerSilent.
//
// Adapted from crossplay 0a1d17f src/apps_local/link/LinkSession.cpp (MIT, notice in ReliableLink.cpp): the retry
// clock kept apart from the heartbeat clock, the bound on frames taken per tick, and acking every copy of a frame.
//
// Memory (all from makeUniqueNoThrow, nothing static): two 1,470 B buffers (receive and transmit), plus per peer up to
// QUEUE_DEPTH snapshot buffers of 1,400 B, each allocated the first time its queue entry holds a STATE and kept until
// the peer is forgotten: about 8.5 KB per peer, about 25 KB at the default 4 peers.
class ReliableLink {
 public:
  static constexpr uint32_t DEFAULT_RESEND_MS = 400;      // AD-13
  static constexpr uint32_t DEFAULT_SILENCE_MS = 10000;   // AD-13
  static constexpr uint32_t DEFAULT_KEEPALIVE_MS = 1000;  // crossplay kHeartbeatIntervalMs; must be < silenceMs
  static constexpr size_t DEFAULT_MAX_PEERS = 4;
  static constexpr size_t QUEUE_DEPTH = 4;           // reliable frames held per peer: one in flight, the rest waiting
  static constexpr size_t MAX_FRAMES_PER_TICK = 16;  // crossplay kMaxPacketsPerTick

  struct Settings {
    uint32_t resendMs = DEFAULT_RESEND_MS;
    uint32_t silenceMs = DEFAULT_SILENCE_MS;
    uint32_t keepaliveMs = DEFAULT_KEEPALIVE_MS;  // keepaliveMs < silenceMs is the caller's to keep: not checked
    size_t maxPeers = DEFAULT_MAX_PEERS;
  };
  enum class Verdict : uint8_t { Taken, Refused, NoRoom };  // Taken, Refused: acked. NoRoom: not acked.

  class Sink {  // the lobby and the match session implement it
   public:
    virtual ~Sink() = default;
    // A frame for the session: Join, Accept, State (reliable: in order, once each, replaced States may be skipped), or
    // Advert, Abort (unsequenced: at once, any number of times; the return value is ignored). `frame` and every
    // span in it alias the link's receive buffer: copy what you keep before returning. May call send() and
    // forget(); must not call tick().
    virtual Verdict onFrame(PeerHandle from, const Protocol::Frame& frame) = 0;
    // A peer sent nothing valid for silenceMs. Once per peer until forget(). May call send() (it returns false
    // for that peer) and forget().
    virtual void onPeerSilent(PeerHandle peer) = 0;
  };

  // Counters since construction. A frame the link took is counted once, by what it was: framesSent (the first
  // transmission of a Join, Accept, or State, and an Advert), resends, acksSent, pingsSent, or abortsSent; one it
  // refused is linkRefused. framesReceived counts frames that decoded and were processed (not stray, not dropped
  // for a full peer table).
  struct Stats {
    uint32_t framesSent = 0;
    uint32_t resends = 0;
    uint32_t acksSent = 0;
    uint32_t pingsSent = 0;
    uint32_t abortsSent = 0;
    uint32_t linkRefused = 0;
    uint32_t framesReceived = 0;
    uint32_t duplicates = 0;
    uint32_t aheadDropped = 0;
    uint32_t noRoom = 0;
    uint32_t badFrames = 0;
    uint32_t strayFrames = 0;
    uint32_t peerTableFull = 0;
    uint32_t queueFull = 0;
    uint32_t silentReports = 0;
  };

  ReliableLink(ILink& link, const IClock& clock, Sink& sink);
  ReliableLink(ILink& link, const IClock& clock, Sink& sink, const Settings& settings);
  ~ReliableLink();
  ReliableLink(const ReliableLink&) = delete;
  ReliableLink& operator=(const ReliableLink&) = delete;

  bool ok() const;  // false when construction could not allocate: every call then does nothing / returns false
  // 0 (ANY_SESSION, the start state) or the host's id. A change drops every peer's state (queues, seq, lost flags).
  void setSession(uint32_t session);
  uint32_t session() const;
  // Reliable: Join, Accept, State (header.type; header.session/seq/ack are overwritten). Unsequenced: Abort.
  // False, nothing queued: not ok(), session unset, a field Protocol::encode refuses, the peer invalid, lost or
  // out of table room, the type is Advert/Ping/Ack, the queue is full (QUEUE_DEPTH), or allocation failed. A true
  // for a reliable frame means queued, not delivered; a first transmission the link refuses is an ordinary loss.
  // For Abort, true means the link took it.
  bool send(PeerHandle to, const Protocol::Frame& frame);
  bool broadcast(const Protocol::Frame& advert);  // Advert only, unsequenced; false for any other type
  void tick();  // receive up to MAX_FRAMES_PER_TICK frames, then per peer: silence, resend, keepalive
  // Drops the peer's state, queue, and lost flag; a later frame registers it afresh. A peer that keeps its seq after
  // you forgot it has its Join/Accept dropped as ahead (never registered, never acked), so forget is for a peer that
  // starts afresh (a new link or session on its side). A peer is registered only by a frame that will be offered.
  void forget(PeerHandle peer);
  size_t unacked() const;  // reliable frames held (in flight or waiting) for peers not lost
  size_t unacked(PeerHandle peer) const;
  const Stats& stats() const;

 private:
  struct Entry;
  struct Peer;

  Peer* findPeer(PeerHandle handle) const;
  Peer* registerPeer(PeerHandle handle);
  bool sendReliable(PeerHandle to, const Protocol::Frame& frame);
  bool sendAbort(PeerHandle to, const Protocol::Frame& frame);
  bool enqueue(Peer& peer, const Protocol::Frame& frame);
  void release(Peer& peer);
  bool transmit(Peer& peer, const Entry& entry, bool again);
  bool sendControl(Peer& peer, Protocol::Type type);
  void applyAck(Peer& peer, uint16_t ack);
  void receiveOne(PeerHandle from, size_t length);
  void receiveSequenced(PeerHandle from, const Protocol::Frame& frame);
  void serve(Peer& peer, uint64_t now);

  ILink& link_;
  const IClock& clock_;
  Sink& sink_;
  Settings settings_;
  Stats stats_;
  uint32_t session_ = Protocol::ANY_SESSION;
  uint32_t nextEpoch_ = 1;
  bool ok_ = false;
  std::unique_ptr<uint8_t[]> rx_;  // frames alias it while a sink runs, and a sink may send(): two buffers
  std::unique_ptr<uint8_t[]> tx_;
  std::unique_ptr<Peer[]> peers_;
};

}  // namespace GameCore
