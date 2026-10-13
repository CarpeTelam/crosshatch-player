#pragma once

// The rig NearbyLobbyTest.cpp builds on (entry 10 of epic-play-nearby): the lobby screens of several devices, and the
// radio they open their links with, over the screen doubles, FakeLink, and the GameLink tasks of NearbySession.
//
// Test doubles (orchestrated-epics.md, Build-agent brief):
// - FakeNearbyRadio stands in for the device's radio (NearbyRadio's own members in GameLobbyActivity.cpp: EspNowLink
//   and its label, GameClock and GameRandom, NearbyProbe::read, and later UdpLink). It hands out a FakeLink end of one
//   FakeLinkNet wrapped in nearby::ObservedLink, a label of the form EspNow::makeLabel("X4 Pro", mac) makes, the
//   FakeRtosClock the NearbySession suites use (the clock FakeRtos.h moves by hand and by every vTaskDelay), and a
//   FixedRandom; the probe answers what the test sets.
//   NearbyLobbyHarnessTest.TheFakeRadioHandsOutWhatTheDeviceRadioDoes pins the label form, that every open() is an
//   independent link, and that the device's own open() returns none (entry 11 flips that on purpose). More permissive
//   than the device: open() never fails unless a test says so and takes no time (the device brings up Wi-Fi, ESP-NOW,
//   and its peer table); the probe answers at once; the clock runs faster than real time while tasks sleep
//   (nearby::waitFor adds 2 ms per real millisecond on top), so a bound in fake milliseconds (the 100 ms poll, the
//   1,000 ms list refresh, the 3,000 ms live window) is shorter in real time than on a device; and a FakeLink loses
//   only what a test scripts.
// - Dev stands in for one device: its own panel (a GfxRenderer double), its own input, and its own lobby, then match.
//   More permissive than two devices: one card, one log, one ActivityManager double, one ButtonNavigator input, and the
//   test thread alternating frame() on each device in place of two cores (NearbyMatchTest.cpp's note). The render task
//   is the test thread rendering between loop passes, so a race between the loop and render tasks is never staged here.
// - NearbyProbeScript.h's read() stands in for NearbyProbe::read() (device only): the readings are the test's.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "EspNowLink.h"
#include "HostCapsScript.h"
#include "LauncherSupport.h"
#include "MatchSupport.h"
#include "NearbyProbeScript.h"
#include "NearbySessionSupport.h"
#include "SaveSupport.h"
#include "activities/games/GameLobbyActivity.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GameModeActivity.h"
#include "util/ButtonNavigator.h"

namespace nearbylobby {

using Button = MappedInputManager::Button;
using nearby::ObservedLink;

// A string of the lobby with its counts filled in, as the screen prints it.
inline std::string fmt(const char* format, const unsigned a, const unsigned b = 0) {
  char text[96];
  std::snprintf(text, sizeof(text), format, a, b);
  return text;
}
inline std::string joinedLine(const unsigned seat) { return fmt(tr(STR_GAMES_NEARBY_PLAYER_JOINED), seat); }
inline std::string leftLine(const unsigned seat) { return fmt(tr(STR_GAMES_NEARBY_PLAYER_LEFT), seat); }
inline std::string seatsLine(const unsigned taken, const unsigned max) {
  return fmt(tr(STR_GAMES_NEARBY_SEATS), taken, max);
}

// The clock the lobby reads: FakeRtosClock, plus a one-shot jump for the test thread. `jumpOnce(skip, ms)` lets the
// test thread's next `skip` reads through and answers the one after with `ms` more, then goes back to the plain
// clock. How a test stages a reading taken later than the one before it inside a single loop pass (the lobby's poll
// and the Start tap that follows it), which the device's 100 ms poll cadence makes a window no fake-time step can
// land in. Another thread's reads (a link task's) are never touched.
class JumpOnceClock final : public GameCore::IClock {
 public:
  uint64_t nowMs() const override {
    const uint64_t now = fakertos::millis();
    if (armed.load() && std::this_thread::get_id() == owner) {
      if (skip > 0) {
        --skip;
      } else {
        armed.store(false);
        return now + jump;
      }
    }
    return now;
  }
  void jumpOnce(const int skipReads, const uint64_t ms) {
    owner = std::this_thread::get_id();
    skip = skipReads;
    jump = ms;
    armed.store(true);
  }
  bool spent() const { return !armed.load(); }

 private:
  mutable std::atomic<bool> armed{false};
  mutable int skip = 0;
  std::thread::id owner;
  uint64_t jump = 0;
};

class FakeNearbyRadio final : public NearbyRadio {
 public:
  FakeNearbyRadio() : net(time, 1), random_({0x5E551011u, 0x5E551012u, 0x5E551013u}) {}

  NearbyProbe::Readings probe() override {
    ++probeReads;
    return readings;
  }

  Opened open() override {
    ++opens;
    Opened opened;
    if (failOpen) return opened;
    links.emplace_back(net);
    probes.emplace_back();
    opened.link = std::make_unique<ObservedLink>(links.back(), probes.back());
    EspNow::Mac mac;
    mac.bytes[4] = 0x3F;
    mac.bytes[5] = static_cast<uint8_t>(0x2A + links.size() - 1);
    const EspNow::Label label = EspNow::makeLabel("X4 Pro", mac);
    std::memcpy(opened.label, label.bytes, label.length);
    opened.labelLength = label.length;
    return opened;
  }

  const GameCore::IClock& clock() override { return lobbyClock; }
  GameCore::IRandom& random() override { return random_; }
  GameCore::LobbySettings lobbySettings() override { return settings; }
  NearbySessionOptions sessionOptions() override { return nearby::fastOptions(); }

  // The label the n-th open() (0 first) made.
  static std::string labelOfOpen(const size_t n) {
    EspNow::Mac mac;
    mac.bytes[4] = 0x3F;
    mac.bytes[5] = static_cast<uint8_t>(0x2A + n);
    const EspNow::Label label = EspNow::makeLabel("X4 Pro", mac);
    return std::string(reinterpret_cast<const char*>(label.bytes), label.length);
  }

  nearby::FakeRtosClock time;
  JumpOnceClock lobbyClock;
  GameCore::LinkTest::FakeLinkNet net;
  GameCore::LobbyTest::FixedRandom random_;
  NearbyProbe::Readings readings{false, SIZE_MAX};
  GameCore::LobbySettings settings = nearby::fastSettings();
  bool failOpen = false;
  int opens = 0;
  int probeReads = 0;
  std::deque<GameCore::LinkTest::FakeLink> links;  // the n-th open()'s end
  std::deque<nearby::LinkProbe> probes;            // and what its link saw
};

// The game every device plays: the nearby fixture, two seats, installed as /.games/nearby.
inline GameCore::Manifest nearbyManifest() {
  GameCore::Manifest manifest = match::manifestOf("nearby", "Nearby");
  manifest.seatsMin = 2;
  manifest.seatsMax = 2;
  manifest.modes = GameCore::Manifest::MODE_PASS | GameCore::Manifest::MODE_NEARBY;
  return manifest;
}

// One device: its panel, input, lobby, and, once the lobby handed over, its match.
struct Dev {
  std::string name;
  std::unique_ptr<GfxRenderer> renderer;
  std::unique_ptr<MappedInputManager> input;
  std::unique_ptr<GameLobbyActivity> lobby;
  std::unique_ptr<GameMatchActivity> match;
  screen::RecordingTarget* target = nullptr;  // the lobby's, while it lives
  bool asked = false;                         // the loop asked for a render since the last one
  bool lobbyExited = false;                   // onExit has run
  bool matchExited = false;
  Activity* active() const { return match ? static_cast<Activity*>(match.get()) : static_cast<Activity*>(lobby.get()); }
};

class LobbyRigTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    hostcaps::reset();
    hostcaps::script().nearby = true;  // a host whose lobby speaks for a nearby-capable device
    nearbyprobe::reset();
    radio = std::make_unique<FakeNearbyRadio>();
    GameLobbyActivity::setRadio(radio.get());
    match::installFixtureAs("nearby", "nearby");
  }

  void TearDown() override {
    fakertos::release();
    for (std::unique_ptr<Dev>& d : devs) dropDev(*d);
    devs.clear();
    GameLobbyActivity::setRadio(nullptr);
    radio.reset();
    ScreenTest::TearDown();
  }

  // What the manager does when a screen goes: onExit under the lock, then the destructor under it.
  static void dropDev(Dev& d) {
    if (d.match) {
      if (!d.matchExited) activityManager.exitHolding(*d.match);
      activityManager.destroyHolding(d.match);
    }
    if (d.lobby) {
      if (!d.lobbyExited) activityManager.exitHolding(*d.lobby);
      activityManager.destroyHolding(d.lobby);
    }
  }

  // ---- devices ----

  // A device on its lobby's Choose screen, entered and drawn. `hash` is its package hash (another one makes its
  // package another version).
  Dev& addDev(const std::string& name, const uint8_t (&hash)[GamePkg::HASH_BYTES] = savefile::HASH_A,
              const GameCore::SettingValues& settings = {}) {
    auto d = std::make_unique<Dev>();
    d->name = name;
    d->renderer = std::make_unique<GfxRenderer>(480, 800);
    match::addFonts(*d->renderer);
    d->input = std::make_unique<MappedInputManager>(gpio, *d->renderer);
    d->lobby = std::make_unique<GameLobbyActivity>(*d->renderer, *d->input, nearbyManifest(), hash, settings);
    d->target = &launcher::TargetOf::of(*d->lobby);
    ButtonNavigator::setMappedInputManager(*d->input);
    d->lobby->onEnter();
    devs.push_back(std::move(d));
    Dev& added = *devs.back();
    render(added);
    return added;
  }

  // ---- the loop ----

  static int asks() { return activityManager.asks.updates + activityManager.asks.immediateUpdates; }

  // One loop pass of `d`, the main loop's: the touch sample, loop(), the frame's input over. A match the lobby handed
  // over to is taken out of the manager's record, and the lobby goes as the manager lets a replaced screen go.
  void frame(Dev& d) {
    ButtonNavigator::setMappedInputManager(*d.input);
    d.input->update();
    const int before = asks();
    const size_t replaced = activityManager.replacements.size();
    d.active()->loop();
    d.input->clear();
    if (asks() > before) d.asked = true;
    if (activityManager.replacements.size() > replaced) takeMatch(d);
  }

  void takeMatch(Dev& d) {
    ASSERT_FALSE(activityManager.replacements.empty());
    std::unique_ptr<Activity> replacement = std::move(activityManager.replacements.back());
    activityManager.replacements.pop_back();
    auto* started = dynamic_cast<GameMatchActivity*>(replacement.get());
    ASSERT_NE(started, nullptr) << "what the lobby replaces itself with is a GameMatchActivity";
    replacement.release();
    d.match.reset(started);
    if (d.lobby) {
      activityManager.exitHolding(*d.lobby);
      d.lobbyExited = true;
      activityManager.destroyHolding(d.lobby);
    }
    d.target = nullptr;
    d.match->onEnter();
    d.asked = true;
  }

  void render(Dev& d) {
    d.asked = false;
    if (d.target) d.target->forget();
    d.active()->render(RenderLock(*d.active()));
  }

  // One pass of each device, drawing what its loop asked for.
  void step() {
    for (size_t i = 0; i < devs.size(); ++i) {
      Dev& d = *devs[i];
      if (!d.active()) continue;
      frame(d);
      if (d.asked) render(d);
    }
  }

  bool run(const std::function<bool()>& done, const int capRealMs = 10000) {
    return nearby::waitFor(
        [&] {
          step();
          return done();
        },
        capRealMs);
  }

  // `polls` passes of every loop, for a test that waits for nothing to happen.
  void idle(const int polls) {
    int left = polls;
    nearby::waitFor([&] {
      step();
      return --left <= 0;
    });
  }

  // Lets `ms` of fake time pass with every loop running (nearby::waitFor moves the clock and the link tasks sleep on
  // it).
  void advance(const uint32_t ms) {
    const uint64_t until = fakertos::millis() + ms;
    nearby::waitFor(
        [&] {
          step();
          return fakertos::millis() >= until;
        },
        60000);
  }

  // ---- reading a device ----

  // Every text of `d`'s last push, one per line.
  static std::vector<std::string> lines(const Dev& d) {
    return d.renderer->shown.empty() ? std::vector<std::string>{} : d.renderer->shown.back().texts;
  }
  static std::string screenText(const Dev& d) {
    std::string text;
    for (const std::string& line : lines(d)) text += line + "\n";
    return text;
  }
  // Some line of `d`'s last push is exactly `line`.
  static bool shows(const Dev& d, const std::string& line) {
    for (const std::string& text : lines(d))
      if (text == line) return true;
    return false;
  }
  // The line right after `label` (a row's second line or value), or "" when there is none.
  static std::string lineAfter(const Dev& d, const std::string& label) {
    const std::vector<std::string> all = lines(d);
    for (size_t i = 0; i + 1 < all.size(); ++i)
      if (all[i] == label) return all[i + 1];
    return "";
  }
  static size_t pushes(const Dev& d) { return d.renderer->shown.size(); }

  // ---- input ----

  // Taps the row of `d`'s lobby drawn with `label`; false when none is drawn.
  bool tapRow(Dev& d, const std::string& label) {
    for (const screen::DrawnText& drawn : d.target->drawn) {
      if (drawn.text != label) continue;
      d.input->tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
      frame(d);
      if (d.asked) render(d);
      return true;
    }
    return false;
  }
  void click(Dev& d, const Button button) {
    d.input->click(button);
    frame(d);
    if (d.asked) render(d);
  }

  // ---- the lobby's flows ----

  // Taps Host a game on `d` and waits for the host screen.
  void host(Dev& d) {
    ASSERT_TRUE(tapRow(d, tr(STR_GAMES_NEARBY_HOST))) << screenText(d);
    ASSERT_TRUE(run([&] { return shows(d, tr(STR_GAMES_NEARBY_WAITING_FOR_PLAYER)); })) << screenText(d);
  }
  // Taps Join a game on `d` and waits for the host list: the choose rows are gone and Cancel is the list's last row (a
  // host already hosting may be listed before the empty list is ever drawn).
  void joinScreen(Dev& d) {
    ASSERT_TRUE(tapRow(d, tr(STR_GAMES_NEARBY_JOIN))) << screenText(d);
    ASSERT_TRUE(run([&] { return !shows(d, tr(STR_GAMES_NEARBY_JOIN)) && shows(d, tr(STR_CANCEL)); })) << screenText(d);
  }
  // `guest` taps the host `label` it lists, and both lobbies show the join.
  void joinHost(Dev& guest, const std::string& label, Dev& hostDev) {
    ASSERT_TRUE(run([&] { return shows(guest, label); })) << "the guest does not list " << label << "\n"
                                                          << screenText(guest);
    ASSERT_TRUE(tapRow(guest, label)) << screenText(guest);
    ASSERT_TRUE(run([&] {
      return shows(guest, tr(STR_GAMES_NEARBY_WAITING_FOR_START)) && shows(hostDev, joinedLine(2));
    })) << "host:\n"
        << screenText(hostDev) << "guest:\n"
        << screenText(guest);
  }

  std::unique_ptr<FakeNearbyRadio> radio;
  std::vector<std::unique_ptr<Dev>> devs;
};

}  // namespace nearbylobby
