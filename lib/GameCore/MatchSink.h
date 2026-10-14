#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "EndSlot.h"
#include "ILink.h"
#include "ReliableLink.h"
#include "Roster.h"
#include "StateQueue.h"

namespace GameCore {

// A peer of the match and the seat it plays, from HostLobby / GuestLobby::peerForSeat.
struct SeatPeer {
  PeerHandle handle;
  uint8_t seat = 0;
};

// The ReliableLink::Sink of a nearby match (spine AD-18, R5), attached behind the lobby with LobbyHandOff::attach,
// which replays a latched ABORT or silence into it. It runs inside ReliableLink::tick() on the GameLink task and
// does two things with what the link brings: the peer's STATEs go to the StateQueue, and an ABORT or a lost peer to
// the EndSlot. It calls nothing in the match, and holds no allocator, log, or static state.
//
// A frame from a handle that is no peer is Refused (acked, so never offered again) and counted; STATE is never
// relayed between guests (more than two seats is deferred). A STATE the queue has no room for is NoRoom: the link
// does not ack it, the peer resends it, and nothing is lost.
class MatchSink final : public ReliableLink::Sink {
 public:
  using Verdict = ReliableLink::Verdict;

  struct Stats {
    uint32_t queued = 0;
    uint32_t replaced = 0;
    uint32_t strangers = 0;
  };

  // `peers` is copied (at most Roster::MAX_SEATS entries are kept).
  MatchSink(StateQueue& queue, EndSlot& end, std::span<const SeatPeer> peers);

  Verdict onFrame(PeerHandle from, const Protocol::Frame& frame) override;
  void onPeerSilent(PeerHandle peer) override;

  // The seat `handle` plays; 0 for a stranger.
  uint8_t seatOf(PeerHandle handle) const;
  const Stats& stats() const { return counts; }

 private:
  StateQueue& queue;
  EndSlot& end;
  std::array<SeatPeer, Roster::MAX_SEATS> peers;
  size_t peerCount = 0;
  Stats counts;
};

}  // namespace GameCore
