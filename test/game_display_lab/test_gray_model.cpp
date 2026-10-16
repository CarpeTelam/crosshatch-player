// The gray engine's per-pixel model and slot (lib/GameDisplayLab/GrayModel, GraySlot.h): the plan's Dose lookup,
// Retarget, Mask build and Commit matrix rows, InkBoy's level arithmetic (`level += (target*64 - level) /
// remaining`), its W>L test (192, 160, 128; a retarget at 160 keeping the level), history routes and the weak row.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "GrayModel.h"
#include "GrayPreset.h"
#include "GraySlot.h"
#include "MaskPlanes.h"

namespace {

using namespace GameDisplayLab;

constexpr uint16_t W = 16;
constexpr uint16_t H = 4;

// A model and two mask pairs over owned buffers.
struct Bench {
  std::vector<uint8_t> pixels = std::vector<uint8_t>(GrayModel::bytesFor(W, H));
  std::vector<uint8_t> history;
  std::vector<uint8_t> weak;
  std::vector<uint8_t> planes = std::vector<uint8_t>(4 * MaskPlanes::bytesFor(W, H));
  GrayModel model;
  MaskPlanes drive;
  MaskPlanes follow;

  explicit Bench(const bool withHistory = false, const bool withWeak = false) {
    if (withHistory) history.resize(GrayModel::historyBytesFor(W, H));
    if (withWeak) weak.resize(GrayModel::weakBytesFor(W, H));
    model = GrayModel(pixels.data(), W, H, withHistory ? history.data() : nullptr, withWeak ? weak.data() : nullptr);
    const size_t n = MaskPlanes::bytesFor(W, H);
    drive = MaskPlanes(planes.data(), planes.data() + n, W, H);
    follow = MaskPlanes(planes.data() + 2 * n, planes.data() + 3 * n, W, H);
  }
};

uint8_t shadeOf(const Shade s) { return static_cast<uint8_t>(s); }

TEST(GrayModel, SizesFollowThePlan) {
  EXPECT_EQ(GrayModel::bytesFor(800, 480), 768000u);
  EXPECT_EQ(GrayModel::historyBytesFor(800, 480), 1152000u);
  EXPECT_EQ(GrayModel::weakBytesFor(800, 480), 48000u);
}

TEST(GrayModel, BaseDoseOnAShadeIsTheTablesDose) {
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  for (uint8_t from = 0; from < SHADE_COUNT; ++from) {
    for (uint8_t to = 0; to < SHADE_COUNT; ++to) {
      EXPECT_EQ(GrayModel::baseDose(static_cast<uint8_t>(from * 64), to, p), p.dose[from][to])
          << int(from) << " > " << int(to);
    }
  }
}

TEST(GrayModel, BaseDoseBetweenShadesInterpolatesRoundedUp) {
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  const uint8_t K = shadeOf(Shade::K);
  const uint8_t L = shadeOf(Shade::L);
  const uint8_t Wt = shadeOf(Shade::W);
  EXPECT_EQ(GrayModel::baseDose(0, Wt, p), 7);    // K > W
  EXPECT_EQ(GrayModel::baseDose(32, Wt, p), 7);   // (7*32 + 6*32) / 64 = 6.5, up
  EXPECT_EQ(GrayModel::baseDose(63, Wt, p), 7);   // (7*1 + 6*63) / 64 = 6.02, up
  EXPECT_EQ(GrayModel::baseDose(64, Wt, p), 6);   // D > W
  EXPECT_EQ(GrayModel::baseDose(160, K, p), 6);   // (4*32 + 7*32) / 64 = 5.5, up
  EXPECT_EQ(GrayModel::baseDose(160, L, p), 1);   // (0*32 + 2*32) / 64 = 1
  EXPECT_EQ(GrayModel::baseDose(192, L, p), 2);   // W > L
  EXPECT_EQ(GrayModel::baseDose(192, Wt, p), 0);  // 0 only on the target
  EXPECT_EQ(GrayModel::baseDose(128, L, p), 0);
  EXPECT_EQ(GrayModel::baseDose(129, L, p), 1);
}

TEST(GrayModel, ResetSettlesEveryPixel) {
  Bench b(true, true);
  b.model.reset(Shade::D);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      EXPECT_EQ(b.model.level(x, y), 64);
      EXPECT_EQ(b.model.remaining(x, y), 0);
      EXPECT_EQ(b.model.target(x, y), shadeOf(Shade::D));
      EXPECT_EQ(b.model.source(x, y), shadeOf(Shade::D));
      EXPECT_EQ(b.model.origin(x, y), GrayModel::ORIGIN_NONE);
      EXPECT_FALSE(b.model.weakEligible(x, y));
    }
  }
  EXPECT_EQ(b.model.activeCount(), 0u);
}

TEST(GrayModel, WhiteToLightFollowsInkBoysLevels) {
  // tools/test_fast_display_grayscale.py: a W>L pixel (2 doses) at 192, 160, 128.
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  b.model.retarget(3, 1, Shade::L, p);
  EXPECT_EQ(b.model.level(3, 1), 192);
  EXPECT_EQ(b.model.remaining(3, 1), 2);
  EXPECT_EQ(b.model.target(3, 1), shadeOf(Shade::L));
  EXPECT_EQ(b.model.source(3, 1), shadeOf(Shade::W));
  b.model.commit(true, true, p);  // W>L holds: the follow-up scan does not drive it
  EXPECT_EQ(b.model.level(3, 1), 160);
  EXPECT_EQ(b.model.remaining(3, 1), 1);
  b.model.commit(true, true, p);
  EXPECT_EQ(b.model.level(3, 1), 128);
  EXPECT_EQ(b.model.remaining(3, 1), 0);
  b.model.commit(true, true, p);  // settled: nothing more
  EXPECT_EQ(b.model.level(3, 1), 128);
  EXPECT_EQ(b.model.activeCount(), 0u);
}

TEST(GrayModel, RetargetMidTransitionKeepsTheLevelAndReplacesRemaining) {
  Bench b;
  GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  b.model.retarget(0, 0, Shade::L, p);
  b.model.commit(true, false, p);
  ASSERT_EQ(b.model.level(0, 0), 160);
  ASSERT_EQ(b.model.remaining(0, 0), 1);
  b.model.retarget(0, 0, Shade::K, p);
  EXPECT_EQ(b.model.level(0, 0), 160);
  EXPECT_EQ(b.model.remaining(0, 0), 6);  // base 6 at 100 %
  EXPECT_EQ(b.model.target(0, 0), shadeOf(Shade::K));
  EXPECT_EQ(b.model.source(0, 0), shadeOf(Shade::W));  // nearest shade to 160: (160 + 32) / 64 = 3

  // Scaled by the retarget percent, rounded up, clamped 1..12.
  const auto budgetAt = [&b](GrayPreset preset, const uint8_t percent, const Shade to) {
    preset.retargetPercent = percent;
    b.model.reset(Shade::W);
    b.model.retarget(0, 0, Shade::L, preset);
    b.model.commit(true, false, preset);  // 160, one dose left
    b.model.retarget(0, 0, to, preset);
    return b.model.remaining(0, 0);
  };
  EXPECT_EQ(budgetAt(p, 150, Shade::K), 9);
  EXPECT_EQ(budgetAt(p, 50, Shade::K), 3);
  EXPECT_EQ(budgetAt(p, 75, Shade::K), 5);  // 4.5, up
  p.dose[3][0] = 12;
  p.dose[2][0] = 12;
  EXPECT_EQ(budgetAt(p, 150, Shade::K), 12);  // 18 clamps to 12
  p = GrayPreset::inkBoyDefaults();
  p.dose[3][2] = 1;  // W>L 1, so the base from 150 to L is 1
  b.model.reset(Shade::W);
  b.model.retarget(0, 0, Shade::D, p);  // W>D 3
  b.model.commit(true, false, p);       // 192 + (64 - 192) / 3 = 150
  ASSERT_EQ(b.model.level(0, 0), 150);
  p.retargetPercent = 50;
  b.model.retarget(0, 0, Shade::L, p);  // base ceil((0*42 + 1*22) / 64) = 1; 50 % of 1 is 0.5, up, at least 1
  EXPECT_EQ(b.model.remaining(0, 0), 1);
  EXPECT_EQ(b.model.source(0, 0), shadeOf(Shade::L));  // (150 + 32) / 64 = 2
}

TEST(GrayModel, RetargetToTheSameTargetIsANoOp) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  b.model.retarget(1, 1, Shade::K, p);
  b.model.commit(true, false, p);
  const uint8_t level = b.model.level(1, 1);
  const uint8_t remaining = b.model.remaining(1, 1);
  b.model.retarget(1, 1, Shade::K, p);
  EXPECT_EQ(b.model.level(1, 1), level);
  EXPECT_EQ(b.model.remaining(1, 1), remaining);
  b.model.retarget(1, 1, Shade::W, p);  // back to white from mid-way: scaled at 100 %
  EXPECT_EQ(b.model.target(1, 1), shadeOf(Shade::W));
  b.model.retarget(-1, 0, Shade::K, p);  // outside: ignored
  b.model.retarget(W, 0, Shade::K, p);
}

TEST(GrayModel, RetargetRectIsClipped) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  b.model.retargetRect(-2, 2, 4, 10, Shade::K, p);
  EXPECT_EQ(b.model.activeCount(), 4u);  // x 0..1, y 2..3
  EXPECT_EQ(b.model.target(0, 2), shadeOf(Shade::K));
  EXPECT_EQ(b.model.target(1, 3), shadeOf(Shade::K));
  EXPECT_EQ(b.model.target(2, 2), shadeOf(Shade::W));
  EXPECT_EQ(b.model.target(0, 1), shadeOf(Shade::W));
  b.model.retargetRect(0, 0, 0, 5, Shade::K, p);
  EXPECT_EQ(b.model.activeCount(), 4u);
}

// Every pixel of `b`: drive white when it lightens, black when it darkens, nothing when settled; the follow-up mask
// the same for a pixel whose hold bit is off with more than one dose left.
void expectMasksFollowTheRules(const Bench& b, const GrayPreset& p) {
  using M = MaskPlanes::Mask;
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const bool active = b.model.remaining(x, y) > 0;
      const bool lighten = b.model.target(x, y) * 64 > b.model.level(x, y);
      EXPECT_EQ(b.drive.test(M::White, x, y), active && lighten) << x << "," << y;
      EXPECT_EQ(b.drive.test(M::Black, x, y), active && !lighten) << x << "," << y;
      const bool hold = (p.holdMask & holdBit(b.model.source(x, y), b.model.target(x, y))) != 0;
      const bool again = active && b.model.remaining(x, y) > 1 && !hold;
      EXPECT_EQ(b.follow.test(M::White, x, y), again && lighten) << x << "," << y;
      EXPECT_EQ(b.follow.test(M::Black, x, y), again && !lighten) << x << "," << y;
    }
  }
}

TEST(GrayModel, MaskBuildDarkensToBlackWithFollowUpOnlyWhenHoldIsOff) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  b.model.retarget(5, 1, Shade::K, p);  // W>K 7, hold off
  b.model.retarget(6, 1, Shade::L, p);  // W>L 2, hold on
  b.model.retarget(7, 1, Shade::D, p);  // W>D 3, hold off
  EXPECT_EQ(b.model.buildMasks(b.drive, b.follow, p, false), 3u);
  using M = MaskPlanes::Mask;
  EXPECT_TRUE(b.drive.test(M::Black, 5, 1));
  EXPECT_TRUE(b.follow.test(M::Black, 5, 1));
  EXPECT_TRUE(b.drive.test(M::Black, 6, 1));
  EXPECT_FALSE(b.follow.test(M::Black, 6, 1));
  EXPECT_TRUE(b.drive.test(M::Black, 7, 1));
  EXPECT_TRUE(b.follow.test(M::Black, 7, 1));
  EXPECT_FALSE(b.drive.test(M::Black, 8, 1));  // settled: holds
  expectMasksFollowTheRules(b, p);
}

TEST(GrayModel, MaskBuildLightensToWhite) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::K);
  b.model.retarget(0, 0, Shade::W, p);  // K>W 7, hold off
  b.model.retarget(1, 0, Shade::D, p);  // K>D 2, hold on
  b.model.retarget(2, 3, Shade::L, p);  // K>L 4, hold on
  EXPECT_EQ(b.model.buildMasks(b.drive, b.follow, p, false), 3u);
  using M = MaskPlanes::Mask;
  EXPECT_TRUE(b.drive.test(M::White, 0, 0));
  EXPECT_TRUE(b.follow.test(M::White, 0, 0));
  EXPECT_TRUE(b.drive.test(M::White, 1, 0));
  EXPECT_FALSE(b.follow.test(M::White, 1, 0));
  EXPECT_TRUE(b.drive.test(M::White, 2, 3));
  EXPECT_FALSE(b.drive.test(M::Black, 2, 3));
  expectMasksFollowTheRules(b, p);
  // Mid-transition pixels after a slot and a retarget back keep to the rules too.
  b.model.commit(true, true, p);
  b.model.retarget(0, 0, Shade::K, p);  // from 54 back to black, source D
  b.model.buildMasks(b.drive, b.follow, p, false);
  EXPECT_TRUE(b.drive.test(M::Black, 0, 0));
  expectMasksFollowTheRules(b, p);
}

TEST(GrayModel, OneDoseLeftWithHoldOffGetsNoFollowUp) {
  Bench b;
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.dose[0][3] = 1;  // K>W one dose, hold off
  b.model.reset(Shade::K);
  b.model.retarget(2, 2, Shade::W, p);
  ASSERT_EQ(b.model.remaining(2, 2), 1);
  EXPECT_EQ(b.model.buildMasks(b.drive, b.follow, p, false), 1u);
  EXPECT_TRUE(b.drive.test(MaskPlanes::Mask::White, 2, 2));
  EXPECT_FALSE(b.follow.test(MaskPlanes::Mask::White, 2, 2));
}

TEST(GrayModel, MismatchedMasksBuildNothing) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  b.model.retarget(0, 0, Shade::K, p);
  std::vector<uint8_t> small(4 * MaskPlanes::bytesFor(8, H));
  MaskPlanes narrow(small.data(), small.data() + MaskPlanes::bytesFor(8, H), 8, H);
  EXPECT_EQ(b.model.buildMasks(narrow, b.follow, p, false), 0u);
  EXPECT_EQ(b.model.buildMasks(b.drive, narrow, p, false), 0u);
}

TEST(GrayModel, WeakPixelIsSetInBothMasksOnlyWithTheWeakRow) {
  Bench b(false, true);
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  b.model.retarget(4, 0, Shade::L, p);  // settled white to L: eligible
  b.model.retarget(5, 0, Shade::K, p);  // W>K: not eligible
  EXPECT_TRUE(b.model.weakEligible(4, 0));
  EXPECT_FALSE(b.model.weakEligible(5, 0));
  using M = MaskPlanes::Mask;
  b.model.buildMasks(b.drive, b.follow, p, true);
  EXPECT_TRUE(b.drive.test(M::Black, 4, 0));
  EXPECT_TRUE(b.drive.test(M::White, 4, 0));  // the third (weak) row
  EXPECT_TRUE(b.drive.test(M::Black, 5, 0));
  EXPECT_FALSE(b.drive.test(M::White, 5, 0));
  b.model.buildMasks(b.drive, b.follow, p, false);
  EXPECT_TRUE(b.drive.test(M::Black, 4, 0));
  EXPECT_FALSE(b.drive.test(M::White, 4, 0));  // no weak row: plain black

  // Any other retarget clears it; a W>L pixel not settled on white is not eligible.
  b.model.retarget(4, 0, Shade::K, p);
  EXPECT_FALSE(b.model.weakEligible(4, 0));
  b.model.retarget(5, 0, Shade::L, p);  // previous target K: not eligible
  EXPECT_FALSE(b.model.weakEligible(5, 0));
  b.model.reset(Shade::W);
  b.model.retarget(6, 0, Shade::K, p);
  b.model.commit(true, false, p);       // 192 > 165
  b.model.retarget(6, 0, Shade::W, p);  // back toward white, doses pending
  b.model.retarget(6, 0, Shade::L, p);  // previous target W, but not settled on it
  EXPECT_FALSE(b.model.weakEligible(6, 0));
}

TEST(GrayModel, WeakPixelFollowsUpInBothMasksWhenItsHoldIsOff) {
  Bench b(false, true);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.setHold(transitionIndex(shadeOf(Shade::W), shadeOf(Shade::L)), false);  // W>L 2 doses, no hold: a follow-up
  b.model.reset(Shade::W);
  b.model.retarget(4, 0, Shade::L, p);
  ASSERT_TRUE(b.model.weakEligible(4, 0));
  ASSERT_EQ(b.model.remaining(4, 0), 2);
  EXPECT_EQ(b.model.buildMasks(b.drive, b.follow, p, true), 1u);
  using M = MaskPlanes::Mask;
  EXPECT_TRUE(b.drive.test(M::Black, 4, 0));
  EXPECT_TRUE(b.drive.test(M::White, 4, 0));
  EXPECT_TRUE(b.follow.test(M::Black, 4, 0));
  EXPECT_TRUE(b.follow.test(M::White, 4, 0));  // the follow-up drives the weak row too
}

// The fake scan stands in for DisplayLabActivity::scan: true is Done (delivered); any other outcome (TimedOut,
// Dropped, Refused) is false (not delivered). It records each scan's masks and reports a fixed busy time.
struct FakeScan {
  std::vector<bool> outcomes;  // per call, in order; missing calls are delivered
  std::vector<std::vector<uint8_t>> sentWhite;
  std::vector<std::vector<uint8_t>> sentBlack;
  size_t calls = 0;

  bool operator()(const MaskPlanes& masks, uint32_t& busyUs) {
    sentWhite.emplace_back(masks.white(), masks.white() + masks.bytes());
    sentBlack.emplace_back(masks.black(), masks.black() + masks.bytes());
    busyUs = 20000;
    const bool done = calls < outcomes.size() ? outcomes[calls] : true;
    ++calls;
    return done;
  }
};

bool bitSet(const std::vector<uint8_t>& plane, const int x, const int y) {
  const size_t i = static_cast<size_t>(y) * MaskPlanes::rowBytesFor(W) + static_cast<size_t>(x) / 8;
  return (plane[i] & (0x80u >> (x % 8))) != 0;
}

TEST(GraySlot, BothScansDeliveredCommitTwoDosesToFollowUpPixels) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::K);
  b.model.retarget(0, 0, Shade::W, p);  // K>W 7, hold off
  b.model.retarget(1, 0, Shade::D, p);  // K>D 2, hold on
  FakeScan scan;
  const SlotResult r = runGraySlot(b.model, b.drive, b.follow, p, false, scan);
  EXPECT_EQ(r.driven, 2u);
  EXPECT_EQ(r.scans, 2);
  EXPECT_EQ(r.busyUs, 40000u);
  EXPECT_TRUE(r.driveDone);
  EXPECT_TRUE(r.followDone);
  ASSERT_EQ(scan.calls, 2u);
  EXPECT_TRUE(bitSet(scan.sentWhite[0], 0, 0));
  EXPECT_TRUE(bitSet(scan.sentWhite[0], 1, 0));
  EXPECT_TRUE(bitSet(scan.sentWhite[1], 0, 0));   // follow-up drives K>W again
  EXPECT_FALSE(bitSet(scan.sentWhite[1], 1, 0));  // K>D holds
  // K>W: 0 + 192/7 = 27, then 27 + 165/6 = 54; K>D: 0 + 64/2 = 32.
  EXPECT_EQ(b.model.level(0, 0), 54);
  EXPECT_EQ(b.model.remaining(0, 0), 5);
  EXPECT_EQ(b.model.level(1, 0), 32);
  EXPECT_EQ(b.model.remaining(1, 0), 1);
}

TEST(GraySlot, FailedFollowUpCommitsOneDose) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::K);
  b.model.retarget(0, 0, Shade::W, p);
  FakeScan scan;
  scan.outcomes = {true, false};
  const SlotResult r = runGraySlot(b.model, b.drive, b.follow, p, false, scan);
  EXPECT_EQ(r.scans, 2);
  EXPECT_TRUE(r.driveDone);
  EXPECT_FALSE(r.followDone);
  EXPECT_EQ(b.model.level(0, 0), 27);
  EXPECT_EQ(b.model.remaining(0, 0), 6);
}

TEST(GraySlot, FailedDriveCommitsNothingAndSendsNoFollowUp) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::K);
  b.model.retarget(0, 0, Shade::W, p);
  FakeScan scan;
  scan.outcomes = {false};
  const SlotResult r = runGraySlot(b.model, b.drive, b.follow, p, false, scan);
  EXPECT_EQ(r.scans, 1);
  EXPECT_EQ(r.busyUs, 20000u);
  EXPECT_FALSE(r.driveDone);
  EXPECT_FALSE(r.followDone);
  EXPECT_EQ(scan.calls, 1u);
  EXPECT_EQ(b.model.level(0, 0), 0);
  EXPECT_EQ(b.model.remaining(0, 0), 7);
}

TEST(GraySlot, NothingToDriveSendsNoScan) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::W);
  FakeScan scan;
  const SlotResult r = runGraySlot(b.model, b.drive, b.follow, p, false, scan);
  EXPECT_EQ(r.driven, 0u);
  EXPECT_EQ(r.scans, 0);
  EXPECT_EQ(scan.calls, 0u);
}

TEST(GraySlot, SlotsRunAPixelToItsTargetExactly) {
  Bench b;
  const GrayPreset p = GrayPreset::inkBoyDefaults();
  b.model.reset(Shade::K);
  b.model.retarget(0, 0, Shade::W, p);
  FakeScan scan;
  int slots = 0;
  while (b.model.activeCount() > 0 && slots < 10) {
    runGraySlot(b.model, b.drive, b.follow, p, false, scan);
    ++slots;
  }
  EXPECT_EQ(slots, 4);  // 7 doses, two per slot
  EXPECT_EQ(b.model.level(0, 0), 192);
}

TEST(GrayModelHistory, CompletedReturnAddsTheRoutesDelta) {
  Bench b(true);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.historyMode = HISTORY_RETURN | HISTORY_REVERSE;
  b.model.reset(Shade::L);
  b.model.retarget(0, 0, Shade::K, p);  // L>K 4; origin L recorded (new target K, settled)
  EXPECT_EQ(b.model.origin(0, 0), shadeOf(Shade::L));
  b.model.commit(true, true, p);
  b.model.commit(true, true, p);  // L>K hold off: two doses a slot, 4 doses
  ASSERT_EQ(b.model.level(0, 0), 0);
  ASSERT_EQ(b.model.remaining(0, 0), 0);
  EXPECT_EQ(b.model.blackSteps(0, 0), 4);
  EXPECT_EQ(b.model.whiteSteps(0, 0), 0);
  b.model.retarget(0, 0, Shade::L, p);  // L>K>L: base K>L 4, return delta -1
  EXPECT_EQ(b.model.remaining(0, 0), 3);
  EXPECT_EQ(b.model.origin(0, 0), GrayModel::ORIGIN_NONE);  // new target L, mid routes neutral
  EXPECT_EQ(b.model.blackSteps(0, 0), 0);

  // Without the return bit, or with history off, the base stands.
  for (const uint8_t mode : {uint8_t{HISTORY_REVERSE}, uint8_t{0}}) {
    p.historyMode = mode;
    b.model.reset(Shade::L);
    b.model.retarget(0, 0, Shade::K, p);
    b.model.commit(true, true, p);
    b.model.commit(true, true, p);
    b.model.retarget(0, 0, Shade::L, p);
    EXPECT_EQ(b.model.remaining(0, 0), 4) << "mode " << int(mode);
  }
}

TEST(GrayModelHistory, ReturnNeedsTheTablesDoseOfSteps) {
  Bench b(true);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.historyMode = HISTORY_RETURN;
  b.model.reset(Shade::L);
  b.model.retarget(0, 0, Shade::K, p);
  b.model.commit(true, true, p);
  b.model.commit(true, true, p);
  p.dose[2][0] = 5;  // the table changed since: 4 steps no longer match L>K
  b.model.retarget(0, 0, Shade::L, p);
  EXPECT_EQ(b.model.remaining(0, 0), 4);
}

TEST(GrayModelHistory, ReversalScalesByTheRoutesPercent) {
  Bench b(true);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.historyMode = HISTORY_REVERSE;
  b.model.reset(Shade::L);
  b.model.retarget(0, 0, Shade::K, p);
  b.model.commit(true, false, p);  // 128 + (0 - 128) / 4 = 96, 3 left
  ASSERT_EQ(b.model.level(0, 0), 96);
  b.model.retarget(0, 0, Shade::L, p);  // base ceil((1*32 + 0*32) / 64) = 1; LKL reverse 150 %: 2
  EXPECT_EQ(b.model.remaining(0, 0), 2);

  p.historyMode = 0;  // retarget scaling only: 100 % of 1
  b.model.reset(Shade::L);
  b.model.retarget(0, 0, Shade::K, p);
  b.model.commit(true, false, p);
  b.model.retarget(0, 0, Shade::L, p);
  EXPECT_EQ(b.model.remaining(0, 0), 1);
}

TEST(GrayModelHistory, NeutralRouteKeepsTheRetargetBudget) {
  Bench b(true);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.historyMode = HISTORY_RETURN | HISTORY_REVERSE;
  b.model.reset(Shade::D);
  b.model.retarget(0, 0, Shade::K, p);  // D>K 4, origin D
  b.model.commit(true, true, p);
  b.model.commit(true, true, p);
  b.model.retarget(0, 0, Shade::D, p);  // D>K>D: delta 0, the base K>D 2
  EXPECT_EQ(b.model.remaining(0, 0), 2);
}

TEST(GrayModelHistory, OriginOnMidShadesOnlyWhenDldOrLdlIsActive) {
  Bench b(true);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.historyMode = HISTORY_RETURN;
  b.model.reset(Shade::D);
  b.model.retarget(0, 0, Shade::L, p);
  EXPECT_EQ(b.model.origin(0, 0), GrayModel::ORIGIN_NONE);
  p.returnDelta[static_cast<size_t>(Route::DLD)] = 1;
  b.model.reset(Shade::D);
  b.model.retarget(0, 0, Shade::L, p);  // D>L 1, origin D
  EXPECT_EQ(b.model.origin(0, 0), shadeOf(Shade::D));
  b.model.commit(true, true, p);
  ASSERT_EQ(b.model.level(0, 0), 128);
  EXPECT_EQ(b.model.whiteSteps(0, 0), 1);
  b.model.retarget(0, 0, Shade::D, p);  // D>L>D: L>D 1 + 1
  EXPECT_EQ(b.model.remaining(0, 0), 2);
  // An unsettled pixel records no origin.
  b.model.reset(Shade::W);
  b.model.retarget(1, 0, Shade::D, p);
  b.model.commit(true, false, p);
  b.model.retarget(1, 0, Shade::K, p);
  EXPECT_EQ(b.model.origin(1, 0), GrayModel::ORIGIN_NONE);
}

TEST(GrayModelHistory, StepCountersCountEachDeliveredDose) {
  Bench b(true);
  GrayPreset p = GrayPreset::inkBoyDefaults();
  p.historyMode = HISTORY_RETURN;
  b.model.reset(Shade::W);
  b.model.retarget(0, 0, Shade::K, p);
  b.model.commit(true, true, p);  // W>K hold off: two doses
  EXPECT_EQ(b.model.blackSteps(0, 0), 2);
  EXPECT_EQ(b.model.whiteSteps(0, 0), 0);
  b.model.commit(false, false, p);  // not delivered: not counted
  EXPECT_EQ(b.model.blackSteps(0, 0), 2);
  b.model.retarget(0, 0, Shade::W, p);  // a retarget restarts the counters
  b.model.commit(true, false, p);
  EXPECT_EQ(b.model.blackSteps(0, 0), 0);
  EXPECT_EQ(b.model.whiteSteps(0, 0), 1);
}

}  // namespace
