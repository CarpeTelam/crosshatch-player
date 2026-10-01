#include <gtest/gtest.h>

#include <iterator>
#include <vector>

#include "MatchLifecycle.h"

using namespace GameCore;

namespace {

constexpr MatchState ALL_STATES[] = {MatchState::Starting, MatchState::Playing, MatchState::Paused,
                                     MatchState::Over,     MatchState::Error,   MatchState::Leaving,
                                     MatchState::Result,   MatchState::HandOff};
constexpr MatchEvent ALL_EVENTS[] = {MatchEvent::Started,     MatchEvent::Back,        MatchEvent::Home,
                                     MatchEvent::Resume,      MatchEvent::Leave,       MatchEvent::RoundOver,
                                     MatchEvent::PlayAgain,   MatchEvent::ScriptError, MatchEvent::ForcedExit,
                                     MatchEvent::TurnChanged, MatchEvent::Tap};
// The states the solo machine has; Result and HandOff are the hidden pass machine's only.
constexpr MatchState SOLO_STATES[] = {MatchState::Starting, MatchState::Playing, MatchState::Paused,
                                      MatchState::Over,     MatchState::Error,   MatchState::Leaving};

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
      {MatchState::Result, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::HandOff, MatchEvent::ForcedExit, MatchState::Leaving},
  };
  return table;
}

// AD-21's hidden pass changes and additions: the solo table with these rows replacing or
// joining its own, plus every state's forced exit and Result's and HandOff's ScriptError.
const std::vector<Transition>& hiddenTransitions() {
  static const std::vector<Transition> table = {
      {MatchState::Starting, MatchEvent::Started, MatchState::HandOff},
      {MatchState::Starting, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Starting, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Playing, MatchEvent::Back, MatchState::Paused},
      {MatchState::Playing, MatchEvent::Home, MatchState::Paused},
      {MatchState::Playing, MatchEvent::RoundOver, MatchState::Over},
      {MatchState::Playing, MatchEvent::TurnChanged, MatchState::Result},
      {MatchState::Playing, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Playing, MatchEvent::ForcedExit, MatchState::Leaving},
      // Paused as entered from Playing; PausedReturnsToTheStateItWasEnteredFrom covers the others.
      {MatchState::Paused, MatchEvent::Resume, MatchState::Playing},
      {MatchState::Paused, MatchEvent::Back, MatchState::Playing},
      {MatchState::Paused, MatchEvent::Leave, MatchState::Leaving},
      {MatchState::Paused, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Paused, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Over, MatchEvent::PlayAgain, MatchState::HandOff},
      {MatchState::Over, MatchEvent::Leave, MatchState::Leaving},
      {MatchState::Over, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Over, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Error, MatchEvent::Back, MatchState::Leaving},
      {MatchState::Error, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::Result, MatchEvent::Back, MatchState::Paused},
      {MatchState::Result, MatchEvent::Home, MatchState::Paused},
      {MatchState::Result, MatchEvent::Tap, MatchState::HandOff},
      {MatchState::Result, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::Result, MatchEvent::ForcedExit, MatchState::Leaving},
      {MatchState::HandOff, MatchEvent::Back, MatchState::Paused},
      {MatchState::HandOff, MatchEvent::Home, MatchState::Paused},
      {MatchState::HandOff, MatchEvent::Tap, MatchState::Playing},
      {MatchState::HandOff, MatchEvent::ScriptError, MatchState::Error},
      {MatchState::HandOff, MatchEvent::ForcedExit, MatchState::Leaving},
  };
  return table;
}

MatchState expectedIn(const std::vector<Transition>& table, const MatchState from, const MatchEvent event) {
  for (const Transition& t : table) {
    if (t.from == from && t.event == event) return t.to;
  }
  return from;
}

// Drives a fresh hidden pass lifecycle into `state` through listed transitions only; Paused
// is entered from Playing.
MatchLifecycle reachHidden(const MatchState state) {
  MatchLifecycle lifecycle(true);
  const auto toPlaying = [&lifecycle] {
    lifecycle.apply(MatchEvent::Started);
    lifecycle.apply(MatchEvent::Tap);
  };
  switch (state) {
    case MatchState::Starting:
      break;
    case MatchState::Playing:
      toPlaying();
      break;
    case MatchState::Paused:
      toPlaying();
      lifecycle.apply(MatchEvent::Back);
      break;
    case MatchState::Over:
      toPlaying();
      lifecycle.apply(MatchEvent::RoundOver);
      break;
    case MatchState::Error:
      lifecycle.apply(MatchEvent::ScriptError);
      break;
    case MatchState::Leaving:
      lifecycle.apply(MatchEvent::ForcedExit);
      break;
    case MatchState::Result:
      toPlaying();
      lifecycle.apply(MatchEvent::TurnChanged);
      break;
    case MatchState::HandOff:
      lifecycle.apply(MatchEvent::Started);
      break;
  }
  EXPECT_EQ(lifecycle.state(), state);
  return lifecycle;
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
    case MatchState::Result:
    case MatchState::HandOff:
      ADD_FAILURE() << "the solo machine never reaches " << MatchLifecycle::name(state);
      return lifecycle;
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
      EXPECT_EQ(MatchLifecycle::next(from, event, false), expected) << MatchLifecycle::name(from) << " + " << label;

      // A built solo lifecycle reaches only the solo states (TheSoloMachineNeverReachesResultOrHandOff).
      if (from == MatchState::Result || from == MatchState::HandOff) continue;
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

// ---- the hidden pass machine ----

TEST(MatchLifecycleTest, TheFlagIsFixedWhenTheLifecycleIsBuilt) {
  EXPECT_FALSE(MatchLifecycle().hiddenPass());
  EXPECT_FALSE(MatchLifecycle(false).hiddenPass());
  EXPECT_TRUE(MatchLifecycle(true).hiddenPass());
  EXPECT_EQ(MatchLifecycle(true).state(), MatchState::Starting);
}

TEST(MatchLifecycleTest, HiddenPassEveryStateAndEventFollowsItsTable) {
  for (const MatchState from : ALL_STATES) {
    for (const MatchEvent event : ALL_EVENTS) {
      const MatchState expected = expectedIn(hiddenTransitions(), from, event);
      const char* label = MatchLifecycle::name(event);
      EXPECT_EQ(MatchLifecycle::next(from, event, true), expected) << MatchLifecycle::name(from) << " + " << label;

      MatchLifecycle lifecycle = reachHidden(from);
      EXPECT_EQ(lifecycle.allows(event), expected != from) << MatchLifecycle::name(from) << " + " << label;
      EXPECT_EQ(lifecycle.apply(event), expected != from) << MatchLifecycle::name(from) << " + " << label;
      EXPECT_EQ(lifecycle.state(), expected) << MatchLifecycle::name(from) << " + " << label;
    }
  }
}

TEST(MatchLifecycleTest, PausedReturnsToTheStateItWasEnteredFrom) {
  for (const MatchState from : {MatchState::Playing, MatchState::Result, MatchState::HandOff}) {
    for (const MatchEvent pause : {MatchEvent::Back, MatchEvent::Home}) {
      for (const MatchEvent resume : {MatchEvent::Resume, MatchEvent::Back}) {
        MatchLifecycle lifecycle = reachHidden(from);
        EXPECT_EQ(lifecycle.resumesTo(), MatchState::Playing) << "outside Paused";
        ASSERT_TRUE(lifecycle.apply(pause)) << MatchLifecycle::name(from);
        ASSERT_EQ(lifecycle.state(), MatchState::Paused);
        EXPECT_EQ(lifecycle.resumesTo(), from) << MatchLifecycle::name(from);
        EXPECT_TRUE(lifecycle.allows(resume));
        ASSERT_TRUE(lifecycle.apply(resume)) << MatchLifecycle::name(from) << " + " << MatchLifecycle::name(resume);
        EXPECT_EQ(lifecycle.state(), from) << MatchLifecycle::name(pause) << " then " << MatchLifecycle::name(resume);
      }
    }
  }
  // The static form: Paused returns to Result or HandOff when told to, Playing otherwise.
  EXPECT_EQ(MatchLifecycle::next(MatchState::Paused, MatchEvent::Resume, true, MatchState::Result), MatchState::Result);
  EXPECT_EQ(MatchLifecycle::next(MatchState::Paused, MatchEvent::Back, true, MatchState::HandOff), MatchState::HandOff);
  EXPECT_EQ(MatchLifecycle::next(MatchState::Paused, MatchEvent::Resume, true, MatchState::Over), MatchState::Playing);
  EXPECT_EQ(MatchLifecycle::next(MatchState::Paused, MatchEvent::Resume, true), MatchState::Playing);
}

// The static solo machine ignores where Paused says it returns: it always resumes play.
TEST(MatchLifecycleTest, TheSoloMachineResumesPlayWhateverPausedWasEnteredFrom) {
  for (const MatchState resumesTo : {MatchState::Result, MatchState::HandOff, MatchState::Playing}) {
    for (const MatchEvent resume : {MatchEvent::Resume, MatchEvent::Back}) {
      EXPECT_EQ(MatchLifecycle::next(MatchState::Paused, resume, false, resumesTo), MatchState::Playing)
          << MatchLifecycle::name(resume) << " toward " << MatchLifecycle::name(resumesTo);
    }
  }
  const MatchLifecycle paused = reach(MatchState::Paused);
  EXPECT_EQ(paused.resumesTo(), MatchState::Playing);
}

// Every event from a hidden Paused entered from each of Playing, Result, and HandOff: only
// Resume and Back, which return where it came from, differ from Paused's own table rows.
TEST(MatchLifecycleTest, HiddenPausedFromEachStateTakesEveryEventAlike) {
  for (const MatchState from : {MatchState::Playing, MatchState::Result, MatchState::HandOff}) {
    for (const MatchEvent event : ALL_EVENTS) {
      const bool resumes = event == MatchEvent::Resume || event == MatchEvent::Back;
      const MatchState expected = resumes ? from : expectedIn(hiddenTransitions(), MatchState::Paused, event);
      const char* label = MatchLifecycle::name(event);
      EXPECT_EQ(MatchLifecycle::next(MatchState::Paused, event, true, from), expected)
          << "paused from " << MatchLifecycle::name(from) << " + " << label;

      MatchLifecycle lifecycle = reachHidden(from);
      ASSERT_TRUE(lifecycle.apply(MatchEvent::Back)) << MatchLifecycle::name(from);
      ASSERT_EQ(lifecycle.state(), MatchState::Paused);
      EXPECT_EQ(lifecycle.allows(event), expected != MatchState::Paused)
          << "paused from " << MatchLifecycle::name(from) << " + " << label;
      EXPECT_EQ(lifecycle.apply(event), expected != MatchState::Paused)
          << "paused from " << MatchLifecycle::name(from) << " + " << label;
      EXPECT_EQ(lifecycle.state(), expected) << "paused from " << MatchLifecycle::name(from) << " + " << label;
    }
  }
}

// A move that ends the round raises RoundOver from Playing; in Result or HandOff it is not a
// transition (AD-21 has no Result to Over), in either machine.
TEST(MatchLifecycleTest, RoundOverIsNoTransitionInResultOrHandOff) {
  for (const MatchState state : {MatchState::Result, MatchState::HandOff}) {
    EXPECT_EQ(MatchLifecycle::next(state, MatchEvent::RoundOver, true), state) << MatchLifecycle::name(state);
    EXPECT_EQ(MatchLifecycle::next(state, MatchEvent::RoundOver), state) << MatchLifecycle::name(state);
    MatchLifecycle lifecycle = reachHidden(state);
    EXPECT_FALSE(lifecycle.allows(MatchEvent::RoundOver)) << MatchLifecycle::name(state);
    EXPECT_FALSE(lifecycle.apply(MatchEvent::RoundOver)) << MatchLifecycle::name(state);
    EXPECT_EQ(lifecycle.state(), state);
  }
  MatchLifecycle playing = reachHidden(MatchState::Playing);
  ASSERT_TRUE(playing.apply(MatchEvent::RoundOver));
  EXPECT_EQ(playing.state(), MatchState::Over);
}

// Each pause records where it came from, not only the first one in a match.
TEST(MatchLifecycleTest, EachPauseInAMatchReturnsToItsOwnState) {
  MatchLifecycle lifecycle = reachHidden(MatchState::Result);
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Back));
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Resume));
  ASSERT_EQ(lifecycle.state(), MatchState::Result);
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Tap));
  ASSERT_EQ(lifecycle.state(), MatchState::HandOff);
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Home));
  EXPECT_EQ(lifecycle.resumesTo(), MatchState::HandOff);
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Resume));
  EXPECT_EQ(lifecycle.state(), MatchState::HandOff) << "the second pause, from HandOff";
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Tap));
  ASSERT_TRUE(lifecycle.apply(MatchEvent::TurnChanged));
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Back));
  EXPECT_EQ(lifecycle.resumesTo(), MatchState::Result);
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Back));
  EXPECT_EQ(lifecycle.state(), MatchState::Result) << "the third pause, from Result";
}

TEST(MatchLifecycleTest, APauseFromResultLeavesAsAnyPauseDoes) {
  MatchLifecycle lifecycle = reachHidden(MatchState::Result);
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Home));
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Leave));
  EXPECT_EQ(lifecycle.state(), MatchState::Leaving);
}

TEST(MatchLifecycleTest, HiddenPassTurnsCycleThroughResultAndHandOff) {
  MatchLifecycle lifecycle(true);
  ASSERT_TRUE(lifecycle.apply(MatchEvent::Started));
  EXPECT_EQ(lifecycle.state(), MatchState::HandOff) << "the first mover is handed the device";
  for (int turn = 0; turn < 3; ++turn) {
    ASSERT_TRUE(lifecycle.apply(MatchEvent::Tap));
    ASSERT_EQ(lifecycle.state(), MatchState::Playing);
    ASSERT_TRUE(lifecycle.apply(MatchEvent::TurnChanged));
    ASSERT_EQ(lifecycle.state(), MatchState::Result);
    ASSERT_TRUE(lifecycle.apply(MatchEvent::Tap));
    ASSERT_EQ(lifecycle.state(), MatchState::HandOff);
  }
}

TEST(MatchLifecycleTest, HiddenPassPlayAgainHandsTheDeviceOver) {
  MatchLifecycle lifecycle = reachHidden(MatchState::Over);
  for (int round = 0; round < 3; ++round) {
    ASSERT_TRUE(lifecycle.apply(MatchEvent::PlayAgain));
    ASSERT_EQ(lifecycle.state(), MatchState::HandOff);
    ASSERT_TRUE(lifecycle.apply(MatchEvent::Tap));
    ASSERT_TRUE(lifecycle.apply(MatchEvent::RoundOver));
  }
  EXPECT_EQ(lifecycle.state(), MatchState::Over);
}

// Breadth-first over every event from Starting: the solo machine (solo and open pass) has no
// way into Result or HandOff, and the new events are never its transitions.
TEST(MatchLifecycleTest, TheSoloMachineNeverReachesResultOrHandOff) {
  std::vector<MatchState> seen = {MatchState::Starting};
  for (size_t i = 0; i < seen.size(); ++i) {
    for (const MatchEvent event : ALL_EVENTS) {
      const MatchState to = MatchLifecycle::next(seen[i], event);
      bool known = false;
      for (const MatchState s : seen) known = known || s == to;
      if (!known) seen.push_back(to);
    }
    EXPECT_EQ(MatchLifecycle::next(seen[i], MatchEvent::TurnChanged), seen[i]) << MatchLifecycle::name(seen[i]);
    EXPECT_EQ(MatchLifecycle::next(seen[i], MatchEvent::Tap), seen[i]) << MatchLifecycle::name(seen[i]);
  }
  EXPECT_EQ(seen.size(), std::size(SOLO_STATES));
  for (const MatchState s : seen) {
    EXPECT_NE(s, MatchState::Result);
    EXPECT_NE(s, MatchState::HandOff);
  }
  // The same through a built lifecycle, whose Paused remembers where it came from.
  MatchLifecycle lifecycle;
  for (const MatchEvent event : {MatchEvent::Started, MatchEvent::TurnChanged, MatchEvent::Tap, MatchEvent::Back,
                                 MatchEvent::Resume, MatchEvent::RoundOver, MatchEvent::PlayAgain}) {
    lifecycle.apply(event);
    EXPECT_FALSE(lifecycle.state() == MatchState::Result || lifecycle.state() == MatchState::HandOff)
        << MatchLifecycle::name(event);
  }
  EXPECT_EQ(lifecycle.state(), MatchState::Playing);
}

TEST(MatchLifecycleTest, ResultAndHandOffHaveNoMenuAndTheirBackOpensThePauseMenu) {
  for (const MatchState state : {MatchState::Result, MatchState::HandOff}) {
    EXPECT_EQ(MatchLifecycle::menuFor(state).count, 0) << MatchLifecycle::name(state);
    EXPECT_EQ(MatchLifecycle::menuFor(state).events, nullptr) << MatchLifecycle::name(state);
    MatchLifecycle lifecycle = reachHidden(state);
    ASSERT_TRUE(lifecycle.apply(MatchEvent::Back));
    const MatchMenu pause = MatchLifecycle::menuFor(lifecycle.state());
    ASSERT_EQ(pause.count, 2) << MatchLifecycle::name(state);
    EXPECT_EQ(pause.events[0], MatchEvent::Resume);
    EXPECT_EQ(pause.events[1], MatchEvent::Leave);
  }
}

TEST(MatchLifecycleTest, NamesTheNewStatesAndEvents) {
  EXPECT_STREQ(MatchLifecycle::name(MatchState::Result), "Result");
  EXPECT_STREQ(MatchLifecycle::name(MatchState::HandOff), "HandOff");
  EXPECT_STREQ(MatchLifecycle::name(MatchEvent::TurnChanged), "TurnChanged");
  EXPECT_STREQ(MatchLifecycle::name(MatchEvent::Tap), "Tap");
}
