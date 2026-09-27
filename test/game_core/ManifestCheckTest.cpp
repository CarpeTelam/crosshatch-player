#include <gtest/gtest.h>

#include <cstring>

#include "Manifest.h"

using GameCore::CheckReason;
using GameCore::CheckResult;
using GameCore::CheckStatus;
using GameCore::HostCaps;
using GameCore::Manifest;

namespace {

// A host whose API range has room on both sides of a game's api.
constexpr HostCaps HOST{3, 2, 2, false};
constexpr HostCaps NEARBY_HOST{3, 2, 2, true};

Manifest game(const int32_t api, const int32_t seatsMin, const int32_t seatsMax, const uint8_t modes) {
  Manifest m;
  std::strcpy(m.id, "g");
  std::strcpy(m.name, "G");
  m.api = api;
  m.seatsMin = seatsMin;
  m.seatsMax = seatsMax;
  m.modes = modes;
  return m;
}

Manifest solo(const int32_t api = 2) { return game(api, 1, 1, Manifest::MODE_SOLO); }

void expectVerdict(const CheckResult& r, const CheckStatus status, const CheckReason reason) {
  EXPECT_EQ(r.status, status);
  EXPECT_EQ(r.reason, reason);
  EXPECT_EQ(r.modes, 0);
  EXPECT_FALSE(r.ok());
}

TEST(ManifestCheckTest, OkCarriesTheStartableModes) {
  const CheckResult r = solo().check(HOST);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.reason, CheckReason::None);
  EXPECT_EQ(r.modes, Manifest::MODE_SOLO);
}

TEST(ManifestCheckTest, ApiAtEachEndOfTheHostRangeIsOk) {
  EXPECT_TRUE(solo(2).check(HOST).ok());  // == minApi
  EXPECT_TRUE(solo(3).check(HOST).ok());  // == api
}

TEST(ManifestCheckTest, ApiOutsideTheHostRangeIsUnavailable) {
  expectVerdict(solo(1).check(HOST), CheckStatus::Unavailable, CheckReason::ApiTooOld);
  expectVerdict(solo(4).check(HOST), CheckStatus::Unavailable, CheckReason::ApiTooNew);
}

TEST(ManifestCheckTest, SeatsMinAtMaxSeatsIsOkAndOneOverIsUnavailable) {
  const CheckResult atMax = game(2, 2, 4, Manifest::MODE_PASS).check(HOST);
  EXPECT_TRUE(atMax.ok());
  EXPECT_EQ(atMax.modes, Manifest::MODE_PASS);
  expectVerdict(game(2, 3, 4, Manifest::MODE_PASS).check(HOST), CheckStatus::Unavailable, CheckReason::TooManySeats);
}

TEST(ManifestCheckTest, SeatsMaxAboveTheHostIsOk) {
  // The player picks n within seats and the host maximum.
  EXPECT_TRUE(game(2, 1, 8, Manifest::MODE_SOLO | Manifest::MODE_PASS).check(HOST).ok());
}

TEST(ManifestCheckTest, SoloNeedsSeatsMinOne) {
  EXPECT_TRUE(game(2, 1, 2, Manifest::MODE_SOLO).check(HOST).ok());
  expectVerdict(game(2, 2, 2, Manifest::MODE_SOLO | Manifest::MODE_PASS).check(HOST), CheckStatus::Invalid,
                CheckReason::SoloNeedsOneSeat);
}

TEST(ManifestCheckTest, NearbyNeedsSeatsMaxTwo) {
  EXPECT_TRUE(game(2, 1, 2, Manifest::MODE_NEARBY).check(NEARBY_HOST).ok());
  expectVerdict(game(2, 1, 1, Manifest::MODE_SOLO | Manifest::MODE_NEARBY).check(NEARBY_HOST), CheckStatus::Invalid,
                CheckReason::NearbyNeedsTwoSeats);
}

TEST(ManifestCheckTest, NearbyOnlyWithoutTheRadioIsUnavailable) {
  expectVerdict(game(2, 2, 2, Manifest::MODE_NEARBY).check(HOST), CheckStatus::Unavailable, CheckReason::NoHostMode);
}

TEST(ManifestCheckTest, NearbyNeedsTwoHostSeats) {
  constexpr HostCaps oneSeat{3, 2, 1, true};
  expectVerdict(game(2, 1, 2, Manifest::MODE_NEARBY).check(oneSeat), CheckStatus::Unavailable, CheckReason::NoHostMode);
}

TEST(ManifestCheckTest, OkDropsModesTheHostCannotStart) {
  const uint8_t all = Manifest::MODE_SOLO | Manifest::MODE_PASS | Manifest::MODE_NEARBY;
  const CheckResult withoutRadio = game(2, 1, 2, all).check(HOST);
  EXPECT_TRUE(withoutRadio.ok());
  EXPECT_EQ(withoutRadio.modes, Manifest::MODE_SOLO | Manifest::MODE_PASS);
  EXPECT_EQ(game(2, 1, 2, all).check(NEARBY_HOST).modes, all);
}

TEST(ManifestCheckTest, PassOnlyIsOkWithoutSolo) {
  const CheckResult r = game(2, 2, 2, Manifest::MODE_PASS).check(HOST);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.modes & Manifest::MODE_SOLO, 0);
}

TEST(ManifestCheckTest, BrokenFieldsAreInvalid) {
  expectVerdict(Manifest{}.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  expectVerdict(solo(0).check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  expectVerdict(game(2, 0, 1, Manifest::MODE_SOLO).check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  expectVerdict(game(2, 2, 1, Manifest::MODE_PASS).check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  expectVerdict(game(2, 1, 1, 0).check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  expectVerdict(game(2, 1, 1, Manifest::MODE_SOLO | 0x08).check(HOST), CheckStatus::Invalid, CheckReason::BadFields);

  Manifest badId = solo();
  std::strcpy(badId.id, "Bad");
  expectVerdict(badId.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest noName = solo();
  noName.name[0] = '\0';
  expectVerdict(noName.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest badIcon = solo();
  std::strcpy(badIcon.icon, "Mark-X");
  expectVerdict(badIcon.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest unterminated = solo();
  std::memset(unterminated.version, 'v', sizeof(unterminated.version));
  expectVerdict(unterminated.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
}

TEST(ManifestCheckTest, InvalidWinsOverUnavailable) {
  // api too new and a solo game needing two seats: the package is broken first.
  expectVerdict(game(9, 2, 2, Manifest::MODE_SOLO).check(HOST), CheckStatus::Invalid, CheckReason::SoloNeedsOneSeat);
}

TEST(ManifestCheckTest, ParsedManifestPassesCheck) {
  Manifest m;
  ASSERT_EQ(Manifest::parse(R"({"id": "tracer", "name": "Tracer", "version": "0.1", "api": 1,
                                "seats": {"min": 1, "max": 1}, "modes": ["solo"], "icon": "mark_x"})",
                            m),
            GameCore::ManifestError::None);
  EXPECT_TRUE(m.check(HostCaps{1, 1, 2, false}).ok());
}

TEST(ManifestCheckTest, EveryReasonHasADescription) {
  for (int r = static_cast<int>(CheckReason::None); r <= static_cast<int>(CheckReason::NoHostMode); ++r) {
    EXPECT_STRNE(GameCore::describe(static_cast<CheckReason>(r)), "unknown reason") << r;
  }
}

}  // namespace
