#pragma once

#include <EndSlot.h>
#include <MatchSink.h>
#include <ReliableLink.h>
#include <StateQueue.h>

#include <cstdint>
#include <span>

#include "GameLinkLog.h"

// The sink NearbySession attaches behind the lobby (LobbyHandOff::attach): GameCore::MatchSink, plus what only the
// device side has, the LINK lines and the link's peer table. It runs inside ReliableLink::tick() on the GameLink task,
// under NearbySession's lock, and calls nothing in the match.
//
// MatchSink refuses a frame from a handle that is no seat and counts it, but it cannot forget() the handle (it knows
// no link), and ReliableLink keeps a peer registered by a frame it offered until forget(). So after the sink answers,
// a frame or a silence report from a handle with no seat is followed by forget(handle) (allowed inside onFrame and
// onPeerSilent), and a third device cannot fill the peer table and starve the real peer. The lobbies already forget
// the strangers they see, so this is the second guard, for the frames that reach the match.
class GameLinkSinkGate final : public GameCore::ReliableLink::Sink {
 public:
  using Verdict = GameCore::ReliableLink::Verdict;

  // `link` is the ReliableLink whose sink this is; null (or bind() later) for a test that builds the sink first.
  GameLinkSinkGate(GameCore::StateQueue& queue, GameCore::EndSlot& end, GameCore::ReliableLink* link, uint32_t session,
                   std::span<const GameCore::SeatPeer> peers)
      : queue(queue), link(link), session(session), sink(queue, end, peers) {}

  void bind(GameCore::ReliableLink& reliable) { link = &reliable; }

  Verdict onFrame(const GameCore::PeerHandle from, const GameCore::Protocol::Frame& frame) override {
    const Verdict verdict = sink.onFrame(from, frame);
    const uint8_t seat = sink.seatOf(from);
    if (seat == 0) {
      if (link != nullptr) link->forget(from);
      return verdict;
    }
    if (frame.header.type == GameCore::Protocol::Type::State) {
      if (verdict == Verdict::Taken) {
        GameLinkLog::stateReceived(session, frame.state.ver, seat);
      } else if (verdict == Verdict::NoRoom) {
        GameLinkLog::queueFull(session, frame.state.ver, queue.overflows());
      }
    } else if (frame.header.type == GameCore::Protocol::Type::Abort) {
      GameLinkLog::abortReceived(session, frame.abortReason, seat);
      abortSeats |= seatBit(seat);
    }
    return verdict;
  }

  // Whether `seat` sent an ABORT (any reason): its session is over, so NearbySession::leave sends it no goodbye. Per
  // seat (R3, AD-11): with more seats the others still get one.
  bool abortHeardFrom(const uint8_t seat) const { return (abortSeats & seatBit(seat)) != 0; }

  void onPeerSilent(const GameCore::PeerHandle peer) override {
    sink.onPeerSilent(peer);
    if (sink.seatOf(peer) == 0 && link != nullptr) link->forget(peer);
  }

  const GameCore::MatchSink::Stats& stats() const { return sink.stats(); }

 private:
  GameCore::StateQueue& queue;
  GameCore::ReliableLink* link;
  uint32_t session;
  GameCore::MatchSink sink;
  // Bit s: seat s sent an ABORT (seats are 1..Roster::MAX_SEATS, 16).
  uint32_t abortSeats = 0;

  static constexpr uint32_t seatBit(const uint8_t seat) { return seat < 32 ? 1u << seat : 0u; }
};
