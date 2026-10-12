#include "ReliableLink.h"

#include <Memory.h>

#include <algorithm>
#include <span>
#include <utility>

// Parts adapted from crossplay 0a1d17f src/apps_local/link/LinkSession.cpp (MIT):
// Copyright (c) 2025 CrossPoint Reader organization; Copyright (c) 2026 Mario Ruiz (CrossPlay).
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

namespace GameCore {

namespace {

using Protocol::Type;

constexpr uint16_t NOTHING_RECEIVED = 0xFFFF;  // the `ack` of a side that has received nothing (rxNext 0, minus 1)
constexpr uint16_t SERIAL_HALF = 0x8000;       // 16-bit serial-number order: a difference below it means "ahead"

// True once `span` ms have passed since `since`; a clock that stepped back never fires.
bool elapsed(const uint64_t now, const uint64_t since, const uint32_t span) {
  return now >= since && now - since >= span;
}

}  // namespace

// One reliable frame held for a peer. Join and Accept keep their fields; a State keeps its snapshot in a buffer
// allocated the first time the entry holds one and kept for the peer's life.
struct ReliableLink::Entry {
  Type type = Type::State;
  Protocol::Join join;
  Protocol::Accept accept;
  uint16_t ver = 0;
  Status status;
  size_t length = 0;
  std::unique_ptr<uint8_t[]> snapshot;
  bool released = false;  // has a seq and is on the wire: only entry 0 can be
  uint16_t seq = 0;
  uint64_t sentAtMs = 0;  // the resend clock: when this entry was last transmitted
};

struct ReliableLink::Peer {
  bool inUse = false;
  PeerHandle handle;
  uint32_t epoch = 0;  // differs for every registration, so a callback can tell the peer was forgotten
  Entry queue[QUEUE_DEPTH];
  size_t queued = 0;
  uint16_t nextSeq = 0;  // next seq to issue
  uint16_t rxNext = 0;   // next seq expected from the peer
  bool hasRx = false;
  bool lost = false;
  uint64_t lastHeardMs = 0;
  uint64_t lastSentMs = 0;

  // Back to a free slot; the snapshot buffers go too.
  void reset() {
    for (Entry& e : queue) e = Entry{};  // entry by entry, frees each snapshot; a whole Peer{} would be a large local
    inUse = false;
    handle = PeerHandle{};
    epoch = 0;
    queued = 0;
    nextSeq = 0;
    rxNext = 0;
    hasRx = false;
    lost = false;
    lastHeardMs = 0;
    lastSentMs = 0;
  }
  uint16_t ackValue() const { return static_cast<uint16_t>(rxNext - 1); }
};

ReliableLink::ReliableLink(ILink& link, const IClock& clock, Sink& sink)
    : ReliableLink(link, clock, sink, Settings{}) {}

ReliableLink::ReliableLink(ILink& link, const IClock& clock, Sink& sink, const Settings& settings)
    : link_(link), clock_(clock), sink_(sink), settings_(settings) {
  rx_ = makeUniqueNoThrow<uint8_t[]>(Protocol::MAX_FRAME_BYTES);
  tx_ = makeUniqueNoThrow<uint8_t[]>(Protocol::MAX_FRAME_BYTES);
  peers_ = makeUniqueNoThrow<Peer[]>(settings_.maxPeers);
  ok_ = rx_ && tx_ && peers_;
}

ReliableLink::~ReliableLink() = default;

bool ReliableLink::ok() const { return ok_; }

void ReliableLink::setSession(const uint32_t session) {
  if (session == session_) return;
  session_ = session;
  // Queues, seqs, and lost flags belong to the old session: a Join resent under the new id would reach the old host.
  if (ok_) {
    for (size_t i = 0; i < settings_.maxPeers; i++) peers_[i].reset();
  }
}

uint32_t ReliableLink::session() const { return session_; }

const ReliableLink::Stats& ReliableLink::stats() const { return stats_; }

ReliableLink::Peer* ReliableLink::findPeer(const PeerHandle handle) const {
  if (!ok_ || !handle.valid()) return nullptr;
  for (size_t i = 0; i < settings_.maxPeers; i++) {
    Peer& p = peers_[i];
    if (p.inUse && p.handle == handle) return &p;
  }
  return nullptr;
}

ReliableLink::Peer* ReliableLink::registerPeer(const PeerHandle handle) {
  for (size_t i = 0; i < settings_.maxPeers; i++) {
    Peer& p = peers_[i];
    if (p.inUse) continue;
    p.reset();
    p.inUse = true;
    p.handle = handle;
    p.epoch = nextEpoch_++;
    p.lastHeardMs = clock_.nowMs();  // liveness starts at registration
    p.lastSentMs = p.lastHeardMs;
    return &p;
  }
  return nullptr;
}

bool ReliableLink::send(const PeerHandle to, const Protocol::Frame& frame) {
  if (!ok_ || session_ == Protocol::ANY_SESSION || !to.valid()) return false;
  switch (frame.header.type) {
    case Type::Join:
    case Type::Accept:
    case Type::State:
      return sendReliable(to, frame);
    case Type::Abort:
      return sendAbort(to, frame);
    case Type::Advert:
    case Type::Ping:
    case Type::Ack:
      break;
  }
  return false;
}

bool ReliableLink::sendReliable(const PeerHandle to, const Protocol::Frame& frame) {
  // Encode once with seq 0: this catches every field Protocol::encode refuses, so no later transmission can fail.
  Protocol::Frame check = frame;
  check.header.session = session_;
  check.header.seq = 0;
  check.header.ack = 0;
  if (Protocol::encode(std::span<uint8_t>(tx_.get(), Protocol::MAX_FRAME_BYTES), check) == 0) return false;

  bool fresh = false;
  Peer* peer = findPeer(to);
  if (peer != nullptr) {
    if (peer->lost) return false;
  } else {
    peer = registerPeer(to);
    if (peer == nullptr) {
      stats_.peerTableFull++;
      return false;
    }
    fresh = true;
  }
  if (!enqueue(*peer, frame)) {
    if (fresh) peer->reset();  // a peer we could not queue for is not registered
    return false;
  }
  if (!peer->queue[0].released) release(*peer);
  return true;
}

bool ReliableLink::enqueue(Peer& peer, const Protocol::Frame& frame) {
  const Type type = frame.header.type;
  Entry* slot = nullptr;
  Entry* const tail = peer.queued > 0 ? &peer.queue[peer.queued - 1] : nullptr;
  if (type == Type::State && tail != nullptr && tail->type == Type::State && !tail->status.over) {
    // Latest wins: a State that has not ended the round is replaced, even one in flight (it is then the only
    // entry). An in-flight one is abandoned: its seq is never reused or resent, and the entry gets a fresh seq.
    tail->released = false;
    slot = tail;
  } else {
    if (peer.queued >= QUEUE_DEPTH) {
      stats_.queueFull++;
      return false;
    }
    slot = &peer.queue[peer.queued];
  }

  if (type == Type::State) {
    const std::span<const uint8_t> snapshot = frame.state.snapshot;
    if (!slot->snapshot) {
      slot->snapshot = makeUniqueNoThrow<uint8_t[]>(Protocol::MAX_SNAPSHOT_BYTES);
      if (!slot->snapshot) return false;  // only an appended slot can lack the buffer: nothing has changed
    }
    std::copy(snapshot.begin(), snapshot.end(), slot->snapshot.get());
    slot->length = snapshot.size();
    slot->ver = frame.state.ver;
    slot->status = frame.state.status;
  } else if (type == Type::Join) {
    slot->join = frame.join;
  } else {
    slot->accept = frame.accept;
  }
  slot->type = type;
  if (slot != tail) {
    slot->released = false;
    peer.queued++;
  }
  return true;
}

void ReliableLink::release(Peer& peer) {
  Entry& e = peer.queue[0];
  e.seq = peer.nextSeq++;
  e.released = true;
  transmit(peer, e, false);
  e.sentAtMs = clock_.nowMs();
}

bool ReliableLink::transmit(Peer& peer, const Entry& entry, const bool again) {
  Protocol::Frame f;
  f.header.type = entry.type;
  f.header.session = session_;
  f.header.seq = entry.seq;
  f.header.ack = peer.ackValue();
  switch (entry.type) {
    case Type::Join:
      f.join = entry.join;
      break;
    case Type::Accept:
      f.accept = entry.accept;
      break;
    case Type::State:
      f.state.ver = entry.ver;
      f.state.status = entry.status;
      f.state.snapshot = std::span<const uint8_t>(entry.snapshot.get(), entry.length);
      break;
    case Type::Advert:
    case Type::Abort:
    case Type::Ping:
    case Type::Ack:
      break;  // never queued
  }
  const size_t size = Protocol::encode(std::span<uint8_t>(tx_.get(), Protocol::MAX_FRAME_BYTES), f);
  const bool sent = size > 0 && link_.send(peer.handle, std::span<const uint8_t>(tx_.get(), size));
  peer.lastSentMs = clock_.nowMs();  // any transmission, refused or not, defers the keepalive
  if (!sent) {
    stats_.linkRefused++;  // an ordinary loss: the resend clock runs all the same
  } else if (again) {
    stats_.resends++;
  } else {
    stats_.framesSent++;
  }
  return sent;
}

bool ReliableLink::sendControl(Peer& peer, const Type type) {
  Protocol::Frame f;
  f.header.type = type;
  f.header.session = session_;
  f.header.seq = 0;  // Ping and Ack consume no seq
  f.header.ack = peer.ackValue();
  const size_t size = Protocol::encode(std::span<uint8_t>(tx_.get(), Protocol::MAX_FRAME_BYTES), f);
  const bool sent = size > 0 && link_.send(peer.handle, std::span<const uint8_t>(tx_.get(), size));
  peer.lastSentMs = clock_.nowMs();
  if (!sent) {
    stats_.linkRefused++;
  } else if (type == Type::Ack) {
    stats_.acksSent++;
  } else {
    stats_.pingsSent++;
  }
  return sent;
}

bool ReliableLink::sendAbort(const PeerHandle to, const Protocol::Frame& frame) {
  // Never queued and never behind a reliable frame; works for a lost or unknown peer.
  Peer* const peer = findPeer(to);
  Protocol::Frame f;
  f.header.type = Type::Abort;
  f.header.session = session_;
  f.header.seq = 0;
  f.header.ack = peer != nullptr ? peer->ackValue() : NOTHING_RECEIVED;
  f.abortReason = frame.abortReason;
  const size_t size = Protocol::encode(std::span<uint8_t>(tx_.get(), Protocol::MAX_FRAME_BYTES), f);
  if (size == 0) return false;
  const bool sent = link_.send(to, std::span<const uint8_t>(tx_.get(), size));
  if (sent) {
    stats_.abortsSent++;
  } else {
    stats_.linkRefused++;
  }
  if (peer != nullptr && !peer->lost) peer->lastSentMs = clock_.nowMs();
  return sent;
}

bool ReliableLink::broadcast(const Protocol::Frame& advert) {
  if (!ok_ || session_ == Protocol::ANY_SESSION || advert.header.type != Type::Advert) return false;
  Protocol::Frame f = advert;
  f.header.session = session_;
  f.header.seq = 0;
  f.header.ack = 0;
  const size_t size = Protocol::encode(std::span<uint8_t>(tx_.get(), Protocol::MAX_FRAME_BYTES), f);
  if (size == 0) return false;
  if (!link_.broadcast(std::span<const uint8_t>(tx_.get(), size))) {
    stats_.linkRefused++;
    return false;
  }
  stats_.framesSent++;
  return true;
}

void ReliableLink::applyAck(Peer& peer, const uint16_t ack) {
  if (peer.queued == 0 || !peer.queue[0].released) return;
  // Serial-number order bounded by the highest seq issued. With one frame in flight that is the in-flight seq
  // itself, so only an ack equal to it counts: an older ack (an abandoned State's, or the initial 0xFFFF against
  // seq 0) and a later one (never issued) leave the frame unacked.
  if (ack != peer.queue[0].seq) return;
  // Pop by rotating the entry to the end of the used part, so its snapshot buffer stays with the peer.
  for (size_t i = 0; i + 1 < peer.queued; i++) std::swap(peer.queue[i], peer.queue[i + 1]);
  peer.queued--;
  peer.queue[peer.queued].released = false;
  if (peer.queued > 0) release(peer);
}

void ReliableLink::forget(const PeerHandle peer) {
  Peer* const p = findPeer(peer);
  if (p != nullptr) p->reset();
}

size_t ReliableLink::unacked() const {
  if (!ok_) return 0;
  size_t total = 0;
  for (size_t i = 0; i < settings_.maxPeers; i++) {
    if (peers_[i].inUse && !peers_[i].lost) total += peers_[i].queued;
  }
  return total;
}

size_t ReliableLink::unacked(const PeerHandle peer) const {
  const Peer* const p = findPeer(peer);
  return p != nullptr && !p->lost ? p->queued : 0;
}

void ReliableLink::tick() {
  if (!ok_) return;
  for (size_t i = 0; i < MAX_FRAMES_PER_TICK; i++) {
    PeerHandle from;
    size_t length = 0;
    if (!link_.receive(from, std::span<uint8_t>(rx_.get(), Protocol::MAX_FRAME_BYTES), length)) break;
    receiveOne(from, length);
  }
  const uint64_t now = clock_.nowMs();
  for (size_t i = 0; i < settings_.maxPeers; i++) {
    if (peers_[i].inUse && !peers_[i].lost) serve(peers_[i], now);
  }
}

void ReliableLink::serve(Peer& peer, const uint64_t now) {
  if (elapsed(now, peer.lastHeardMs, settings_.silenceMs)) {
    peer.lost = true;
    for (Entry& e : peer.queue) e.released = false;
    peer.queued = 0;
    stats_.silentReports++;
    sink_.onPeerSilent(peer.handle);  // the caller does nothing more with this peer
    return;
  }
  // The resend clock is the entry's own sentAtMs: acks, keepalives, and the peer's traffic never move it.
  if (peer.queued > 0 && peer.queue[0].released && elapsed(now, peer.queue[0].sentAtMs, settings_.resendMs)) {
    Entry& e = peer.queue[0];
    transmit(peer, e, true);
    e.sentAtMs = clock_.nowMs();
  }
  if (elapsed(now, peer.lastSentMs, settings_.keepaliveMs)) {
    // An ACK acknowledges the last frame received and so doubles as the keepalive; before any, a PING.
    sendControl(peer, peer.hasRx ? Type::Ack : Type::Ping);
  }
}

void ReliableLink::receiveOne(const PeerHandle from, const size_t length) {
  Protocol::Frame frame;
  if (Protocol::decode(std::span<const uint8_t>(rx_.get(), length), session_, frame) != Protocol::DecodeStatus::Ok) {
    stats_.badFrames++;
    return;
  }
  const Type type = frame.header.type;
  if (session_ == Protocol::ANY_SESSION && type != Type::Advert) {
    stats_.strayFrames++;
    return;
  }
  switch (type) {
    case Type::Advert:
      stats_.framesReceived++;
      sink_.onFrame(from, frame);  // from anyone; never registers a peer
      return;
    case Type::Abort: {
      stats_.framesReceived++;
      Peer* const peer = findPeer(from);
      if (peer != nullptr && !peer->lost) {
        peer->lastHeardMs = clock_.nowMs();
        applyAck(*peer, frame.header.ack);
      }
      sink_.onFrame(from, frame);  // even from a lost or unknown peer: the session latches the first reason
      return;
    }
    case Type::Ping:
    case Type::Ack: {
      Peer* const peer = findPeer(from);
      if (peer == nullptr || peer->lost) {
        stats_.strayFrames++;
        return;
      }
      stats_.framesReceived++;
      peer->lastHeardMs = clock_.nowMs();
      applyAck(*peer, frame.header.ack);
      return;
    }
    case Type::Join:
    case Type::Accept:
    case Type::State:
      receiveSequenced(from, frame);
      return;
  }
}

void ReliableLink::receiveSequenced(const PeerHandle from, const Protocol::Frame& frame) {
  Peer* peer = findPeer(from);
  if (peer != nullptr && peer->lost) {
    stats_.strayFrames++;
    return;
  }
  const uint16_t seq = frame.header.seq;
  const uint16_t d = static_cast<uint16_t>(seq - (peer != nullptr ? peer->rxNext : 0));
  const bool isState = frame.header.type == Type::State;
  // d == 0: the next frame. A State ahead of it (d below the half) is a skip: a State may follow a seq that was
  // abandoned for it, and the sender waits for the frame before it to be acked only for a Join or Accept.
  const bool offer = d == 0 || (d < SERIAL_HALF && isState);
  if (peer == nullptr) {
    // A peer is registered only by a frame that will be offered: anything else leaves no slot to keep alive.
    if (!offer) {
      stats_.framesReceived++;
      if (d < SERIAL_HALF) {
        stats_.aheadDropped++;
      } else {
        stats_.duplicates++;
      }
      return;
    }
    peer = registerPeer(from);
    if (peer == nullptr) {
      stats_.peerTableFull++;
      return;
    }
  }
  stats_.framesReceived++;
  peer->lastHeardMs = clock_.nowMs();
  applyAck(*peer, frame.header.ack);

  if (offer) {
    const uint32_t epoch = peer->epoch;
    const Verdict verdict = sink_.onFrame(from, frame);
    peer = findPeer(from);  // the sink may have forgotten (and re-registered) the peer
    if (peer == nullptr || peer->epoch != epoch) return;
    if (verdict == Verdict::NoRoom) {
      stats_.noRoom++;  // left unacked: the peer's resend offers it again
      return;
    }
    peer->rxNext = static_cast<uint16_t>(seq + 1);
    peer->hasRx = true;
    sendControl(*peer, Type::Ack);
  } else if (d < SERIAL_HALF) {
    stats_.aheadDropped++;  // a Join or Accept past a gap: wait for the one before
  } else {
    stats_.duplicates++;
    sendControl(*peer, Type::Ack);  // every copy is acked, retransmits included, or the sender resends forever
  }
}

}  // namespace GameCore
