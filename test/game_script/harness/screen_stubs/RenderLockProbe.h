#pragma once

// What the RenderLock double (ScreenDoubles.cpp) counts. The lock is one non-recursive
// mutex, as the real manager's rendering mutex is. A thread that locks it while it holds
// it would hang the device (12cc816: an activity destructor, or onExit, taking the lock
// ActivityManager already holds); here it is counted in selfDeadlocks, and the second
// lock does not wait. (A Try-mode lock by its holder just fails, as on the device, and is not counted.)

#include <atomic>

namespace fakelock {

inline std::atomic<int> acquisitions{0};
inline std::atomic<int> selfDeadlocks{0};
inline std::atomic<int> taskAcquisitions{0};  // acquisitions made on a task thread
inline std::atomic<int> taskAttempts{0};      // attempts made on a task thread, whether or not they got the lock
bool held();                                  // some thread holds the lock now

inline void reset() {
  acquisitions = 0;
  selfDeadlocks = 0;
  taskAcquisitions = 0;
  taskAttempts = 0;
}

}  // namespace fakelock
