#include <gtest/gtest.h>

#include <vector>

#include "MatchLifecycle.h"

using namespace GameCore;

namespace {

constexpr MatchState ALL_STATES[] = {MatchState::Starting, MatchState::Playing, MatchState::Paused,
                                     MatchState::Over,     MatchState::Error,   MatchState::Leaving};
constexpr MatchEvent ALL_EVENTS[] = {MatchEvent::Started,   MatchEvent::Back,        MatchEvent::Home,
                                     MatchEvent::Resume,    MatchEvent::Leave,       MatchEvent::RoundOver,
                                     MatchEvent::PlayAgain, MatchEvent::ScriptError, MatchEvent::ForcedExit};

struct Transition {
  MatchState from;
  MatchEvent event;
  MatchState to;
};

// AD-21's solo diagram, plus Back closing the pause menu and the forced exit.
const std::vector<Transition>& transitions() {
  static const std::vector<Transition> table = {
      {MatchState::Starting, MatchEvent::Started, MatchState::Playing},
      {MatchState::Starting, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Starting, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Playing, MatchEvent::Back, MatchState::Paused},
      {MatchState::Playing, MatchEvent::Home, MatchState::Paused},
      {MatchState::Playing, MatchEvent::RoundOver, MatchState::Over},
      {MatchState::Playing, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Playing, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Paused, MatchEvent::Resume, MatchState::Playing},
      {MatchState::Paused, MatchEvent::Back, MatchState::Playing},
      {MatchState::Paused, MatchEvent::Leave, MatchState::Leaving},
      {MatchState::Paused, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Paused, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Over, MatchEvent::PlayAgain, MatchState::Playing},
      {MatchState::Over, MatchEvent::Leave, MatchState::Leaving},
      {MatchState::Over, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Over, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Error, MatchEvent::Back, MatchState::Leaving},
      {MatchState::Error, MatchEvent::ForcedExit, MatchState::Leaving},
  };
  return table;
}

// Drives a fresh lifecycle into `state` through listed transitions only.
MatchLifecycle reach(const MatchState state) {
  MatchLifecycle lifecycle;
  switch (state) {
    case MatchState::Starting:
      break;
    case MatchState::Playing:
      lifecycle.apply(MatchEvent::Started);
      break;
    case MatchState::Paused:
      lifecycle.apply(MatchEvent::Started);
      lifecycle.apply(MatchEvent::Back);
      break;
    case MatchState::Over:
      lifecycle.apply(MatchEvent::Started);
      lifecycle.apply(MatchEvent::RoundOver);
      break;
    case MatchState::Error:
      lifecycle.apply(MatchEvent::ScriptError);
      break;
    case MatchState::Leaving:
      lifecycle.apply(MatchEvent::ForcedExit);
      break;
  }
  EXPECT_EQ(lifecycle.state(), state);
  return lifecycle;
}

}  // namespace

TEST(MatchLifecycleTest, StartsInStarting) { EXPECT_EQ(MatchLifecycle().state(), MatchState::Starting); }

TEST(MatchLifecycleTest, EveryStateAndEventFollowsTheTable) {
  for (const MatchState from : ALL_STATES) {
    for (const MatchEvent event : ALL_EVENTS) {
      MatchState expected = from;
      for (const Transition& t : transitions()) {
        if (t.from == from && t.event == event) expected = t.to;
      }
      const char* label = MatchLifecycle::name(event);
      EXPECT_EQ(MatchLifecycle::next(from, event), expected) << MatchLifecycle::name(from) << " + " << label;

      MatchLifecycle lifecycle = reach(from);
      EXPECT_EQ(lifecycle.allows(event), expected != from) << MatchLifecycle::name(from) << " + " << label;
      EXPECT_EQ(lifecycle.apply(event), expected != from) << MatchLifecycle::name(from) << " + " << label;
      EXPECT_EQ(lifecycle.state(), expected) << MatchLifecycle::name(from) << " + " << label;
    }
  }
}

TEST(MatchLifecycleTest, BackAndHomeNeverLeaveDirectly) {
  for (const MatchState from : ALL_STATES) {
    if (from == MatchState::Error || from == MatchState::Leaving) continue;
    EXPECT_NE(MatchLifecycle::next(from, MatchEvent::Back), MatchState::Leaving) << MatchLifecycle::name(from);
    EXPECT_NE(MatchLifecycle::next(from, MatchEvent::Home), MatchState::Leaving) << MatchLifecycle::name(from);
  }
}

TEST(MatchLifecycleTest, ASecondScriptErrorKeepsTheFirstErrorView) {
  MatchLifecycle lifecycle = reach(MatchState::Error);
  EXPECT_FALSE(lifecycle.apply(MatchEvent::ScriptError));
  EXPECT_EQ(lifecycle.state(), MatchState::Error);
}

TEST(MatchLifecycleTest, RoundsRepeatThroughPlayAgain) {
  MatchLifecycle lifecycle = reach(MatchState::Playing);
  for (int round = 0; round < 3; ++round) {
    ASSERT_TRUE(lifecycle.apply(MatchEvent::RoundOver));
    ASSERT_TRUE(lifecycle.apply(MatchEvent::PlayAgain));
  }
  EXPECT_EQ(lifecycle.state(), MatchState::Playing);
}

TEST(MatchLifecycleTest, LeavingIsTerminal) {
  MatchLifecycle lifecycle = reach(MatchState::Leaving);
  for (const MatchEvent event : ALL_EVENTS) {
    EXPECT_FALSE(lifecycle.apply(event)) << MatchLifecycle::name(event);
  }
  EXPECT_EQ(lifecycle.state(), MatchState::Leaving);
}

TEST(MatchLifecycleTest, MenusOfferTheirTransitions) {
  const auto expectMenu = [](const MatchState state, const std::vector<MatchEvent>& expected) {
    const MatchMenu menu = MatchLifecycle::menuFor(state);
    ASSERT_EQ(menu.count, expected.size()) << MatchLifecycle::name(state);
    for (uint8_t i = 0; i < menu.count; ++i) {
      EXPECT_EQ(menu.events[i], expected[i]) << MatchLifecycle::name(state) << " option " << int{i};
      EXPECT_TRUE(reach(state).allows(menu.events[i])) << MatchLifecycle::name(state) << " option " << int{i};
    }
  };
  expectMenu(MatchState::Paused, {MatchEvent::Resume, MatchEvent::Leave});
  expectMenu(MatchState::Over, {MatchEvent::PlayAgain, MatchEvent::Leave});
  expectMenu(MatchState::Error, {MatchEvent::Back});
  expectMenu(MatchState::Starting, {});
  expectMenu(MatchState::Playing, {});
  expectMenu(MatchState::Leaving, {});
}

TEST(MatchLifecycleTest, NamesEveryStateAndEvent) {
  for (const MatchState state : ALL_STATES) EXPECT_STRNE(MatchLifecycle::name(state), "?");
  for (const MatchEvent event : ALL_EVENTS) EXPECT_STRNE(MatchLifecycle::name(event), "?");
}
