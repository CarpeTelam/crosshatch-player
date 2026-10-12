#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IClock.h"
#include "ILink.h"
#include "Lobby.h"
#include "ReliableLink.h"
#include "Roster.h"

// The guest's side of the lobby (R3; spine AD-11 Nearby, AD-13): lists the hosts advertising this game, joins one,
// and waits for Start. It owns the ReliableLink as its Sink; one task calls it at a time, no locking. A peer is a
// PeerHandle, time is the IClock. While Browsing the link's session is 0, so only ADVERT reaches it; join() sets the
// host's session.
//
// Adapted from crossplay 0a1d17f src/apps_local/link/LinkSession.cpp and host-tests/link/test_link.cpp (MIT, notice in
// ReliableLink.cpp): the candidate list that forgets a host not heard for kCandidateForgetMs, the repeated unreliable
// goodbye (kByeRepeats), and the rule that a host heard broadcasting again after the match began ended it (its
// Hello-after-match rule).
namespace GameCore {

enum class GuestState : uint8_t { Browsing, Joining, Joined, Started, Refused, HostGone, Cancelled };
enum class Refusal : uint8_t { None, Full, VersionMismatch };
enum class GoneReason : uint8_t { None, Left, Silent };
enum class JoinResult : uint8_t { Sent, NotBrowsing, UnknownHost, DifferentVersion, LinkRefused };

struct HostEntry {
  PeerHandle handle;
  uint32_t session = 0;
  std::array<uint8_t, Protocol::MAX_HOST_LABEL_BYTES> label{};
  uint8_t labelLen = 0;
  uint8_t hostApi = 0;  // the host's API level, from its ADVERT
  uint8_t seatsTaken = 0, seatsMax = 0;
  HostVerdict verdict = HostVerdict::Joinable;
  uint64_t heardMs = 0;
};

class GuestLobby final : private ReliableLink::Sink {
 public:
  static constexpr size_t MAX_HOSTS = 8;

  GuestLobby(ILink& link, const IClock& clock, const LobbyGame& game, const LobbySettings& settings = {});
  GuestLobby(const GuestLobby&) = delete;
  GuestLobby& operator=(const GuestLobby&) = delete;

  bool ok() const { return link_.ok(); }
  // link.tick(), then: Joining becomes Joined once the JOIN is acked; a listed host not heard for hostExpireMs leaves.
  void tick();
  GuestState state() const { return state_; }
  size_t hostCount() const { return hostCount_; }
  // First heard first. An index at or above hostCount() returns entry 0, which may be empty: check hostCount() first.
  const HostEntry& host(size_t index) const { return hosts_[index < hostCount_ ? index : 0]; }
  // Browsing only: JOIN to the listed host (handle, session). DifferentVersion and the other refusals send nothing.
  JoinResult join(PeerHandle host, uint32_t session);
  // Meaningful once a join was sent (until backToList); an empty entry before.
  const HostEntry& joinedHost() const { return joined_; }
  Refusal refusal() const { return refusal_; }
  GoneReason gone() const { return gone_; }
  // From Refused or HostGone: forgets the host, session 0, an empty list, Browsing.
  void backToList();
  // Joining, Joined, or Started: ABORT(left) to the host, then Cancelled. Any other state: Cancelled, nothing sent.
  void cancel();
  // Valid once Started.
  const Roster& roster() const { return roster_; }
  uint8_t seat() const { return seat_; }
  uint32_t session() const { return link_.session(); }
  PeerHandle peerForSeat(uint8_t seat) const;  // the host for seat 1; NO_PEER otherwise (a guest knows only the host)
  ReliableLink& link() { return link_; }
  void handOff(ReliableLink::Sink& next) { handOff_.attach(next, link_.session()); }
  LobbyEnd ended() const { return handOff_.end(); }

 private:
  using Verdict = ReliableLink::Verdict;

  Verdict onFrame(PeerHandle from, const Protocol::Frame& frame) override;
  void onPeerSilent(PeerHandle peer) override;
  void onAdvert(PeerHandle from, const Protocol::Frame& frame);
  Verdict onAccept(PeerHandle from, const Protocol::Accept& accept);
  Verdict onAbort(const Protocol::Frame& frame);
  void hostEnded();  // the wait is over: the link forgets the host
  void expireHosts();
  bool waiting() const { return state_ == GuestState::Joining || state_ == GuestState::Joined; }

  LobbyGame game_;
  LobbySettings settings_;
  const IClock& clock_;
  ReliableLink link_;
  GuestState state_ = GuestState::Browsing;
  Refusal refusal_ = Refusal::None;
  GoneReason gone_ = GoneReason::None;
  std::array<HostEntry, MAX_HOSTS> hosts_{};
  size_t hostCount_ = 0;
  HostEntry joined_;
  Roster roster_;
  uint8_t seat_ = 0;
  uint64_t startedAtMs_ = 0;
  bool advertEnded_ = false;
  LobbyHandOff handOff_;
};

}  // namespace GameCore
