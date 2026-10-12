#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "IClock.h"
#include "ILink.h"
#include "IRandom.h"
#include "Lobby.h"
#include "ReliableLink.h"
#include "Roster.h"

// The host's side of the lobby (R3; spine AD-11 Nearby, AD-13): draws the session, advertises the game and its seats,
// seats guests in JOIN arrival order, starts, and answers a JOIN it cannot seat with ABORT. It owns the ReliableLink
// as its Sink; one task calls it at a time (as ReliableLink), no locking. Nothing here knows a radio: a peer is a
// PeerHandle, time is the IClock, the session id comes from the IRandom.
//
// Seats: the host is seat 1; guests are kept in arrival order and a leaver closes the gap, so the seats given at Start
// (guest rank + 2) are always contiguous 1..n. seatsTaken() counts the host's seat.
//
// Adapted from crossplay 0a1d17f src/apps_local/link/LinkSession.cpp and host-tests/link/test_link.cpp (MIT, notice
// in ReliableLink.cpp): the ADVERT clock apart from the resend clock, the rate-limited answer to a refused JOIN
// (kJoinIntervalMs), and the repeated unreliable goodbye (kByeRepeats).
namespace GameCore {

enum class HostState : uint8_t { Waiting, Started, Closed };
enum class StartResult : uint8_t { Started, NotEnoughSeats, NotWaiting, LinkFailed };
enum class HostEventKind : uint8_t { GuestJoined, GuestLeft, GuestSilent };
struct HostEvent {
  HostEventKind kind = HostEventKind::GuestJoined;
  uint8_t seat = 0;  // the seat the guest held in arrival order (2 = the first guest)
};

class HostLobby final : private ReliableLink::Sink {
 public:
  static constexpr size_t EVENT_CAPACITY = 8;

  HostLobby(ILink& link, const IClock& clock, IRandom& random, const LobbyGame& game, std::span<const uint8_t> label,
            const LobbySettings& settings = {});
  HostLobby(const HostLobby&) = delete;
  HostLobby& operator=(const HostLobby&) = delete;

  bool ok() const;  // the link allocated and game.seatLimit() >= 2
  // link.tick(), then an ADVERT when Waiting and one is due (the first tick, the interval, or a seat change).
  void tick();
  HostState state() const { return state_; }
  uint32_t session() const { return session_; }
  uint8_t seatsTaken() const { return static_cast<uint8_t>(guestCount_ + 1); }
  uint8_t seatLimit() const { return game_.seatLimit(); }
  bool canStart() const { return seatsTaken() >= game_.startSeats(); }
  StartResult start();
  void cancel();  // Waiting or Started: ABORT(left) to every seated guest, then Closed
  // The oldest event; the queue keeps the newest EVENT_CAPACITY, so a screen that falls behind resyncs from
  // seatsTaken().
  bool poll(HostEvent& out);
  // Valid once Started.
  const Roster& roster() const { return roster_; }
  PeerHandle peerForSeat(uint8_t seat) const;  // NO_PEER before Start, for seat 1, and for an unknown seat
  ReliableLink& link() { return link_; }
  void handOff(ReliableLink::Sink& next) { handOff_.attach(next, session_); }
  LobbyEnd ended() const { return handOff_.end(); }

 private:
  using Verdict = ReliableLink::Verdict;
  struct Guest {
    PeerHandle handle;
    uint8_t api = 0;  // the guest's own level, from its JOIN
  };
  struct Answered {
    PeerHandle peer;
    uint64_t atMs = 0;
  };
  static constexpr size_t MAX_GUESTS = Roster::MAX_SEATS - 1;
  static constexpr size_t ANSWER_RING = 4;

  Verdict onFrame(PeerHandle from, const Protocol::Frame& frame) override;
  void onPeerSilent(PeerHandle peer) override;
  Verdict onJoin(PeerHandle from, const Protocol::Join& join);
  Verdict onAbort(PeerHandle from, const Protocol::Frame& frame);
  Verdict refuse(PeerHandle from, Protocol::AbortReason reason);
  bool mayAnswer(PeerHandle peer);
  size_t guestIndex(PeerHandle handle) const;  // guestCount_ when not seated
  void removeGuest(size_t index, HostEventKind kind);
  void pushEvent(HostEventKind kind, uint8_t seat);
  void sendAdvert();
  void sendAbortToGuests();
  int32_t lowestApi() const;

  LobbyGame game_;
  LobbySettings settings_;
  const IClock& clock_;
  uint32_t session_;
  std::array<uint8_t, Protocol::MAX_HOST_LABEL_BYTES> label_{};
  uint8_t labelLen_ = 0;
  ReliableLink link_;
  HostState state_ = HostState::Waiting;
  std::array<Guest, MAX_GUESTS> guests_{};
  size_t guestCount_ = 0;
  std::array<Answered, ANSWER_RING> answered_{};
  size_t answeredNext_ = 0;
  std::array<HostEvent, EVENT_CAPACITY> events_{};
  size_t eventHead_ = 0;
  size_t eventCount_ = 0;
  bool advertNow_ = true;
  uint64_t lastAdvertMs_ = 0;
  Roster roster_;
  LobbyHandOff handOff_;
};

}  // namespace GameCore
