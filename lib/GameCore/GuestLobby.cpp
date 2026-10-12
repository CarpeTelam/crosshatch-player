#include "GuestLobby.h"

#include <algorithm>
#include <string_view>

// The host list's expiry, the repeated goodbye, and the "host heard broadcasting after the match began" rule are
// adapted from crossplay 0a1d17f src/apps_local/link/LinkSession.cpp (kCandidateForgetMs, kByeRepeats, the
// Hello-after-match rule and its peerConfirmed_ guard) and its host-tests/link/test_link.cpp
// (testPeerRestartingEndsTheMatch); the MIT notice is in ReliableLink.cpp.

namespace GameCore {

namespace {

using Protocol::AbortReason;
using Protocol::Type;

ReliableLink::Settings linkSettings(const LobbySettings& settings) {
  ReliableLink::Settings link = settings.link;
  link.maxPeers = 1;  // the host it joined
  return link;
}

}  // namespace

GuestLobby::GuestLobby(ILink& link, const IClock& clock, const LobbyGame& game, const LobbySettings& settings)
    : game_(game), settings_(settings), clock_(clock), link_(link, clock, *this, linkSettings(settings)) {}

PeerHandle GuestLobby::peerForSeat(const uint8_t seat) const {
  return state_ == GuestState::Started && seat == 1 ? joined_.handle : NO_PEER;
}

void GuestLobby::tick() {
  link_.tick();
  if (state_ == GuestState::Joining && link_.unacked(joined_.handle) == 0) state_ = GuestState::Joined;
  if (state_ == GuestState::Browsing) expireHosts();
}

void GuestLobby::expireHosts() {
  const uint64_t now = clock_.nowMs();
  size_t kept = 0;
  for (size_t i = 0; i < hostCount_; i++) {
    if (elapsed(now, hosts_[i].heardMs, settings_.hostExpireMs)) continue;
    if (kept != i) hosts_[kept] = hosts_[i];
    kept++;
  }
  for (size_t i = kept; i < hostCount_; i++) hosts_[i] = HostEntry{};
  hostCount_ = kept;
}

void GuestLobby::onAdvert(const PeerHandle from, const Protocol::Frame& frame) {
  const Protocol::Advert& advert = frame.advert;
  if (advert.gameId != std::string_view(game_.gameId)) return;  // another game is not listed
  size_t slot = hostCount_;
  for (size_t i = 0; i < hostCount_; i++) {
    if (hosts_[i].handle == from) slot = i;  // a new session for the same handle replaces the entry
  }
  if (slot == hostCount_) {
    if (hostCount_ == MAX_HOSTS) expireHosts();
    if (hostCount_ == MAX_HOSTS) return;  // the table is full: the new host is dropped
    slot = hostCount_++;
  }
  HostEntry& entry = hosts_[slot];
  entry = HostEntry{};
  entry.handle = from;
  entry.session = frame.header.session;
  entry.hostApi = advert.hostApi;
  entry.labelLen = static_cast<uint8_t>(std::min(advert.hostLabel.size(), entry.label.size()));
  std::copy_n(advert.hostLabel.begin(), entry.labelLen, entry.label.begin());
  entry.seatsTaken = advert.seatsTaken;
  entry.seatsMax = advert.seatsMax;
  entry.verdict = versionVerdict(game_, advert.pkgHash, advert.hostApi, advert.surface);
  entry.heardMs = clock_.nowMs();
}

JoinResult GuestLobby::join(const PeerHandle host, const uint32_t session) {
  if (state_ != GuestState::Browsing) return JoinResult::NotBrowsing;
  size_t found = hostCount_;
  for (size_t i = 0; i < hostCount_; i++) {
    if (hosts_[i].handle == host && hosts_[i].session == session) found = i;
  }
  if (found == hostCount_) return JoinResult::UnknownHost;
  if (hosts_[found].verdict == HostVerdict::DifferentVersion) return JoinResult::DifferentVersion;
  link_.setSession(session);
  Protocol::Frame join;
  join.header.type = Type::Join;
  join.join.pkgHash = game_.pkgHash;
  join.join.hostApi = static_cast<uint8_t>(game_.caps.api);  // Join::hostApi is the sender's own level
  join.join.surface = game_.surface;
  if (!link_.send(host, join)) {
    link_.setSession(Protocol::ANY_SESSION);
    return JoinResult::LinkRefused;
  }
  joined_ = hosts_[found];
  for (HostEntry& entry : hosts_) entry = HostEntry{};  // the other entries are dropped
  hostCount_ = 0;
  state_ = GuestState::Joining;
  return JoinResult::Sent;
}

ReliableLink::Verdict GuestLobby::onFrame(const PeerHandle from, const Protocol::Frame& frame) {
  switch (frame.header.type) {
    case Type::Advert:
      if (state_ == GuestState::Browsing) {
        onAdvert(from, frame);
      } else if (state_ == GuestState::Started && from == joined_.handle && !advertEnded_ &&
                 elapsed(clock_.nowMs(), startedAtMs_, settings_.advertGraceMs)) {
        // The host is advertising again, so it is in a lobby, not in this match: the match is over. Earlier copies
        // are the pre-Start adverts a delaying link delivers late.
        advertEnded_ = true;
        handOff_.forwardAbort(from, AbortReason::Left);
      }
      return Verdict::Refused;
    case Type::Accept:
      return onAccept(from, frame.accept);
    case Type::Abort:
      if (from != joined_.handle) return Verdict::Refused;  // another device: ignored
      return onAbort(frame);
    case Type::State:
      if (state_ == GuestState::Started && from == joined_.handle) return handOff_.forward(from, frame);
      if (waiting() && from == joined_.handle) return Verdict::NoRoom;  // not before ACCEPT: the host resends it
      break;
    case Type::Join:
    case Type::Ping:
    case Type::Ack:
      break;
  }
  // A frame we have no use for must not hold the one peer slot (the host of a live lobby or match keeps it).
  const bool live = waiting() || state_ == GuestState::Started;
  if (!(live && from == joined_.handle)) link_.forget(from);
  return Verdict::Refused;
}

ReliableLink::Verdict GuestLobby::onAccept(const PeerHandle from, const Protocol::Accept& accept) {
  if (!waiting() || from != joined_.handle) {
    const bool live = state_ == GuestState::Started && from == joined_.handle;  // a late copy: acked by the link
    if (!live) link_.forget(from);
    return Verdict::Refused;
  }
  // An ACCEPT outside what a host of this game can send (decode passes seat 1 and n 1) or outside what this device can
  // seat (its own caps.maxSeats may be below the host's) ends the wait, or the guest would sit Joined for a Start that
  // never comes. The host has already started, so it is told this guest is gone (as a cancel does) rather than left to
  // find out at the silence limit.
  if (accept.n < 2 || accept.n > Roster::MAX_SEATS || accept.seat < 2 || accept.seat > accept.n ||
      accept.n > game_.seatLimit() || accept.n < game_.startSeats()) {
    Protocol::Frame abort;
    abort.header.type = Type::Abort;
    abort.abortReason = AbortReason::Left;
    for (uint8_t copy = 0; copy < CANCEL_ABORT_COPIES; copy++) link_.send(joined_.handle, abort);
    gone_ = GoneReason::Left;
    state_ = GuestState::HostGone;
    hostEnded();
    return Verdict::Refused;
  }
  seat_ = accept.seat;
  roster_ = Roster::nearby(accept.n, accept.seat, std::min<int32_t>(game_.caps.api, joined_.hostApi));
  startedAtMs_ = clock_.nowMs();
  state_ = GuestState::Started;
  return Verdict::Taken;
}

ReliableLink::Verdict GuestLobby::onAbort(const Protocol::Frame& frame) {
  if (state_ == GuestState::Started) return handOff_.forward(joined_.handle, frame);
  if (!waiting()) return Verdict::Refused;
  switch (frame.abortReason) {
    case AbortReason::Full:
      refusal_ = Refusal::Full;
      state_ = GuestState::Refused;
      break;
    case AbortReason::VersionMismatch:
      refusal_ = Refusal::VersionMismatch;
      state_ = GuestState::Refused;
      break;
    case AbortReason::Timeout:
      gone_ = GoneReason::Silent;
      state_ = GuestState::HostGone;
      break;
    case AbortReason::Left:
    case AbortReason::ScriptError:
      gone_ = GoneReason::Left;
      state_ = GuestState::HostGone;
      break;
  }
  hostEnded();
  return Verdict::Refused;
}

void GuestLobby::onPeerSilent(const PeerHandle peer) {
  if (peer != joined_.handle) {
    link_.forget(peer);
  } else if (state_ == GuestState::Started) {
    handOff_.forwardSilent(peer);
  } else if (waiting()) {
    gone_ = GoneReason::Silent;
    state_ = GuestState::HostGone;
    hostEnded();
  }
}

void GuestLobby::hostEnded() { link_.forget(joined_.handle); }

void GuestLobby::backToList() {
  if (state_ != GuestState::Refused && state_ != GuestState::HostGone) return;
  link_.forget(joined_.handle);
  link_.setSession(Protocol::ANY_SESSION);
  joined_ = HostEntry{};
  refusal_ = Refusal::None;
  gone_ = GoneReason::None;
  state_ = GuestState::Browsing;
}

void GuestLobby::cancel() {
  if (waiting() || state_ == GuestState::Started) {
    Protocol::Frame abort;
    abort.header.type = Type::Abort;
    abort.abortReason = AbortReason::Left;
    for (uint8_t copy = 0; copy < CANCEL_ABORT_COPIES; copy++) link_.send(joined_.handle, abort);
    link_.forget(joined_.handle);
  }
  state_ = GuestState::Cancelled;
}

}  // namespace GameCore
