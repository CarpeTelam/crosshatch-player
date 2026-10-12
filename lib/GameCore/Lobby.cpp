#include "Lobby.h"

#include <algorithm>

namespace GameCore {

void LobbyGame::setGameId(const std::string_view id) {
  const size_t length = std::min(id.size(), Protocol::MAX_GAME_ID_BYTES);
  std::copy_n(id.begin(), length, gameId);
  gameId[length] = '\0';
  std::fill(gameId + length + 1, gameId + sizeof(gameId), '\0');
}

uint8_t LobbyGame::seatLimit() const {
  const int32_t limit = std::min<int32_t>({seatsMax, caps.maxSeats, Roster::MAX_SEATS});
  return static_cast<uint8_t>(std::max<int32_t>(limit, 0));
}

uint8_t LobbyGame::startSeats() const { return static_cast<uint8_t>(std::clamp<int32_t>(seatsMin, 2, 255)); }

HostVerdict versionVerdict(const LobbyGame& own, const std::array<uint8_t, Protocol::PKG_HASH_BYTES>& peerHash,
                           const uint8_t peerApi, const uint32_t peerSurface) {
  if (peerHash != own.pkgHash) return HostVerdict::DifferentVersion;
  const bool eitherPreview = own.apiPreview || static_cast<int32_t>(peerApi) > own.caps.api;
  if (eitherPreview && peerSurface != own.surface) return HostVerdict::DifferentVersion;
  return HostVerdict::Joinable;
}

uint32_t drawSession(IRandom& random) {
  for (int draw = 0; draw < 8; draw++) {
    const uint32_t value = random.next32();
    if (value != 0) return value;
  }
  return 1;
}

void LobbyHandOff::attach(ReliableLink::Sink& next, const uint32_t session) {
  const bool first = next_ == nullptr;
  next_ = &next;
  session_ = session;
  if (!first) return;
  if (end_.kind == LobbyEnd::Kind::Abort) {
    deliverAbort(end_.peer, end_.reason);
  } else if (end_.kind == LobbyEnd::Kind::Silent) {
    next.onPeerSilent(end_.peer);
  }
}

ReliableLink::Verdict LobbyHandOff::forward(const PeerHandle from, const Protocol::Frame& frame) {
  using Verdict = ReliableLink::Verdict;
  if (next_ != nullptr) return next_->onFrame(from, frame);
  switch (frame.header.type) {
    case Protocol::Type::State:
      return Verdict::NoRoom;  // not acked: the peer resends it, and it is delivered after attach
    case Protocol::Type::Abort:
      latch(LobbyEnd::Kind::Abort, from, frame.abortReason);
      return Verdict::Refused;
    default:
      return Verdict::Refused;
  }
}

void LobbyHandOff::forwardSilent(const PeerHandle peer) {
  if (next_ != nullptr) {
    next_->onPeerSilent(peer);
  } else {
    latch(LobbyEnd::Kind::Silent, peer, Protocol::AbortReason::Timeout);
  }
}

void LobbyHandOff::forwardAbort(const PeerHandle from, const Protocol::AbortReason reason) {
  if (next_ != nullptr) {
    deliverAbort(from, reason);
  } else {
    latch(LobbyEnd::Kind::Abort, from, reason);
  }
}

void LobbyHandOff::deliverAbort(const PeerHandle from, const Protocol::AbortReason reason) {
  Protocol::Frame frame;
  frame.header.type = Protocol::Type::Abort;
  frame.header.session = session_;
  frame.abortReason = reason;
  next_->onFrame(from, frame);
}

void LobbyHandOff::latch(const LobbyEnd::Kind kind, const PeerHandle peer, const Protocol::AbortReason reason) {
  if (end_.kind != LobbyEnd::Kind::None) return;  // the first end wins
  end_.kind = kind;
  end_.peer = peer;
  end_.reason = reason;
}

}  // namespace GameCore
