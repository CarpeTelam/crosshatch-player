#include "MatchSink.h"

namespace GameCore {

MatchSink::MatchSink(StateQueue& queue, EndSlot& end, const std::span<const SeatPeer> peerList)
    : queue(queue), end(end) {
  for (const SeatPeer& peer : peerList) {
    if (peerCount == peers.size()) break;
    peers[peerCount++] = peer;
  }
}

uint8_t MatchSink::seatOf(const PeerHandle handle) const {
  if (!handle.valid()) return 0;
  for (size_t i = 0; i < peerCount; ++i) {
    if (peers[i].handle == handle) return peers[i].seat;
  }
  return 0;
}

ReliableLink::Verdict MatchSink::onFrame(const PeerHandle from, const Protocol::Frame& frame) {
  switch (frame.header.type) {
    case Protocol::Type::State:
    case Protocol::Type::Abort:
      break;
    default:
      return Verdict::Refused;  // Join, Accept, Advert: the lobby's, not the match's
  }
  const uint8_t seat = seatOf(from);
  if (seat == 0) {
    ++counts.strangers;
    return Verdict::Refused;
  }
  if (frame.header.type == Protocol::Type::Abort) {
    end.latch(frame.abortReason);
    return Verdict::Taken;
  }
  switch (queue.push(seat, frame.state.ver, frame.state.status, frame.state.snapshot)) {
    case StateQueue::Push::Queued:
      ++counts.queued;
      return Verdict::Taken;
    case StateQueue::Push::Replaced:
      ++counts.replaced;
      return Verdict::Taken;
    case StateQueue::Push::Full:
      return Verdict::NoRoom;
    case StateQueue::Push::Invalid:
      break;
  }
  return Verdict::Refused;
}

void MatchSink::onPeerSilent(const PeerHandle peer) {
  if (seatOf(peer) != 0) end.latch(Protocol::AbortReason::Timeout);
}

}  // namespace GameCore
