#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>

#include "Logging.h"
#include "MatchPersistence.h"
#include "MatchStore.h"
#include "SnapshotMailbox.h"

// MatchPersistence on its own: the forced exit's deadline over a fake clock, and that nothing is written before the
// match is bound to a store, or while the store is not ready (never allocated) or resume.bin is not writable.
// GameMatchTest and ResumeMatchTest play the same code through the real activity.

namespace {

uint32_t fakeNow = 0;
uint32_t clockNow() { return fakeNow; }

class MatchPersistenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakeNow = 0;
    fakelog::clearLines();
  }
  static size_t skipLines() { return fakelog::countLines("skipped"); }
};

TEST_F(MatchPersistenceTest, DeadlineConstantIsTheDocumentedOne) {
  EXPECT_EQ(MatchPersistence::FORCED_EXIT_DEADLINE_MS, 1500u);
}

TEST_F(MatchPersistenceTest, StepsAreAllowedOutsideAForcedExit) {
  MatchStore store;
  MatchPersistence persistence;
  persistence.bind(store, "game", &clockNow);
  fakeNow = 0xFFFFFFFFu;  // however late
  EXPECT_FALSE(persistence.forcedExit());
  EXPECT_TRUE(persistence.sdStepAllowed("the resume write"));
  EXPECT_EQ(skipLines(), 0u);
}

TEST_F(MatchPersistenceTest, TheDeadlineIsCountedFromTheClockReadAtBeginForcedExit) {
  MatchStore store;
  MatchPersistence persistence;
  persistence.bind(store, "game", &clockNow);
  fakeNow = 1000;
  persistence.beginForcedExit();
  EXPECT_TRUE(persistence.forcedExit());

  fakeNow = 1000;
  EXPECT_TRUE(persistence.sdStepAllowed("a"));
  fakeNow = 1000 + 1499;
  EXPECT_TRUE(persistence.sdStepAllowed("b")) << "1,499 ms in is still in time";
  EXPECT_EQ(skipLines(), 0u);
  fakeNow = 1000 + 1500;
  EXPECT_FALSE(persistence.sdStepAllowed("the ch.store flush")) << "1,500 ms in is past the deadline";
  EXPECT_EQ(skipLines(), 1u);
  EXPECT_TRUE(fakelog::anyLine("skipped the ch.store flush"));
  EXPECT_FALSE(persistence.sdStepAllowed("the resume write"));
  EXPECT_EQ(skipLines(), 2u) << "one line per refusal";
}

TEST_F(MatchPersistenceTest, TheDeadlineSurvivesTheClockWrapping) {
  MatchStore store;
  MatchPersistence persistence;
  persistence.bind(store, "game", &clockNow);
  fakeNow = 0xFFFFFF00u;
  persistence.beginForcedExit();
  fakeNow = 0xFFFFFF00u + 1499u;  // wraps past zero
  EXPECT_TRUE(persistence.sdStepAllowed("a"));
  fakeNow = 0xFFFFFF00u + 1500u;
  EXPECT_FALSE(persistence.sdStepAllowed("b"));
}

TEST_F(MatchPersistenceTest, WritableIsWhatTheMatchSets) {
  MatchPersistence persistence;
  EXPECT_FALSE(persistence.writable());
  persistence.setWritable(true);
  EXPECT_TRUE(persistence.writable());
  persistence.setWritable(false);
  EXPECT_FALSE(persistence.writable());
}

TEST_F(MatchPersistenceTest, NothingIsDoneBeforeBind) {
  std::array<uint8_t, 16> bytes{};
  SnapshotMailbox mailbox{std::span<uint8_t>(bytes)};
  const std::array<uint8_t, 2> snapshot{1, 2};
  ASSERT_TRUE(mailbox.publish(snapshot, 1, false));

  MatchPersistence persistence;
  persistence.setWritable(true);
  persistence.flushResumeOf(mailbox);
  persistence.retryResumeDelete(true);
  persistence.onOver();
  persistence.flushStore();
  EXPECT_TRUE(mailbox.pending()) << "no store, so the snapshot was not taken";
  EXPECT_FALSE(persistence.deletePending());
}

TEST_F(MatchPersistenceTest, NothingIsDoneWhileTheStoreIsNotReady) {
  std::array<uint8_t, 16> bytes{};
  SnapshotMailbox mailbox{std::span<uint8_t>(bytes)};
  const std::array<uint8_t, 2> snapshot{1, 2};
  ASSERT_TRUE(mailbox.publish(snapshot, 1, false));

  MatchStore store;  // never allocated
  ASSERT_FALSE(store.ready());
  MatchPersistence persistence;
  persistence.bind(store, "game", &clockNow);
  persistence.setWritable(true);
  persistence.flushResumeOf(mailbox);
  persistence.retryResumeDelete(true);
  persistence.flushStore();
  EXPECT_TRUE(mailbox.pending()) << "the store never got that far, so the snapshot was not taken";
  EXPECT_FALSE(persistence.deletePending());
  EXPECT_EQ(skipLines(), 0u);
}

}  // namespace
