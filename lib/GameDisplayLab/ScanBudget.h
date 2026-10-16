#pragma once

#include <cstdint>

// The display lab's scan bookkeeping, kept pure so the host suite pins the rules the plan's matrix states: a scan whose
// facade call took longer than `timeoutUs` timed out (its run ends and the panel is cleaned), one whose call came back
// in under `droppedBelowUs` was dropped by the facade before it reached the panel (its time is no measurement), and
// `cap` scans since the last cleaning refresh make the next scan clean first. The activity owns one of these per
// session.
namespace GameDisplayLab {

class ScanBudget {
 public:
  enum class Outcome : uint8_t { Done, TimedOut, Dropped };

  constexpr ScanBudget(const uint32_t cap, const uint32_t timeoutUs, const uint32_t droppedBelowUs)
      : cap_(cap), timeoutUs_(timeoutUs), droppedBelowUs_(droppedBelowUs) {}

  // True when the next scan must be preceded by a cleaning refresh.
  constexpr bool cleanDue() const { return sinceClean_ >= cap_; }
  // A cleaning refresh ran.
  constexpr void cleaned() { sinceClean_ = 0; }
  // A scan was delivered and its facade call took `busyUs`. A dropped scan still counts: the call was made.
  constexpr Outcome record(const uint32_t busyUs) {
    ++sinceClean_;
    ++total_;
    if (busyUs < droppedBelowUs_) return Outcome::Dropped;
    return busyUs > timeoutUs_ ? Outcome::TimedOut : Outcome::Done;
  }

  constexpr uint32_t sinceClean() const { return sinceClean_; }
  constexpr uint32_t total() const { return total_; }

 private:
  uint32_t cap_;
  uint32_t timeoutUs_;
  uint32_t droppedBelowUs_;
  uint32_t sinceClean_ = 0;
  uint32_t total_ = 0;
};

}  // namespace GameDisplayLab
