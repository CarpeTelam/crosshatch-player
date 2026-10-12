#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "GameLinkLog.h"
#include "Logging.h"

// The text of every LINK line (entry 8 of epic-play-nearby, R13): one test per line pins it exactly, so a changed word
// is a red test, and each line names the `session` id (a STATE line its `ver`).

namespace {

using GameCore::Adoption;
using GameCore::Protocol::AbortReason;

constexpr uint32_t SESSION = 0x00A1B2C3u;

class GameLinkLogTest : public ::testing::Test {
 protected:
  void SetUp() override { fakelog::clearLines(); }
  // The one line the call logged.
  std::string only() {
    const std::vector<std::string> lines = fakelog::snapshot();
    EXPECT_EQ(lines.size(), 1u);
    return lines.empty() ? std::string() : lines.front();
  }
};

TEST_F(GameLinkLogTest, StateSent) {
  GameLinkLog::stateSent(SESSION, 7);
  EXPECT_EQ(only(), "INF LINK: session=00a1b2c3 state sent ver=7");
}

TEST_F(GameLinkLogTest, StateReceived) {
  GameLinkLog::stateReceived(SESSION, 65535, 2);
  EXPECT_EQ(only(), "INF LINK: session=00a1b2c3 state recv ver=65535 seat=2");
}

TEST_F(GameLinkLogTest, StateAdopted) {
  GameLinkLog::stateAdopted(SESSION, 9, 1, Adoption::RoundStarted);
  EXPECT_EQ(only(), "INF LINK: session=00a1b2c3 state adopt ver=9 seat=1 result=RoundStarted");
}

TEST_F(GameLinkLogTest, EveryAdoptionVerdictHasItsName) {
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::Adopted), "Adopted");
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::RoundStarted), "RoundStarted");
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::NotPeer), "NotPeer");
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::Busy), "Busy");
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::BadSnapshot), "BadSnapshot");
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::Stale), "Stale");
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::OutOfTurn), "OutOfTurn");
  EXPECT_STREQ(GameLinkLog::adoptionName(Adoption::BadStatus), "BadStatus");
}

TEST_F(GameLinkLogTest, QueueFull) {
  GameLinkLog::queueFull(SESSION, 5, 3);
  EXPECT_EQ(only(), "INF LINK: session=00a1b2c3 queue full ver=5 overflows=3");
}

TEST_F(GameLinkLogTest, AbortReceived) {
  GameLinkLog::abortReceived(SESSION, AbortReason::VersionMismatch, 2);
  EXPECT_EQ(only(), "INF LINK: session=00a1b2c3 abort recv reason=version_mismatch seat=2");
}

TEST_F(GameLinkLogTest, EveryAbortReasonHasItsName) {
  EXPECT_STREQ(GameLinkLog::abortReasonName(AbortReason::Left), "left");
  EXPECT_STREQ(GameLinkLog::abortReasonName(AbortReason::ScriptError), "script_error");
  EXPECT_STREQ(GameLinkLog::abortReasonName(AbortReason::Timeout), "timeout");
  EXPECT_STREQ(GameLinkLog::abortReasonName(AbortReason::Full), "full");
  EXPECT_STREQ(GameLinkLog::abortReasonName(AbortReason::VersionMismatch), "version_mismatch");
  EXPECT_STREQ(GameLinkLog::abortReasonName(static_cast<AbortReason>(0)), "unknown");
}

TEST_F(GameLinkLogTest, LeaveSent) {
  GameLinkLog::leaveSent(SESSION, AbortReason::Left, 120, 2, 3);
  EXPECT_EQ(only(), "INF LINK: session=00a1b2c3 leave reason=left waited=120ms unacked=2 copies=3");
}

TEST_F(GameLinkLogTest, TheStatsLines) {
  GameLinkLog::resent(SESSION, 2, 9);
  GameLinkLog::silent(SESSION, 1, 1);
  GameLinkLog::abortSent(SESSION, 3, 3);
  GameLinkLog::linkRefused(SESSION, 4, 11);
  const std::vector<std::string> lines = fakelog::snapshot();
  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0], "INF LINK: session=00a1b2c3 resend n=2 total=9");
  EXPECT_EQ(lines[1], "INF LINK: session=00a1b2c3 silence n=1 total=1");
  EXPECT_EQ(lines[2], "INF LINK: session=00a1b2c3 abort sent n=3 total=3");
  EXPECT_EQ(lines[3], "INF LINK: session=00a1b2c3 link refused n=4 total=11");
}

TEST_F(GameLinkLogTest, TaskStartedAndStopped) {
  GameLinkLog::taskStarted(0, 4096);
  GameLinkLog::taskStopped(SESSION, 1536);
  const std::vector<std::string> lines = fakelog::snapshot();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "INF LINK: session=00000000 task started; stack 4096 bytes");
  EXPECT_EQ(lines[1], "INF LINK: session=00a1b2c3 task stopped; stack high-water 1536 bytes free");
}

TEST_F(GameLinkLogTest, TheErrorLinesNameTheSessionToo) {
  GameLinkLog::shipRefused(SESSION, 1401);
  GameLinkLog::bringUpFailed(0, "no PSRAM for the pool");
  GameLinkLog::taskLeaked(SESSION);
  const std::vector<std::string> lines = fakelog::snapshot();
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "ERR LINK: session=00a1b2c3 ship refused: snapshot of 1401 bytes");
  EXPECT_EQ(lines[1], "ERR LINK: session=00000000 bring-up failed: no PSRAM for the pool");
  EXPECT_EQ(lines[2], "ERR LINK: session=00a1b2c3 task did not stop; its state is leaked");
}

}  // namespace
