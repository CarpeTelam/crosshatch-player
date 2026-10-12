#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "HostCaps.h"
#include "ILink.h"
#include "IRandom.h"
#include "Protocol.h"
#include "ReliableLink.h"
#include "Roster.h"

// What HostLobby and GuestLobby share (spine AD-11 Nearby, AD-13 session and matching; R3): the game a lobby speaks
// for, the version rule, the timing settings, a session id, and the hand-off of the link to the match. Pure logic: no
// log, no static state, no allocation of its own.
namespace GameCore {

// What a lobby knows about the game, copied in (the lobby never reads a manifest).
struct LobbyGame {
  char gameId[Protocol::MAX_GAME_ID_BYTES + 1] = {};  // the manifest id
  std::array<uint8_t, Protocol::PKG_HASH_BYTES> pkgHash{};
  uint8_t manifestApi = 0;             // the game's declared api (ADVERT carries it)
  int32_t seatsMin = 0, seatsMax = 0;  // the manifest's seats
  uint32_t surface = 0;                // API_SURFACE_CRC
  bool apiPreview = false;             // !API_LEVEL_FROZEN: this device's level is a preview
  HostCaps caps;                       // caps.api = this device's API_LEVEL; caps.maxSeats

  void setGameId(std::string_view id);  // copies at most MAX_GAME_ID_BYTES bytes
  // min(seatsMax, caps.maxSeats, Roster::MAX_SEATS), clamped to 0..16.
  uint8_t seatLimit() const;
  // max(2, seatsMin), at most 255: the fewest seats Start accepts. Above seatLimit() no Start is possible.
  uint8_t startSeats() const;
};

enum class HostVerdict : uint8_t { Joinable, DifferentVersion };

// Equal package hash, and equal surface when either device's level is a preview. The wire carries a peer's level,
// not whether it is frozen; a peer at or below this device's level is frozen when this device's level is
// (ApiLevel.h: every level below a frozen one is), a newer peer's level is taken as a preview. So a frozen older
// device refuses a frozen newer peer whose surface differs; v1 has one level, so nothing differs today, and telling
// them apart would take a wire flag.
HostVerdict versionVerdict(const LobbyGame& own, const std::array<uint8_t, Protocol::PKG_HASH_BYTES>& peerHash,
                           uint8_t peerApi, uint32_t peerSurface);

// Every lobby timing value; none is a literal anywhere else. Unmeasured: R12 confirms them on a radio.
struct LobbySettings {
  uint32_t advertMs = 500;       // host: ADVERT interval (a seat change advertises at once)
  uint32_t hostExpireMs = 3000;  // guest: a host not heard for this long leaves the list (crossplay kCandidateForgetMs)
  uint32_t advertGraceMs = 1000;   // guest, started: a same-session ADVERT within this long of ACCEPT is a late copy
  uint32_t refusalAnswerMs = 200;  // host: one ABORT answer per refused peer per this long (crossplay kJoinIntervalMs)
  ReliableLink::Settings link;     // resend 400, silence 10,000, keepalive 1,000; maxPeers is set by the lobby
};

// True once `span` ms have passed since `since`; a clock that stepped back never fires.
inline bool elapsed(const uint64_t now, const uint64_t since, const uint32_t span) {
  return now >= since && now - since >= span;
}

// ABORT(left) is unsequenced, so a cancel sends it this many times (crossplay kByeRepeats).
inline constexpr uint8_t CANCEL_ABORT_COPIES = 3;

// How a lobby learned the match was over before the match session took the link.
struct LobbyEnd {
  enum class Kind : uint8_t { None, Abort, Silent };
  Kind kind = Kind::None;
  Protocol::AbortReason reason = Protocol::AbortReason::Left;
  PeerHandle peer;
};

// A session id for a lobby: next32() until nonzero, at most 8 draws, then 1 (unreachable with a real source).
uint32_t drawSession(IRandom& random);

// After Start a lobby stays the link's Sink, and passes what it is given on to the match's sink once that is attached.
// Until then a State is NoRoom (the peer resends it), and an Abort or a silent peer is latched (the first wins) and
// replayed into the sink on attach.
class LobbyHandOff {
 public:
  // Forwards from now on; replays a latched end into `next` once. `session` is the lobby's, for a synthesized Abort.
  void attach(ReliableLink::Sink& next, uint32_t session);
  bool attached() const { return next_ != nullptr; }
  ReliableLink::Verdict forward(PeerHandle from, const Protocol::Frame& frame);
  void forwardSilent(PeerHandle peer);
  // An Abort the lobby itself decided on (a host heard broadcasting again): forwarded as a frame of type Abort.
  void forwardAbort(PeerHandle from, Protocol::AbortReason reason);
  // The latched end; None when nothing arrived before attach. Kept readable after the replay.
  LobbyEnd end() const { return end_; }

 private:
  void deliverAbort(PeerHandle from, Protocol::AbortReason reason);
  void latch(LobbyEnd::Kind kind, PeerHandle peer, Protocol::AbortReason reason);

  ReliableLink::Sink* next_ = nullptr;
  uint32_t session_ = 0;
  LobbyEnd end_;
};

}  // namespace GameCore
