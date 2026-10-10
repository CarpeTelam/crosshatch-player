#include <gtest/gtest.h>

#include <cstdint>

#include "GameTouchRoute.h"

using GameTouchRoute::Action;
using GameTouchRoute::Aimed;
using GameTouchRoute::Decision;

namespace {

// A Source that counts each read, so the tests pin the read points as well as the answer: a value read lazily by the
// activity stays read only on the path that reads it.
struct FakeSource {
  bool pushing = false;
  uint32_t now = 1000;
  uint32_t held = 0;
  uint32_t onPanel = 9;
  uint32_t current = 5;
  // frameAt(atMs) answers `before` when atMs < pushDoneMs (a push that completed after the touch-down), else onPanel.
  uint32_t before = 8;
  uint32_t pushDoneMs = 0;

  int pushingReads = 0;
  int nowReads = 0;
  int heldReads = 0;
  int onPanelReads = 0;
  int frameAtReads = 0;
  int currentReads = 0;
  uint32_t lastAtMs = 0;

  bool firstFramePushing() {
    ++pushingReads;
    return pushing;
  }
  uint32_t nowMs() {
    ++nowReads;
    return now;
  }
  uint32_t heldMs() {
    ++heldReads;
    return held;
  }
  uint32_t frameOnPanel() {
    ++onPanelReads;
    return onPanel;
  }
  uint32_t frameAt(const uint32_t atMs) {
    ++frameAtReads;
    lastAtMs = atMs;
    return static_cast<int32_t>(atMs - pushDoneMs) < 0 ? before : onPanel;
  }
  uint32_t currentFrame() {
    ++currentReads;
    return current;
  }
  int reads() const { return pushingReads + nowReads + heldReads + onPanelReads + frameAtReads + currentReads; }
};

Aimed aimed(const bool tap = false) {
  return Aimed{.tap = tap,
               .awaitingRound = false,
               .awaitingDisplay = false,
               .latched = false,
               .latchedFrame = 0,
               .latchedMs = 0,
               .playingSinceMs = 0};
}

TEST(GameTouchRouteTest, ALatchedContactIsPostedWithItsLatchedFrame) {
  FakeSource source;
  Aimed in = aimed();
  in.latched = true;
  in.latchedFrame = 7;
  const Decision d = GameTouchRoute::decide(in, source);
  EXPECT_EQ(d.action, Action::Post);
  EXPECT_EQ(d.shownFrame, 7u);
  EXPECT_EQ(source.onPanelReads, 0) << "a latched contact never asks the panel";
}

TEST(GameTouchRouteTest, AnUnlatchedContactIsPostedWithTheFrameOnThePanel) {
  FakeSource source;
  const Decision d = GameTouchRoute::decide(aimed(), source);
  EXPECT_EQ(d.action, Action::Post);
  EXPECT_EQ(d.shownFrame, 9u);
}

TEST(GameTouchRouteTest, ASwipeNeverAsksFrameAtOrTheClock) {
  FakeSource source;
  GameTouchRoute::decide(aimed(/*tap=*/false), source);
  EXPECT_EQ(source.frameAtReads, 0);
  EXPECT_EQ(source.heldReads, 0);
  EXPECT_EQ(source.nowReads, 0);
}

TEST(GameTouchRouteTest, ATapIsBackDatedToTheFrameBeforeAPushThatCompletedAfterItsTouchDown) {
  FakeSource source;
  source.now = 2000;
  source.held = 100;  // touch-down at 1900
  source.pushDoneMs = 1950;
  source.before = 8;
  const Decision d = GameTouchRoute::decide(aimed(/*tap=*/true), source);
  EXPECT_EQ(d.action, Action::Post);
  EXPECT_EQ(d.shownFrame, 8u);
  EXPECT_EQ(source.lastAtMs, 1900u);
}

TEST(GameTouchRouteTest, ATapAfterTheLastPushKeepsTheFrameOnThePanel) {
  FakeSource source;
  source.now = 2000;
  source.held = 10;
  source.pushDoneMs = 1950;
  const Decision d = GameTouchRoute::decide(aimed(/*tap=*/true), source);
  EXPECT_EQ(d.shownFrame, 9u);
  EXPECT_EQ(source.frameAtReads, 1);
}

TEST(GameTouchRouteTest, ATapNeverRaisesTheFrameOfAnOlderLatch) {
  FakeSource source;  // frameAt answers the panel's 9; the latch holds 7
  Aimed in = aimed(/*tap=*/true);
  in.latched = true;
  in.latchedFrame = 7;
  const Decision d = GameTouchRoute::decide(in, source);
  EXPECT_EQ(d.shownFrame, 7u) << "the older of the latch and frameAt is posted";
}

TEST(GameTouchRouteTest, TheTaggedFrameIsNeverUntagged) {
  FakeSource source;
  Aimed in = aimed();
  in.latched = true;
  in.latchedFrame = GameTouchRoute::UNTAGGED;
  EXPECT_EQ(GameTouchRoute::decide(in, source).shownFrame, GameTouchRoute::UNTAGGED - 1);
  FakeSource panel;
  panel.onPanel = GameTouchRoute::UNTAGGED;
  EXPECT_EQ(GameTouchRoute::decide(aimed(), panel).shownFrame, GameTouchRoute::UNTAGGED - 1);
}

TEST(GameTouchRouteTest, ATouchBeforeTheFrameIsOnThePanelIsDroppedAndReadsNothingMore) {
  FakeSource source;
  Aimed in = aimed(/*tap=*/true);
  in.awaitingDisplay = true;
  const Decision d = GameTouchRoute::decide(in, source);
  EXPECT_EQ(d.action, Action::DropBeforeDisplay);
  EXPECT_EQ(source.nowReads + source.heldReads + source.onPanelReads + source.frameAtReads + source.currentReads, 0);
}

TEST(GameTouchRouteTest, AwaitingARoundNeverReadsFirstFramePushing) {
  FakeSource source;
  source.pushing = true;
  Aimed in = aimed();
  in.awaitingRound = true;
  in.awaitingDisplay = true;
  const Decision d = GameTouchRoute::decide(in, source);
  EXPECT_EQ(d.action, Action::DropBeforeDisplay);
  EXPECT_EQ(source.reads(), 0);
}

TEST(GameTouchRouteTest, TheFirstFramePathIgnoresTheDisplayCountAndPostsTheVmsCurrentFrame) {
  FakeSource source;
  source.pushing = true;
  source.current = 4;
  Aimed in = aimed();
  in.awaitingDisplay = true;  // the frame is published but not yet on the panel
  in.playingSinceMs = 900;
  const Decision d = GameTouchRoute::decide(in, source);
  EXPECT_EQ(d.action, Action::Post);
  EXPECT_EQ(d.shownFrame, 4u);
  EXPECT_EQ(source.frameAtReads, 0);
  EXPECT_EQ(source.onPanelReads, 0);
}

TEST(GameTouchRouteTest, TheFirstFramePathNeverPostsUntagged) {
  FakeSource source;
  source.pushing = true;
  source.current = GameTouchRoute::UNTAGGED;
  EXPECT_EQ(GameTouchRoute::decide(aimed(), source).shownFrame, GameTouchRoute::UNTAGGED - 1);
}

TEST(GameTouchRouteTest, TheFirstFramePathTakesWhenTheContactBeganFromTheLatchOrNow) {
  FakeSource latchedSource;
  latchedSource.pushing = true;
  latchedSource.now = 2000;
  Aimed in = aimed();
  in.latched = true;
  in.latchedMs = 850;  // before the hand-off passed at 900
  in.playingSinceMs = 900;
  Decision d = GameTouchRoute::decide(in, latchedSource);
  EXPECT_EQ(d.action, Action::DropBeforeHandOff) << "the latch dates the contact, not now";
  EXPECT_EQ(d.beforeHandOffMs, 50);

  FakeSource nowSource;
  nowSource.pushing = true;
  nowSource.now = 2000;
  in.latched = false;
  d = GameTouchRoute::decide(in, nowSource);
  EXPECT_EQ(d.action, Action::Post) << "a contact the loop never saw down began now";
}

TEST(GameTouchRouteTest, ABeganAtTheHandOffInstantIsAccepted) {
  FakeSource source;
  source.pushing = true;
  Aimed in = aimed();
  in.latched = true;
  in.latchedMs = 900;
  in.playingSinceMs = 900;
  EXPECT_EQ(GameTouchRoute::decide(in, source).action, Action::Post);
  in.latchedMs = 899;
  FakeSource later;
  later.pushing = true;
  const Decision d = GameTouchRoute::decide(in, later);
  EXPECT_EQ(d.action, Action::DropBeforeHandOff);
  EXPECT_EQ(d.beforeHandOffMs, 1);
}

TEST(GameTouchRouteTest, AFirstFrameTapIsBackDatedByItsHeldTimeAndOnlyATap) {
  FakeSource tapSource;
  tapSource.pushing = true;
  tapSource.now = 1000;
  tapSource.held = 300;  // touch-down at 700
  Aimed in = aimed(/*tap=*/true);
  in.playingSinceMs = 900;
  Decision d = GameTouchRoute::decide(in, tapSource);
  EXPECT_EQ(d.action, Action::DropBeforeHandOff) << "the press that passed the hand-off, lifting late";
  EXPECT_EQ(d.beforeHandOffMs, 200);

  FakeSource swipeSource;
  swipeSource.pushing = true;
  swipeSource.now = 1000;
  swipeSource.held = 300;
  in.tap = false;
  d = GameTouchRoute::decide(in, swipeSource);
  EXPECT_EQ(d.action, Action::Post) << "a swipe is dated by its latch or now, never by lastTouchHeldMs";
  EXPECT_EQ(swipeSource.heldReads, 0);
}

TEST(GameTouchRouteTest, ABackDatedTapNeverMovesALatchedBeganLater) {
  FakeSource source;
  source.pushing = true;
  source.now = 1000;
  source.held = 10;  // back-dated 990, later than the latch at 950
  Aimed in = aimed(/*tap=*/true);
  in.latched = true;
  in.latchedMs = 950;
  in.playingSinceMs = 960;  // the latch preceded the hand-off; the later back-dated time must not rescue it
  Decision late = GameTouchRoute::decide(in, source);
  EXPECT_EQ(late.action, Action::DropBeforeHandOff);
  EXPECT_EQ(late.beforeHandOffMs, 10);
  in.playingSinceMs = 940;
  FakeSource early;
  early.pushing = true;
  early.now = 1000;
  early.held = 100;  // back-dated 900, earlier than the latch: the older wins
  const Decision d = GameTouchRoute::decide(in, early);
  EXPECT_EQ(d.action, Action::DropBeforeHandOff);
  EXPECT_EQ(d.beforeHandOffMs, 40);
}

TEST(GameTouchRouteTest, TheHandOffDropKeepsTheWrapOfTheSignedCompare) {
  FakeSource source;
  source.pushing = true;
  Aimed in = aimed();
  in.latched = true;
  in.playingSinceMs = 5;          // the clock wrapped since: began is just before the wrap
  in.latchedMs = UINT32_MAX - 4;  // 10 ms before playingSinceMs
  const Decision d = GameTouchRoute::decide(in, source);
  EXPECT_EQ(d.action, Action::DropBeforeHandOff);
  EXPECT_EQ(d.beforeHandOffMs, 10);
}

TEST(GameTouchRouteTest, FirstFramePushingIsReadOnce) {
  FakeSource source;
  GameTouchRoute::decide(aimed(), source);
  EXPECT_EQ(source.pushingReads, 1);
}

}  // namespace
