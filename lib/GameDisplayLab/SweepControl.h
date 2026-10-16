#pragma once

#include <atomic>
#include <cstdint>

// The transition test's request word (the display-lab epic, D11; story dlab-c2): how the screen's loop task (the
// owner) talks to the sweep task that runs the slots. One atomic word holds the requests (Stop, Run, Replay, Clean)
// and the task's InPanel state; a second holds the owner's heartbeat, the micros() of its last loop() pass. Pure and
// std-only, pinned by test_sweep_control.cpp. Times are a free-running microsecond clock; ages are wrap-safe.
//
// Owner: reset() before the task starts, then the controls (requestRun, requestReplay, requestClean, requestStop),
// heartbeat() once per loop() pass. Task: take() at each slot boundary, which hands over the requests and clears the
// one-shot ones (Replay, Clean); Run is a state, not a one-shot, so two presses before the task serves them cancel
// out. Around every scan and clean the task calls enterPanel(), and leavePanel() after: the owner reads insidePanel()
// after requestStop() to know whether the task may be waiting for the render lock. Every change to the word is one
// compare-exchange (change()), so a Stop set while enterPanel() runs is never missed: either the task entered first
// (and the owner sees InPanel), or its compare-exchange fails and it sees Stop. Compare-exchange rather than
// fetch_or/fetch_and: on the ESP32-S3 those are library calls the toolchain places in IRAM, and the image already
// links the compare-exchange one.
namespace GameDisplayLab {

class SweepControl {
 public:
  enum class Gate : uint8_t { Enter, Stop, Covered };

  // What take() hands the task.
  struct Requests {
    bool stop = false;
    bool run = false;
    bool replay = false;
    bool clean = false;
  };

  static constexpr uint32_t STOP = 1u << 0;
  static constexpr uint32_t RUN = 1u << 1;
  static constexpr uint32_t REPLAY = 1u << 2;
  static constexpr uint32_t CLEAN = 1u << 3;
  static constexpr uint32_t IN_PANEL = 1u << 4;

  // --- owner (the loop task) ---------------------------------------------------------------------------------------
  // A new run: running from a replay (clean, sweep from 0), the heartbeat at `nowUs`.
  void reset(const uint32_t nowUs) {
    heartbeat_.store(nowUs);
    word_.store(RUN | REPLAY);
  }
  void heartbeat(const uint32_t nowUs) { heartbeat_.store(nowUs); }
  // Microseconds since the last heartbeat; 0 when the heartbeat is newer than `nowUs` (another task read its clock
  // before the owner's last pass).
  uint32_t heartbeatAgeUs(const uint32_t nowUs) const {
    const int32_t age = static_cast<int32_t>(nowUs - heartbeat_.load());
    return age > 0 ? static_cast<uint32_t>(age) : 0;
  }
  void requestRun(const bool run) { change(run ? RUN : 0, run ? 0 : RUN); }
  // Clean, the sweep from 0, running.
  void requestReplay() { change(REPLAY | RUN, 0); }
  // Clean, paused: Clean set and Run cleared in one step.
  void requestClean() { change(CLEAN, RUN); }
  // Sticky: nothing clears it but reset().
  void requestStop() { change(STOP, 0); }
  bool runRequested() const { return (word_.load() & RUN) != 0; }
  bool insidePanel() const { return (word_.load() & IN_PANEL) != 0; }

  // --- task --------------------------------------------------------------------------------------------------------
  // The requests, with Replay and Clean cleared in the same step.
  Requests take() {
    const uint32_t word = change(0, REPLAY | CLEAN);
    Requests requests;
    requests.stop = (word & STOP) != 0;
    requests.run = (word & RUN) != 0;
    requests.replay = (word & REPLAY) != 0;
    requests.clean = (word & CLEAN) != 0;
    return requests;
  }
  // Before a scan or a clean at `nowUs`: Stop when a stop was asked, Covered when the owner's heartbeat is older than
  // `gapUs` (another screen covers the view), otherwise Enter, with InPanel set.
  Gate enterPanel(const uint32_t nowUs, const uint32_t gapUs) {
    uint32_t word = word_.load();
    while (true) {
      if ((word & STOP) != 0) return Gate::Stop;
      if (heartbeatAgeUs(nowUs) > gapUs) return Gate::Covered;
      if (word_.compare_exchange_weak(word, word | IN_PANEL)) return Gate::Enter;
    }
  }
  void leavePanel() { change(0, IN_PANEL); }

 private:
  // Sets `set` and clears `clear` in one compare-exchange; returns the word before.
  uint32_t change(const uint32_t set, const uint32_t clear) {
    uint32_t word = word_.load();
    while (!word_.compare_exchange_weak(word, (word | set) & ~clear)) {
    }
    return word;
  }

  std::atomic<uint32_t> word_{0};
  std::atomic<uint32_t> heartbeat_{0};
};

}  // namespace GameDisplayLab
