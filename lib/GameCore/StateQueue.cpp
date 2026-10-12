#include "StateQueue.h"

#include <algorithm>

#include "Protocol.h"

namespace GameCore {

StateQueue::StateQueue(IStateStore& store) : store(store) {}

StateQueue::~StateQueue() { clear(); }

bool StateQueue::ok() const { return store.capacity() >= DEPTH + 1; }

StateQueue::Push StateQueue::push(const uint8_t fromSeat, const uint16_t ver, const Status& status,
                                  const std::span<const uint8_t> snapshot) {
  if (snapshot.empty() || snapshot.size() > Protocol::MAX_SNAPSHOT_BYTES) return Push::Invalid;

  // Only the newest entry can be replaced, and not when it ends a round: the rule ReliableLink::enqueue applies on
  // the send side, so order is kept (an older non-over entry before an `over` is never replaced).
  if (count > 0) {
    QueuedState& tail = at(count - 1);
    if (!tail.status.over && tail.fromSeat == fromSeat) {
      const std::span<uint8_t> buffer = store.bytes(tail.handle);
      if (buffer.size() < snapshot.size()) return Push::Invalid;
      std::copy(snapshot.begin(), snapshot.end(), buffer.begin());
      tail.ver = ver;
      tail.status = status;
      tail.length = static_cast<uint16_t>(snapshot.size());
      return Push::Replaced;
    }
  }

  if (count >= DEPTH) {
    ++overflowCount;
    return Push::Full;
  }
  const StateHandle handle = store.acquire();
  if (handle == NO_STATE_HANDLE) {
    ++overflowCount;
    return Push::Full;
  }
  const std::span<uint8_t> buffer = store.bytes(handle);
  if (buffer.size() < snapshot.size()) {
    store.release(handle);
    ++overflowCount;
    return Push::Full;
  }
  std::copy(snapshot.begin(), snapshot.end(), buffer.begin());
  QueuedState& slot = at(count);
  slot.handle = handle;
  slot.fromSeat = fromSeat;
  slot.ver = ver;
  slot.status = status;
  slot.length = static_cast<uint16_t>(snapshot.size());
  ++count;
  return Push::Queued;
}

bool StateQueue::take(QueuedState& out) {
  if (count == 0) return false;
  out = entries[first];
  entries[first] = QueuedState{};
  first = (first + 1) % DEPTH;
  --count;
  return true;
}

std::span<const uint8_t> StateQueue::bytes(const QueuedState& taken) {
  if (taken.handle == NO_STATE_HANDLE) return {};
  const std::span<uint8_t> buffer = store.bytes(taken.handle);
  return buffer.first(std::min<size_t>(taken.length, buffer.size()));
}

void StateQueue::release(QueuedState& taken) {
  store.release(taken.handle);
  taken.handle = NO_STATE_HANDLE;
}

void StateQueue::clear() {
  while (count > 0) {
    QueuedState dropped;
    take(dropped);
    release(dropped);
  }
  first = 0;
}

}  // namespace GameCore
