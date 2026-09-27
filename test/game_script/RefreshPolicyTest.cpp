#include <gtest/gtest.h>

#include <cstdint>

#include "RefreshPolicy.h"

using GameScript::FAST_REFRESH_LIMIT;
using GameScript::Refresh;
using GameScript::RefreshPolicy;

namespace {

// Each call shows a frame with a new hash, so none is skipped as identical.
class RefreshPolicyTest : public ::testing::Test {
 protected:
  Refresh show(const Refresh hint) {
    Refresh mode = Refresh::Fast;
    EXPECT_TRUE(policy.decide(++hash, hint, mode));
    return mode;
  }

  RefreshPolicy policy;
  uint64_t hash = 0;
};

TEST_F(RefreshPolicyTest, TheFirstFrameIsFullWhateverItAsks) {
  EXPECT_TRUE(policy.fullForced());
  EXPECT_EQ(show(Refresh::Fast), Refresh::Full);
  EXPECT_FALSE(policy.fullForced());
}

TEST_F(RefreshPolicyTest, LaterFramesGetTheirHint) {
  show(Refresh::Fast);
  EXPECT_EQ(show(Refresh::Fast), Refresh::Fast);
  EXPECT_EQ(show(Refresh::Half), Refresh::Half);
  EXPECT_EQ(show(Refresh::Full), Refresh::Full);
  EXPECT_EQ(show(Refresh::Fast), Refresh::Fast);
}

TEST_F(RefreshPolicyTest, TooManyFastRefreshesInARowEscalateToHalf) {
  show(Refresh::Fast);  // full
  for (int i = 0; i < FAST_REFRESH_LIMIT; ++i) ASSERT_EQ(show(Refresh::Fast), Refresh::Fast) << i;
  EXPECT_EQ(show(Refresh::Fast), Refresh::Half);
  // The count starts over after the escalation.
  for (int i = 0; i < FAST_REFRESH_LIMIT; ++i) ASSERT_EQ(show(Refresh::Fast), Refresh::Fast) << i;
  EXPECT_EQ(show(Refresh::Fast), Refresh::Half);
}

TEST_F(RefreshPolicyTest, AHalfOrFullRefreshRestartsTheCount) {
  show(Refresh::Fast);
  for (int i = 0; i < FAST_REFRESH_LIMIT - 1; ++i) show(Refresh::Fast);
  EXPECT_EQ(show(Refresh::Half), Refresh::Half);
  for (int i = 0; i < FAST_REFRESH_LIMIT; ++i) ASSERT_EQ(show(Refresh::Fast), Refresh::Fast) << i;
  EXPECT_EQ(show(Refresh::Full), Refresh::Full);
  for (int i = 0; i < FAST_REFRESH_LIMIT; ++i) ASSERT_EQ(show(Refresh::Fast), Refresh::Fast) << i;
}

TEST_F(RefreshPolicyTest, ForceFullBeatsTheHintOnceAndRestartsTheCount) {
  show(Refresh::Fast);
  for (int i = 0; i < FAST_REFRESH_LIMIT - 1; ++i) show(Refresh::Fast);
  policy.forceFull();
  EXPECT_EQ(show(Refresh::Fast), Refresh::Full);
  EXPECT_EQ(show(Refresh::Fast), Refresh::Fast);
  for (int i = 0; i < FAST_REFRESH_LIMIT - 1; ++i) ASSERT_EQ(show(Refresh::Fast), Refresh::Fast) << i;
}

TEST_F(RefreshPolicyTest, AFrameIdenticalToTheOneOnScreenIsSkipped) {
  Refresh mode = Refresh::Fast;
  ASSERT_TRUE(policy.decide(42, Refresh::Fast, mode));
  // Same commands again, whatever it asks for: nothing drawn, mode untouched.
  mode = Refresh::Half;
  EXPECT_FALSE(policy.decide(42, Refresh::Full, mode));
  EXPECT_EQ(mode, Refresh::Half);
  // A different frame is shown, and then it is the one on screen.
  ASSERT_TRUE(policy.decide(43, Refresh::Fast, mode));
  EXPECT_EQ(mode, Refresh::Fast);
  EXPECT_FALSE(policy.decide(43, Refresh::Fast, mode));
  // Going back to the earlier frame is a change.
  EXPECT_TRUE(policy.decide(42, Refresh::Fast, mode));
}

TEST_F(RefreshPolicyTest, ForceFullRedrawsAnIdenticalFrame) {
  Refresh mode = Refresh::Fast;
  ASSERT_TRUE(policy.decide(7, Refresh::Fast, mode));
  policy.forceFull();
  ASSERT_TRUE(policy.decide(7, Refresh::Fast, mode));
  EXPECT_EQ(mode, Refresh::Full);
  EXPECT_FALSE(policy.decide(7, Refresh::Fast, mode));
}

TEST_F(RefreshPolicyTest, SkippedFramesDoNotCountAsFastRefreshes) {
  Refresh mode = Refresh::Fast;
  ASSERT_TRUE(policy.decide(1, Refresh::Fast, mode));  // full
  for (int i = 0; i < 3 * FAST_REFRESH_LIMIT; ++i) ASSERT_FALSE(policy.decide(1, Refresh::Fast, mode));
  for (uint64_t h = 2; h < 2 + FAST_REFRESH_LIMIT; ++h) {
    ASSERT_TRUE(policy.decide(h, Refresh::Fast, mode));
    EXPECT_EQ(mode, Refresh::Fast) << h;
  }
}

}  // namespace
