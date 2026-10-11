#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "Manifest.h"
#include "ModeTable.h"
#include "Roster.h"

using GameCore::Manifest;
using GameCore::ManifestError;
using GameCore::Mode;
using GameCore::ModeRow;

namespace {

// A manifest with `modes` and, when not empty, `default_mode`, seats wide enough for every mode.
std::string manifestWith(const std::string& modes, const std::string& defaultMode = "") {
  return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 2}, "modes": [)" + modes +
         "]" + (defaultMode.empty() ? "" : R"(, "default_mode": ")" + defaultMode + "\"") + "}";
}

TEST(ModeTableTest, ListsSoloPassNearbyInThatOrder) {
  ASSERT_EQ(GameCore::MODE_COUNT, 3u);
  EXPECT_EQ(GameCore::MODE_TABLE[0].mode, Mode::Solo);
  EXPECT_EQ(GameCore::MODE_TABLE[1].mode, Mode::Pass);
  EXPECT_EQ(GameCore::MODE_TABLE[2].mode, Mode::Nearby);
  EXPECT_STREQ(GameCore::MODE_TABLE[0].name, "solo");
  EXPECT_STREQ(GameCore::MODE_TABLE[1].name, "pass");
  EXPECT_STREQ(GameCore::MODE_TABLE[2].name, "nearby");
  EXPECT_EQ(GameCore::MODE_TABLE[0].bit, Manifest::MODE_SOLO);
  EXPECT_EQ(GameCore::MODE_TABLE[1].bit, Manifest::MODE_PASS);
  EXPECT_EQ(GameCore::MODE_TABLE[2].bit, Manifest::MODE_NEARBY);
}

TEST(ModeTableTest, EachModeNameAndBitRoundTrips) {
  std::set<std::string> names;
  uint8_t all = 0;
  for (const ModeRow& row : GameCore::MODE_TABLE) {
    EXPECT_EQ(GameCore::modeRow(row.mode), &row) << row.name;
    EXPECT_EQ(GameCore::modeRowForBit(row.bit), &row) << row.name;
    EXPECT_EQ(GameCore::modeRowForName(row.name), &row) << row.name;
    EXPECT_STREQ(GameCore::modeName(row.mode), row.name);
    EXPECT_NE(row.bit, 0);
    EXPECT_EQ(row.bit & (row.bit - 1), 0) << row.name << " has one bit";
    EXPECT_EQ(all & row.bit, 0) << row.name << " shares no bit";
    all = static_cast<uint8_t>(all | row.bit);
    names.insert(row.name);
  }
  EXPECT_EQ(names.size(), GameCore::MODE_COUNT) << "names are distinct";
  EXPECT_EQ(GameCore::ALL_MODE_BITS, all);
  EXPECT_EQ(GameCore::ALL_MODE_BITS, Manifest::MODE_SOLO | Manifest::MODE_PASS | Manifest::MODE_NEARBY);
}

TEST(ModeTableTest, RowsFollowTheEnumOrder) {
  // Mode is dense from 0 and the table lists it in order; ModeTable.h's static_assert ties the count to the last
  // enumerator, so a mode added without a row fails the build, not this test.
  for (size_t i = 0; i < GameCore::MODE_COUNT; ++i) EXPECT_EQ(GameCore::MODE_TABLE[i].mode, static_cast<Mode>(i));
}

TEST(ModeTableTest, RefusesAnUnknownNameBitOrMode) {
  for (const char* name : {"", "Solo", "SOLO", "solo ", " pass", "nearbyy", "online", "solo,pass"})
    EXPECT_EQ(GameCore::modeRowForName(name), nullptr) << '"' << name << '"';
  const uint8_t none = 0;
  const uint8_t two = Manifest::MODE_SOLO | Manifest::MODE_PASS;
  const uint8_t unknown = 0x08;
  const uint8_t knownAndUnknown = Manifest::MODE_SOLO | 0x08;
  for (const uint8_t bits : {none, two, unknown, knownAndUnknown, GameCore::ALL_MODE_BITS})
    EXPECT_EQ(GameCore::modeRowForBit(bits), nullptr) << static_cast<int>(bits);
  EXPECT_EQ(GameCore::modeRow(static_cast<Mode>(GameCore::MODE_COUNT)), nullptr);
  EXPECT_EQ(GameCore::modeRow(static_cast<Mode>(255)), nullptr);
}

TEST(ModeTableTest, ModeNameReadsAStrayValueAsSolo) {
  EXPECT_STREQ(GameCore::modeName(static_cast<Mode>(GameCore::MODE_COUNT)), "solo");
  EXPECT_STREQ(GameCore::modeName(static_cast<Mode>(255)), "solo");
}

TEST(ModeTableTest, ManifestParsesEachNameToItsBit) {
  for (const ModeRow& row : GameCore::MODE_TABLE) {
    Manifest m;
    ASSERT_EQ(Manifest::parse(manifestWith(std::string("\"") + row.name + "\"", row.name), m), ManifestError::None)
        << row.name;
    EXPECT_EQ(m.modes, row.bit) << row.name;
    EXPECT_EQ(m.defaultMode, row.bit) << row.name;
  }
  Manifest all;
  ASSERT_EQ(Manifest::parse(manifestWith(R"("nearby", "solo", "pass", "solo")"), all), ManifestError::None);
  EXPECT_EQ(all.modes, GameCore::ALL_MODE_BITS);
}

TEST(ModeTableTest, ManifestRefusesAnUnknownModeNameWithTheSameErrors) {
  Manifest m;
  EXPECT_EQ(Manifest::parse(manifestWith(R"("solo", "online")"), m), ManifestError::BadModes);
  EXPECT_EQ(Manifest::parse(manifestWith(R"("Solo")"), m), ManifestError::BadModes);
  EXPECT_EQ(Manifest::parse(manifestWith(R"("solo")", "online"), m), ManifestError::BadDefaultMode);
  EXPECT_EQ(Manifest::parse(manifestWith(R"("solo")", "pass"), m), ManifestError::BadDefaultMode) << "not one of modes";
}

TEST(ModeTableTest, StartModeReadsTheTableOrder) {
  Manifest m;
  ASSERT_EQ(Manifest::parse(manifestWith(R"("nearby", "pass", "solo")"), m), ManifestError::None);
  EXPECT_EQ(m.startMode(0, GameCore::ALL_MODE_BITS), Manifest::MODE_SOLO);
  EXPECT_EQ(m.startMode(0, Manifest::MODE_PASS | Manifest::MODE_NEARBY), Manifest::MODE_PASS);
  EXPECT_EQ(m.startMode(0, Manifest::MODE_NEARBY), Manifest::MODE_NEARBY);
  EXPECT_EQ(m.startMode(0, 0), 0);
  EXPECT_EQ(m.startMode(0x08, Manifest::MODE_PASS), Manifest::MODE_PASS) << "a remembered unknown bit is ignored";
}

}  // namespace
