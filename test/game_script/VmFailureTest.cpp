#include <gtest/gtest.h>

#include "LuaGame.h"
#include "VmFailure.h"

// The error view's wording of a failed GameVM (AD-14): GameVM::failure()'s order,
// the match's detail text (vmFailureText), and its headline (vmHealthy).

using GameScript::failedToStart;
using GameScript::failureDetail;
using GameScript::HostFailureTexts;
using GameScript::LuaGame;
using GameScript::VmFailure;
using GameScript::vmFailure;

namespace {

using HostFailure = LuaGame::HostFailure;

constexpr const char* OOM_TEXT = "tr out of memory";
constexpr const char* NOT_LOADED_TEXT = "tr not loaded";
constexpr const char* SCRIPT_MESSAGE = "main.lua:3: boom";

HostFailureTexts texts() {
  HostFailureTexts t;
  t.outOfMemory = OOM_TEXT;
  t.notLoaded = NOT_LOADED_TEXT;
  return t;
}

TEST(VmFailureTest, NoFailureUntilTheVmFailed) {
  for (const HostFailure host : {HostFailure::None, HostFailure::OutOfMemory, HostFailure::NotLoaded}) {
    EXPECT_EQ(vmFailure(false, false, host), VmFailure::None);
    EXPECT_EQ(vmFailure(false, true, host), VmFailure::None);
  }
}

TEST(VmFailureTest, ASessionThatNeverFitComesFirst) {
  for (const HostFailure host : {HostFailure::None, HostFailure::OutOfMemory, HostFailure::NotLoaded}) {
    EXPECT_EQ(vmFailure(true, true, host), VmFailure::NoSession);
  }
}

TEST(VmFailureTest, TheHostFailureThenTheScript) {
  EXPECT_EQ(vmFailure(true, false, HostFailure::OutOfMemory), VmFailure::OutOfMemory);
  EXPECT_EQ(vmFailure(true, false, HostFailure::NotLoaded), VmFailure::NotLoaded);
  EXPECT_EQ(vmFailure(true, false, HostFailure::None), VmFailure::Script);
}

TEST(VmFailureTest, DetailIsTheHostTextForHostFailures) {
  EXPECT_STREQ(failureDetail(VmFailure::NoSession, texts(), SCRIPT_MESSAGE), OOM_TEXT);
  EXPECT_STREQ(failureDetail(VmFailure::OutOfMemory, texts(), SCRIPT_MESSAGE), OOM_TEXT);
  EXPECT_STREQ(failureDetail(VmFailure::NotLoaded, texts(), SCRIPT_MESSAGE), NOT_LOADED_TEXT);
}

TEST(VmFailureTest, DetailIsTheScriptMessageOtherwise) {
  EXPECT_STREQ(failureDetail(VmFailure::Script, texts(), SCRIPT_MESSAGE), SCRIPT_MESSAGE);
  EXPECT_STREQ(failureDetail(VmFailure::None, texts(), SCRIPT_MESSAGE), SCRIPT_MESSAGE);
}

// AD-14, as amended by the owner 2026-09-28: every host failure comes before any
// game code runs, so the headline says the game could not start; only the script's
// own error says it stopped. Every VmFailure value is covered by one of the two.
TEST(VmFailureTest, EveryHostFailureFailedToStart) {
  for (const VmFailure failure : {VmFailure::NoSession, VmFailure::OutOfMemory, VmFailure::NotLoaded}) {
    EXPECT_TRUE(failedToStart(failure)) << static_cast<int>(failure);
  }
}

TEST(VmFailureTest, AScriptFailureOrNoFailureDidNotFailToStart) {
  for (const VmFailure failure : {VmFailure::Script, VmFailure::None}) {
    EXPECT_FALSE(failedToStart(failure)) << static_cast<int>(failure);
  }
}

}  // namespace
