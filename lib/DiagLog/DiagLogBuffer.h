#pragma once

// The bounded buffer behind the diagnostics log hook (lib/DiagLog/DiagLog.h): any task appends a log line without
// blocking and without taking the SD card; one writer drains it. Header-only and free of the platform but for the lock,
// so the host tests run the same code the device does.
//
// Why a critical section and not a mutex: logPrintf is called from the loop, the VM task, the GameLink task, and the
// Wi-Fi tasks, at any priority, and a mutex held by a low-priority task could stall a high-priority one in the middle
// of a game frame. The lock here covers only a memcpy of at most 256 bytes (in at most two pieces, split at the end
// of the ring; its time on the device is unmeasured), so a task never waits on anything but that copy. On the device it
// is FreeRTOS's critical section (interrupts masked on this core, a spinlock against the other core); a plain spinlock
// would deadlock on the single-core C3 when a higher-priority task preempts the holder. Off the device (the host tests)
// it is a spinlock that yields.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#else
#include <thread>
#endif

namespace DiagLog {

#if defined(ESP_PLATFORM)
class CriticalLock {
 public:
  void lock() { portENTER_CRITICAL(&mux_); }
  void unlock() { portEXIT_CRITICAL(&mux_); }

 private:
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
using DefaultLock = CriticalLock;
#else
class SpinLock {
 public:
  void lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
  }
  void unlock() { flag_.clear(std::memory_order_release); }

 private:
  std::atomic_flag flag_;
};
using DefaultLock = SpinLock;
#endif

// A ring of whole lines: each record is one length byte and that many bytes of the line. A line that does not fit is
// dropped whole and counted (never partly stored), so the file never holds half a line from the buffer.
template <size_t Capacity, class Lock = DefaultLock>
class LineBuffer {
 public:
  // The longest record: logPrintf's buffer is 256 bytes with its terminator, so a line is at most 255 bytes.
  static constexpr size_t MAX_LINE = 255;
  static_assert(Capacity > MAX_LINE, "the buffer must hold at least one longest line");

  // Appends one line, which always ends in a newline in the buffer: a line without one (logPrintf cut it at its buffer
  // size) gets one, and a line longer than MAX_LINE is cut. False when there was no room: the line is dropped and
  // counted. An empty line is nothing to store and returns true.
  bool append(const char* text, size_t length) {
    if (length == 0) return true;
    const size_t body = length < MAX_LINE ? length : MAX_LINE - 1;
    const bool terminated = body == length && text[body - 1] == '\n';
    const size_t record = terminated ? body : body + 1;
    Guard guard(lock_);
    if (Capacity - used_ < record + 1) {
      dropped_++;
      droppedTotal_++;
      return false;
    }
    put(static_cast<uint8_t>(record));
    putBytes(text, body);
    if (!terminated) put(static_cast<uint8_t>('\n'));
    accepted_++;
    return true;
  }

  // Moves whole lines, oldest first, into out (at most maxBytes, and at least MAX_LINE so a line always fits); returns
  // the bytes written. Stops before a line that would not fit, so a caller loops until it returns 0.
  size_t drain(char* out, size_t maxBytes) {
    Guard guard(lock_);
    size_t produced = 0;
    while (used_ > 0) {
      const size_t record = data_[tail_];
      if (produced + record > maxBytes) break;
      tail_ = next(tail_);
      getBytes(out + produced, record);
      produced += record;
      used_ -= record + 1;
      drained_++;
    }
    return produced;
  }

  // The lines dropped since the last call (the writer reports them in the file), and resets that count.
  uint32_t takeDropped() {
    Guard guard(lock_);
    const uint32_t count = dropped_;
    dropped_ = 0;
    return count;
  }

  uint32_t acceptedTotal() const { return read(accepted_); }
  uint32_t drainedTotal() const { return read(drained_); }
  uint32_t droppedTotal() const { return read(droppedTotal_); }
  size_t used() const { return read(used_); }

 private:
  class Guard {
   public:
    explicit Guard(Lock& lock) : lock_(lock) { lock_.lock(); }
    ~Guard() { lock_.unlock(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

   private:
    Lock& lock_;
  };

  template <class T>
  T read(const T& value) const {
    Guard guard(lock_);
    return value;
  }
  static size_t next(size_t index) { return index + 1 == Capacity ? 0 : index + 1; }
  void put(uint8_t byte) {
    data_[head_] = byte;
    head_ = next(head_);
    used_++;
  }

  // Copies in at most two pieces, split where the ring ends. Called under the lock; room was checked by the caller.
  void putBytes(const char* source, size_t length) {
    const size_t first = length < Capacity - head_ ? length : Capacity - head_;
    std::memcpy(data_ + head_, source, first);
    if (first < length) std::memcpy(data_, source + first, length - first);
    head_ = (head_ + length) % Capacity;
    used_ += length;
  }
  void getBytes(char* out, size_t length) {
    const size_t first = length < Capacity - tail_ ? length : Capacity - tail_;
    std::memcpy(out, data_ + tail_, first);
    if (first < length) std::memcpy(out + first, data_, length - first);
    tail_ = (tail_ + length) % Capacity;
  }

  mutable Lock lock_;
  uint8_t data_[Capacity] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t used_ = 0;
  uint32_t dropped_ = 0;
  uint32_t droppedTotal_ = 0;
  uint32_t accepted_ = 0;
  uint32_t drained_ = 0;
};

}  // namespace DiagLog
