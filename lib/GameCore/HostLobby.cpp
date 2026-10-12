#include "HostLobby.h"

#include <algorithm>

// The ADVERT clock, the rate-limited refusal answer, and the repeated goodbye are adapted from crossplay 0a1d17f
// src/apps_local/link/LinkSession.cpp (kHelloIntervalMs, kJoinIntervalMs, kByeRepeats) and its
// host-tests/link/test_link.cpp (testJoinAnswersAreRateLimited, testMatchedPeersDoNotEchoJoins); the MIT notice is in
// ReliableLink.cpp.

namespace GameCore {

namespace {

using Protocol::AbortReason;
using Protocol::Type;

ReliableLink::Settings linkSettings(const LobbySettings& settings, const LobbyGame& game) {
  ReliableLink::Settings link = settings.link;
  // Every possible guest plus one free slot, so a stranger can still be registered and answered when the seats are
  // full.
  link.maxPeers = std::max<size_t>(game.seatLimit(), 1);
  return link;
}

}  // namespace

HostLobby::HostLobby(ILink& link, const IClock& clock, IRandom& random, const LobbyGame& game,
                     const std::span<const uint8_t> label, const LobbySettings& settings)
    : game_(game),
      settings_(settings),
      clock_(clock),
      session_(drawSession(random)),
      link_(link, clock, *this, linkSettings(settings, game)) {
  labelLen_ = static_cast<uint8_t>(std::min(label.size(), label_.size()));
  std::copy_n(label.begin(), labelLen_, label_.begin());
  link_.setSession(session_);
  if (!ok()) state_ = HostState::Closed;
}

bool HostLobby::ok() const {
  // A game whose fewest seats exceed what this host can seat could never start.
  return link_.ok() && game_.seatLimit() >= 2 && game_.startSeats() <= game_.seatLimit();
}

size_t HostLobby::guestIndex(const PeerHandle handle) const {
  for (size_t i = 0; i < guestCount_; i++) {
    if (guests_[i].handle == handle) return i;
  }
  return guestCount_;
}

PeerHandle HostLobby::peerForSeat(const uint8_t seat) const {
  if (state_ != HostState::Started || seat < 2 || seat - 2 >= static_cast<int>(guestCount_)) return NO_PEER;
  return guests_[seat - 2].handle;
}

void HostLobby::pushEvent(const HostEventKind kind, const uint8_t seat) {
  if (eventCount_ == EVENT_CAPACITY) {  // keep the newest
    eventHead_ = (eventHead_ + 1) % EVENT_CAPACITY;
    eventCount_--;
  }
  events_[(eventHead_ + eventCount_) % EVENT_CAPACITY] = HostEvent{kind, seat};
  eventCount_++;
}

bool HostLobby::poll(HostEvent& out) {
  if (eventCount_ == 0) return false;
  out = events_[eventHead_];
  eventHead_ = (eventHead_ + 1) % EVENT_CAPACITY;
  eventCount_--;
  return true;
}

void HostLobby::tick() {
  link_.tick();
  if (state_ != HostState::Waiting) return;
  const uint64_t now = clock_.nowMs();
  if (advertNow_ || elapsed(now, lastAdvertMs_, settings_.advertMs)) {
    sendAdvert();
    advertNow_ = false;
    lastAdvertMs_ = now;
  }
}

void HostLobby::sendAdvert() {
  Protocol::Frame frame;
  frame.header.type = Type::Advert;
  frame.advert.gameId = std::string_view(game_.gameId);
  frame.advert.pkgHash = game_.pkgHash;
  frame.advert.manifestApi = game_.manifestApi;
  frame.advert.hostApi = static_cast<uint8_t>(game_.caps.api);
  frame.advert.surface = game_.surface;
  frame.advert.seatsMax = game_.seatLimit();
  frame.advert.seatsTaken = seatsTaken();
  frame.advert.hostLabel = std::span<const uint8_t>(label_.data(), labelLen_);
  link_.broadcast(frame);  // a refused broadcast is an ordinary loss: the next interval tries again
}

// The ring holds the 4 most recent refused peers, so the one-answer-per-interval bound holds per peer for up to 4
// rotating strangers (each stranger's JOIN resend is 400 ms anyway).
bool HostLobby::mayAnswer(const PeerHandle peer) {
  const uint64_t now = clock_.nowMs();
  for (Answered& a : answered_) {
    if (a.peer == peer) {
      if (!elapsed(now, a.atMs, settings_.refusalAnswerMs)) return false;
      a.atMs = now;
      return true;
    }
  }
  answered_[answeredNext_] = Answered{peer, now};
  answeredNext_ = (answeredNext_ + 1) % ANSWER_RING;
  return true;
}

// Turns a peer away: at most one ABORT per refusalAnswerMs, then the link forgets it, so the peer neither holds a slot
// nor is acked (its resends are offered, and refused, afresh).
ReliableLink::Verdict HostLobby::refuse(const PeerHandle from, const AbortReason reason) {
  if (mayAnswer(from)) {
    Protocol::Frame abort;
    abort.header.type = Type::Abort;
    abort.abortReason = reason;
    link_.send(from, abort);
  }
  link_.forget(from);
  return Verdict::Refused;
}

ReliableLink::Verdict HostLobby::onFrame(const PeerHandle from, const Protocol::Frame& frame) {
  const bool seated = guestIndex(from) < guestCount_;
  switch (frame.header.type) {
    case Type::Join:
      return onJoin(from, frame.join);
    case Type::Abort:
      return onAbort(from, frame);
    case Type::State:
      if (seated && state_ == HostState::Started) return handOff_.forward(from, frame);
      break;
    case Type::Advert:
    case Type::Accept:
    case Type::Ping:
    case Type::Ack:
      break;
  }
  // A stranger must not hold a peer slot; a seated guest's frame we have no use for is simply refused.
  if (!seated) link_.forget(from);
  return Verdict::Refused;
}

ReliableLink::Verdict HostLobby::onJoin(const PeerHandle from, const Protocol::Join& join) {
  // A closed lobby (cancelled, or a failed start) has told its guests and forgotten them: a JOIN from one of them is
  // answered ABORT(left), never taken as a seat it still holds, which would resume its keepalives.
  if (state_ == HostState::Closed) return refuse(from, AbortReason::Left);
  if (guestIndex(from) < guestCount_) return Verdict::Taken;                 // seated already: no second seat
  if (state_ != HostState::Waiting) return refuse(from, AbortReason::Full);  // AD-13: a late JOIN gets ABORT(full)
  if (versionVerdict(game_, join.pkgHash, join.hostApi, join.surface) == HostVerdict::DifferentVersion) {
    return refuse(from, AbortReason::VersionMismatch);
  }
  if (seatsTaken() >= seatLimit()) return refuse(from, AbortReason::Full);
  guests_[guestCount_] = Guest{from, join.hostApi};  // Join::hostApi is the sender's own level
  guestCount_++;
  pushEvent(HostEventKind::GuestJoined, static_cast<uint8_t>(guestCount_ + 1));
  advertNow_ = true;
  return Verdict::Taken;
}

ReliableLink::Verdict HostLobby::onAbort(const PeerHandle from, const Protocol::Frame& frame) {
  const size_t index = guestIndex(from);
  if (index >= guestCount_) return Verdict::Refused;  // from anyone else: ignored
  if (state_ == HostState::Started) return handOff_.forward(from, frame);
  if (state_ == HostState::Waiting) removeGuest(index, HostEventKind::GuestLeft);
  return Verdict::Refused;
}

void HostLobby::onPeerSilent(const PeerHandle peer) {
  const size_t index = guestIndex(peer);
  if (state_ == HostState::Started && index < guestCount_) {
    handOff_.forwardSilent(peer);
  } else if (state_ == HostState::Waiting) {
    if (index < guestCount_) {
      removeGuest(index, HostEventKind::GuestSilent);
    } else {
      link_.forget(peer);
    }
  }
}

// Frees a seat before Start: the later guests close the gap, the link forgets the leaver (so a rejoin starts at seq 0),
// and the new count is advertised at once.
void HostLobby::removeGuest(const size_t index, const HostEventKind kind) {
  const PeerHandle leaver = guests_[index].handle;
  pushEvent(kind, static_cast<uint8_t>(index + 2));
  for (size_t i = index; i + 1 < guestCount_; i++) guests_[i] = guests_[i + 1];
  guestCount_--;
  guests_[guestCount_] = Guest{};
  link_.forget(leaver);
  advertNow_ = true;
}

int32_t HostLobby::lowestApi() const {
  int32_t api = game_.caps.api;
  for (size_t i = 0; i < guestCount_; i++) api = std::min<int32_t>(api, guests_[i].api);
  return api;
}

StartResult HostLobby::start() {
  if (state_ != HostState::Waiting) return StartResult::NotWaiting;
  if (!canStart()) return StartResult::NotEnoughSeats;
  const uint8_t n = seatsTaken();
  for (size_t i = 0; i < guestCount_; i++) {
    Protocol::Frame accept;
    accept.header.type = Type::Accept;
    accept.accept.seat = static_cast<uint8_t>(i + 2);
    accept.accept.n = n;
    if (!link_.send(guests_[i].handle, accept)) {
      sendAbortToGuests();
      state_ = HostState::Closed;
      return StartResult::LinkFailed;
    }
  }
  roster_ = Roster::nearby(n, 1, lowestApi());
  state_ = HostState::Started;
  return StartResult::Started;
}

void HostLobby::sendAbortToGuests() {
  Protocol::Frame abort;
  abort.header.type = Type::Abort;
  abort.abortReason = AbortReason::Left;
  for (uint8_t copy = 0; copy < CANCEL_ABORT_COPIES; copy++) {
    for (size_t i = 0; i < guestCount_; i++) link_.send(guests_[i].handle, abort);
  }
  // Nothing more goes to them, keepalives included: a guest that lost every copy hears silence and leaves at the limit.
  for (size_t i = 0; i < guestCount_; i++) link_.forget(guests_[i].handle);
}

void HostLobby::cancel() {
  if (state_ == HostState::Closed) return;
  sendAbortToGuests();
  state_ = HostState::Closed;
}

}  // namespace GameCore
