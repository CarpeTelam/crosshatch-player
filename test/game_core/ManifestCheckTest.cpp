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
// Fields in order: api, minApi, maxSeats, nearby, pass.
constexpr HostCaps HOST{3, 2, 2, false, true};
constexpr HostCaps NEARBY_HOST{3, 2, 2, true, true};
// A host without Pass and Play (the firmware turned it on in epic-pass-and-play entry 1).
constexpr HostCaps NO_PASS_HOST{3, 2, 2, false, false};

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

TEST(ManifestCheckTest, PassNeedsSeatsMaxTwo) {
  // A pass match has at least two seats, so a manifest offering pass with one is broken.
  expectVerdict(game(2, 1, 1, Manifest::MODE_PASS).check(HOST), CheckStatus::Invalid, CheckReason::NearbyNeedsTwoSeats);
  expectVerdict(game(2, 1, 1, Manifest::MODE_SOLO | Manifest::MODE_PASS).check(HOST), CheckStatus::Invalid,
                CheckReason::NearbyNeedsTwoSeats);
  expectVerdict(game(2, 1, 1, Manifest::MODE_SOLO | Manifest::MODE_PASS | Manifest::MODE_NEARBY).check(NEARBY_HOST),
                CheckStatus::Invalid, CheckReason::NearbyNeedsTwoSeats);
  const CheckResult passOnly = game(2, 1, 2, Manifest::MODE_PASS).check(HOST);
  EXPECT_TRUE(passOnly.ok());
  EXPECT_EQ(passOnly.modes, Manifest::MODE_PASS);
  const CheckResult soloAndPass = game(2, 1, 2, Manifest::MODE_SOLO | Manifest::MODE_PASS).check(HOST);
  EXPECT_TRUE(soloAndPass.ok());
  EXPECT_EQ(soloAndPass.modes, Manifest::MODE_SOLO | Manifest::MODE_PASS);
  // Invalid wins over a host without pass: the package is broken wherever it goes.
  expectVerdict(game(2, 1, 1, Manifest::MODE_PASS).check(NO_PASS_HOST), CheckStatus::Invalid,
                CheckReason::NearbyNeedsTwoSeats);
  expectVerdict(game(2, 1, 1, Manifest::MODE_SOLO | Manifest::MODE_PASS).check(NO_PASS_HOST), CheckStatus::Invalid,
                CheckReason::NearbyNeedsTwoSeats);
}

TEST(ManifestCheckTest, TheSeatRuleSaysItCoversPassAndNearby) {
  EXPECT_STREQ(GameCore::describe(CheckReason::NearbyNeedsTwoSeats), "pass and nearby need seats.max 2 or more");
}

TEST(ManifestCheckTest, NearbyOnlyWithoutTheRadioIsUnavailable) {
  expectVerdict(game(2, 2, 2, Manifest::MODE_NEARBY).check(HOST), CheckStatus::Unavailable, CheckReason::NoHostMode);
}

TEST(ManifestCheckTest, NearbyNeedsTwoHostSeats) {
  constexpr HostCaps oneSeat{3, 2, 1, true, true};
  expectVerdict(game(2, 1, 2, Manifest::MODE_NEARBY).check(oneSeat), CheckStatus::Unavailable, CheckReason::NoHostMode);
}

TEST(ManifestCheckTest, OkDropsModesTheHostCannotStart) {
  const uint8_t all = Manifest::MODE_SOLO | Manifest::MODE_PASS | Manifest::MODE_NEARBY;
  const CheckResult withoutRadio = game(2, 1, 2, all).check(HOST);
  EXPECT_TRUE(withoutRadio.ok());
  EXPECT_EQ(withoutRadio.modes, Manifest::MODE_SOLO | Manifest::MODE_PASS);
  EXPECT_EQ(game(2, 1, 2, all).check(NEARBY_HOST).modes, all);
}

TEST(ManifestCheckTest, AHostWithoutPassOffersOnlySolo) {
  const uint8_t all = Manifest::MODE_SOLO | Manifest::MODE_PASS | Manifest::MODE_NEARBY;
  const CheckResult withoutPass = game(2, 1, 2, Manifest::MODE_SOLO | Manifest::MODE_PASS).check(NO_PASS_HOST);
  EXPECT_TRUE(withoutPass.ok());
  EXPECT_EQ(withoutPass.modes, Manifest::MODE_SOLO);
  // Neither pass nor nearby without their capabilities.
  EXPECT_EQ(game(2, 1, 2, all).check(NO_PASS_HOST).modes, Manifest::MODE_SOLO);
  // Nearby's radio does not bring pass with it.
  constexpr HostCaps radioOnly{3, 2, 2, true, false};
  EXPECT_EQ(game(2, 1, 2, all).check(radioOnly).modes, Manifest::MODE_SOLO | Manifest::MODE_NEARBY);
  // The capability alone offers pass.
  EXPECT_EQ(game(2, 1, 2, all).check(HOST).modes, Manifest::MODE_SOLO | Manifest::MODE_PASS);
}

TEST(ManifestCheckTest, PassOnlyOnAHostWithoutPassIsUnavailable) {
  expectVerdict(game(2, 2, 2, Manifest::MODE_PASS).check(NO_PASS_HOST), CheckStatus::Unavailable,
                CheckReason::NoHostMode);
  expectVerdict(game(2, 1, 2, Manifest::MODE_PASS).check(NO_PASS_HOST), CheckStatus::Unavailable,
                CheckReason::NoHostMode);
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
  Manifest underscore = solo();
  std::strcpy(underscore.icon, "old_name");
  expectVerdict(underscore.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest badWeight = solo();
  badWeight.iconWeight = 2;
  expectVerdict(badWeight.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest unterminated = solo();
  std::memset(unterminated.version, 'v', sizeof(unterminated.version));
  expectVerdict(unterminated.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  // An icon with no terminator: only the length cap in validIcon refuses it, and the installer's strlen
  // of the icon would otherwise read past the field.
  Manifest unterminatedIcon = solo();
  std::memset(unterminatedIcon.icon, 'x', sizeof(unterminatedIcon.icon));
  expectVerdict(unterminatedIcon.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  // default_mode is one of the modes, one bit; settingsCount at most MAX_SETTINGS.
  Manifest otherDefault = solo();
  otherDefault.defaultMode = Manifest::MODE_PASS;
  expectVerdict(otherDefault.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest twoDefaults = game(2, 1, 2, Manifest::MODE_SOLO | Manifest::MODE_PASS);
  twoDefaults.defaultMode = Manifest::MODE_SOLO | Manifest::MODE_PASS;
  expectVerdict(twoDefaults.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest tooManySettings = solo();
  tooManySettings.settingsCount = Manifest::MAX_SETTINGS + 1;
  expectVerdict(tooManySettings.check(HOST), CheckStatus::Invalid, CheckReason::BadFields);
  Manifest fine = game(2, 1, 2, Manifest::MODE_SOLO | Manifest::MODE_PASS);
  fine.defaultMode = Manifest::MODE_PASS;
  fine.settingsCount = Manifest::MAX_SETTINGS;
  EXPECT_TRUE(fine.check(HOST).ok());
}

TEST(ManifestCheckTest, InvalidWinsOverUnavailable) {
  // api too new and a solo game needing two seats: the package is broken first.
  expectVerdict(game(9, 2, 2, Manifest::MODE_SOLO).check(HOST), CheckStatus::Invalid, CheckReason::SoloNeedsOneSeat);
}

TEST(ManifestCheckTest, ParsedManifestPassesCheck) {
  Manifest m;
  ASSERT_EQ(Manifest::parse(R"({"id": "tracer", "name": "Tracer", "version": "0.1", "api": 1,
                                "seats": {"min": 1, "max": 1}, "modes": ["solo"], "icon": "x"})",
                            m),
            GameCore::ManifestError::None);
  EXPECT_TRUE(m.check(HostCaps{1, 1, 2, false, false}).ok());
}

TEST(ManifestCheckTest, EveryReasonHasADescription) {
  for (int r = static_cast<int>(CheckReason::None); r <= static_cast<int>(CheckReason::NoHostMode); ++r) {
    EXPECT_STRNE(GameCore::describe(static_cast<CheckReason>(r)), "unknown reason") << r;
  }
}

}  // namespace
