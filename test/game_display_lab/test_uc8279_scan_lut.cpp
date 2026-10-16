// The UC8279 scan backend's pure parts (lib/GameDisplayLab/UC8279ScanLut and UC8279ScanSession): the five tables of a
// scan pinned byte for byte (D7's frame split, D9's levels), the rate list and labels, the planes from the two masks
// (D2), and the lab session's call sequence (D8) against a fake that refuses as the SDK seam documents
// (freeink-sdk docs/display-lab.md, Uc8279X4Driver's labTablesValid and labScan).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "LabExperiments.h"
#include "MaskPlanes.h"
#include "UC8279ScanLut.h"
#include "UC8279ScanSession.h"

namespace {

using namespace GameDisplayLab;
using Mask = MaskPlanes::Mask;

constexpr uint16_t WIDTH = 800;
constexpr uint16_t HEIGHT = 480;

using Tables = std::array<uint8_t, UC8279_SCAN_TABLE_BYTES>;

Tables poisoned() {
  Tables tables;
  tables.fill(0xA5);
  return tables;
}

Tables built(const uint8_t frames, const bool swap) {
  Tables tables = poisoned();
  EXPECT_TRUE(buildUc8279ScanTables(tables.data(), frames, swap)) << "frames " << +frames;
  return tables;
}

// The expected tables, written out independently of the builder: each table's prefix, the group
// [0x01, P1..P4, 0x01, 0x01] whose phases carry `phases` at the table's level (top two bits), then zeros.
constexpr uint8_t GND = 0x00;
constexpr uint8_t VSH = 0x40;
constexpr uint8_t VSL = 0x80;

Tables expected(const std::array<uint8_t, 4>& phases, const bool swap) {
  const uint8_t levels[UC8279_TABLES] = {GND, GND, swap ? VSH : VSL, swap ? VSL : VSH, GND};
  Tables tables{};
  for (size_t t = 0; t < UC8279_TABLES; ++t) {
    uint8_t* table = tables.data() + t * UC8279_TABLE_BYTES;
    table[0] = static_cast<uint8_t>(0x20 + t);
    table[1] = 0x01;
    for (size_t p = 0; p < 4; ++p) table[2 + p] = phases[p] == 0 ? 0x00 : static_cast<uint8_t>(levels[t] | phases[p]);
    table[6] = 0x01;
    table[7] = 0x01;
  }
  return tables;
}

void expectTables(const Tables& got, const Tables& want, const char* what) {
  for (size_t i = 0; i < UC8279_SCAN_TABLE_BYTES; ++i) {
    ASSERT_EQ(got[i], want[i]) << what << ": table " << i / UC8279_TABLE_BYTES << " byte " << i % UC8279_TABLE_BYTES;
  }
}

TEST(Uc8279ScanLut, Sizes) {
  EXPECT_EQ(UC8279_TABLES, 5u);
  EXPECT_EQ(UC8279_TABLE_BYTES, 43u);
  EXPECT_EQ(UC8279_SCAN_TABLE_BYTES, 215u);
  EXPECT_EQ(UC8279_MAX_FRAMES, 252u);
}

TEST(Uc8279ScanLut, FramesSplitOverTheFourPhasesSixtyThreeAtATime) {
  struct Case {
    uint8_t frames;
    std::array<uint8_t, 4> phases;
  };
  const Case cases[] = {
      {0, {0, 0, 0, 0}},       {1, {1, 0, 0, 0}},       {10, {10, 0, 0, 0}},   {63, {63, 0, 0, 0}},
      {64, {63, 1, 0, 0}},     {100, {63, 37, 0, 0}},   {126, {63, 63, 0, 0}}, {189, {63, 63, 63, 0}},
      {251, {63, 63, 63, 62}}, {252, {63, 63, 63, 63}},
  };
  for (const Case& c : cases) {
    for (const bool swap : {false, true}) {
      const std::string what = "frames " + std::to_string(c.frames) + " swap " + std::to_string(swap);
      expectTables(built(c.frames, swap), expected(c.phases, swap), what.c_str());
    }
  }
}

TEST(Uc8279ScanLut, HundredFramesUnswappedIsTheDesignNotesExample) {
  const Tables tables = built(100, false);
  const uint8_t vcom[] = {0x20, 0x01, 0x3F, 0x25, 0x00, 0x00, 0x01, 0x01};
  const uint8_t ww[] = {0x21, 0x01, 0x3F, 0x25, 0x00, 0x00, 0x01, 0x01};
  const uint8_t kw[] = {0x22, 0x01, 0xBF, 0xA5, 0x00, 0x00, 0x01, 0x01};
  const uint8_t wk[] = {0x23, 0x01, 0x7F, 0x65, 0x00, 0x00, 0x01, 0x01};
  const uint8_t kk[] = {0x24, 0x01, 0x3F, 0x25, 0x00, 0x00, 0x01, 0x01};
  const uint8_t* heads[UC8279_TABLES] = {vcom, ww, kw, wk, kk};
  for (size_t t = 0; t < UC8279_TABLES; ++t) {
    const uint8_t* table = tables.data() + t * UC8279_TABLE_BYTES;
    EXPECT_EQ(memcmp(table, heads[t], 8), 0) << "table " << t;
    for (size_t i = 8; i < UC8279_TABLE_BYTES; ++i) EXPECT_EQ(table[i], 0x00) << "table " << t << " byte " << i;
  }
}

TEST(Uc8279ScanLut, SwapExchangesKwAndWkLevelsOnly) {
  const Tables plain = built(10, false);
  const Tables swapped = built(10, true);
  constexpr size_t KW_P1 = 2 * UC8279_TABLE_BYTES + 2;
  constexpr size_t WK_P1 = 3 * UC8279_TABLE_BYTES + 2;
  EXPECT_EQ(plain[KW_P1], 0x8A);    // VSL, 10 frames
  EXPECT_EQ(plain[WK_P1], 0x4A);    // VSH
  EXPECT_EQ(swapped[KW_P1], 0x4A);  // VSH
  EXPECT_EQ(swapped[WK_P1], 0x8A);  // VSL
  for (size_t i = 0; i < UC8279_SCAN_TABLE_BYTES; ++i) {
    if (i == KW_P1 || i == WK_P1) continue;
    EXPECT_EQ(swapped[i], plain[i]) << "byte " << i;
  }
}

TEST(Uc8279ScanLut, OverTheCeilingIsRefusedAndWritesNothing) {
  for (const uint8_t frames : {static_cast<uint8_t>(253), static_cast<uint8_t>(254), static_cast<uint8_t>(255)}) {
    for (const bool swap : {false, true}) {
      Tables tables = poisoned();
      EXPECT_FALSE(buildUc8279ScanTables(tables.data(), frames, swap)) << "frames " << +frames;
      for (const uint8_t b : tables) ASSERT_EQ(b, 0xA5) << "frames " << +frames;
    }
  }
}

TEST(Uc8279ScanLut, EveryExperimentFrameCountBuilds) {
  for (const bool swap : {false, true}) {
    for (const uint8_t tp : FRAME_PERIOD_TPS) {
      Tables tables = poisoned();
      EXPECT_TRUE(buildUc8279ScanTables(tables.data(), tp, swap)) << "tp " << +tp;
    }
    Tables tables = poisoned();
    EXPECT_TRUE(buildUc8279ScanTables(tables.data(), POLARITY_TP, swap));
  }
}

TEST(Uc8279ScanLut, PrefixesAreTheSeamsRegisterOrder) {
  for (const bool swap : {false, true}) {
    const Tables tables = built(POLARITY_TP, swap);
    for (size_t t = 0; t < UC8279_TABLES; ++t) EXPECT_EQ(tables[t * UC8279_TABLE_BYTES], 0x20 + t) << "table " << t;
  }
}

TEST(Uc8279ScanLut, RateListIsThePllCodesInOrder) {
  EXPECT_EQ(UC8279_RATE_COUNT, 3u);
  EXPECT_EQ(UC8279_PLL_CODES[0], 0x0E);  // the driver's stock pll, so rate index 0 is stock
  EXPECT_EQ(UC8279_PLL_CODES[1], 0x0D);
  EXPECT_EQ(UC8279_PLL_CODES[2], 0x0F);
  EXPECT_STREQ(uc8279RateLabel(0), "0x0E");
  EXPECT_STREQ(uc8279RateLabel(1), "0x0D");
  EXPECT_STREQ(uc8279RateLabel(2), "0x0F");
  for (uint8_t i = 0; i < UC8279_RATE_COUNT; ++i) {
    char label[8];
    snprintf(label, sizeof(label), "0x%02X", static_cast<unsigned>(UC8279_PLL_CODES[i]));
    EXPECT_STREQ(uc8279RateLabel(i), label) << "rate " << +i;
    uint8_t code = 0;
    EXPECT_TRUE(uc8279PllCode(i, code)) << "rate " << +i;
    EXPECT_EQ(code, UC8279_PLL_CODES[i]) << "rate " << +i;
  }
  for (const uint8_t bad : {static_cast<uint8_t>(3), static_cast<uint8_t>(16), static_cast<uint8_t>(255)}) {
    EXPECT_EQ(uc8279RateLabel(bad), nullptr) << "rate " << +bad;
    uint8_t code = 0x5A;
    EXPECT_FALSE(uc8279PllCode(bad, code)) << "rate " << +bad;
    EXPECT_EQ(code, 0x5A) << "rate " << +bad;
  }
}

TEST(Uc8279ScanLut, RowLabelsNameEachRowsTableAndTheLevelItCarries) {
  constexpr size_t KW_P1 = 2 * UC8279_TABLE_BYTES + 2;
  constexpr size_t WK_P1 = 3 * UC8279_TABLE_BYTES + 2;
  auto levelName = [](const uint8_t phase) {
    return (phase & 0xC0) == VSL ? "VSL" : (phase & 0xC0) == VSH ? "VSH" : "?";
  };
  for (const bool swap : {false, true}) {
    const Tables tables = built(POLARITY_TP, swap);
    EXPECT_EQ(std::string("KW ") + levelName(tables[KW_P1]), uc8279RowLabel(1, swap)) << "swap " << swap;
    EXPECT_EQ(std::string("WK ") + levelName(tables[WK_P1]), uc8279RowLabel(2, swap)) << "swap " << swap;
    EXPECT_EQ(uc8279RowLabel(0, swap), nullptr);
    EXPECT_EQ(uc8279RowLabel(3, swap), nullptr);
  }
  // The labels the polarity screen shows: "Row 1 (KW VSL)", "Row 2 (WK VSH)".
  EXPECT_STREQ(uc8279RowLabel(1, false), "KW VSL");
  EXPECT_STREQ(uc8279RowLabel(2, false), "WK VSH");
  EXPECT_STREQ(uc8279RowLabel(1, true), "KW VSH");
  EXPECT_STREQ(uc8279RowLabel(2, true), "WK VSL");
}

// Full-screen masks and the planes encoded from them.
struct Screen {
  std::vector<uint8_t> black = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> white = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> oldPlane = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> newPlane = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  MaskPlanes masks{black.data(), white.data(), WIDTH, HEIGHT};

  void encode() { encodeUc8279Planes(black.data(), white.data(), oldPlane.data(), newPlane.data(), oldPlane.size()); }
};

void expectEvery(const std::vector<uint8_t>& plane, const uint8_t value, const char* name) {
  for (size_t i = 0; i < plane.size(); ++i) ASSERT_EQ(plane[i], value) << name << " byte " << i;
}

TEST(Uc8279Planes, AllHoldIsWhiteInBothPlanes) {
  Screen screen;
  screen.masks.clear();
  screen.encode();
  expectEvery(screen.oldPlane, 0xFF, "old");  // (1, 1): WW
  expectEvery(screen.newPlane, 0xFF, "new");
}

TEST(Uc8279Planes, PolarityRowsSelectKwThenWk) {
  // Row 1 fills the white mask: (0, 1), KW. Row 2 fills the black mask: (1, 0), WK.
  constexpr uint8_t OLD[POLARITY_ROWS] = {0x00, 0xFF};
  constexpr uint8_t NEW[POLARITY_ROWS] = {0xFF, 0x00};
  for (size_t row = 0; row < POLARITY_ROWS; ++row) {
    Screen screen;
    screen.masks.clear();
    screen.masks.fill(POLARITY_ROW_MASKS[row]);
    screen.encode();
    SCOPED_TRACE(row + 1);
    expectEvery(screen.oldPlane, OLD[row], "old");
    expectEvery(screen.newPlane, NEW[row], "new");
  }
}

TEST(Uc8279Planes, PlanesAreTheInvertedMasksPixelForPixel) {
  Screen screen;
  screen.masks.clear();
  screen.masks.setPixel(Mask::White, 0, 0);
  screen.masks.setPixel(Mask::Black, 0, 0);  // both masks: (0, 0), KK, which holds at GND
  screen.masks.setRect(Mask::White, 8, 0, 8, 1);
  screen.masks.setPixel(Mask::Black, 799, 479);
  screen.encode();
  EXPECT_EQ(screen.oldPlane[0], 0x7F);
  EXPECT_EQ(screen.newPlane[0], 0x7F);
  EXPECT_EQ(screen.oldPlane[1], 0x00);  // drive to white: old 0
  EXPECT_EQ(screen.newPlane[1], 0xFF);  //                  new 1
  EXPECT_EQ(screen.oldPlane[47999], 0xFF);
  EXPECT_EQ(screen.newPlane[47999], 0xFE);
  for (size_t i = 0; i < screen.oldPlane.size(); ++i) {
    ASSERT_EQ(screen.oldPlane[i], static_cast<uint8_t>(~screen.white[i])) << "byte " << i;
    ASSERT_EQ(screen.newPlane[i], static_cast<uint8_t>(~screen.black[i])) << "byte " << i;
  }
}

// FreeInkDisplay's lab API as freeink-sdk docs/display-lab.md and Uc8279X4Driver define it, recorded: it refuses an
// upload whose length is not 215 or whose prefixes are not 0x20-0x24, and a labScan before an upload since the last
// labEnd. More permissive than the device: it never refuses for inversion (the lab wrapper checks that) and never
// times BUSY.
struct RecordingLab {
  enum class Call { Supported, Upload, Rate, Scan, End };

  bool supported = true;
  bool refuseRate = false;
  bool refuseScan = false;
  uint32_t scanUs = 12345;

  mutable std::vector<Call> calls;  // labSupported() is const, as on FreeInkDisplay, and still recorded
  std::vector<uint8_t> uploaded;
  uint8_t pll = 0;
  const uint8_t* plane1 = nullptr;
  const uint8_t* plane2 = nullptr;
  bool loaded = false;

  bool labSupported() const {
    calls.push_back(Call::Supported);
    return supported;
  }
  bool labUploadWaveform(const uint8_t* bytes, const size_t len) {
    calls.push_back(Call::Upload);
    if (bytes == nullptr || len != UC8279_SCAN_TABLE_BYTES) return false;
    for (size_t t = 0; t < UC8279_TABLES; ++t) {
      if (bytes[t * UC8279_TABLE_BYTES] != 0x20 + t) return false;
    }
    uploaded.assign(bytes, bytes + len);
    loaded = true;
    return true;
  }
  bool labSetFrameRate(const uint8_t code) {
    calls.push_back(Call::Rate);
    if (refuseRate) return false;
    pll = code;
    return true;
  }
  bool labScan(const uint8_t* p1, const uint8_t* p2, uint32_t& busyUs) {
    calls.push_back(Call::Scan);
    busyUs = 0;
    if (!loaded || refuseScan || p1 == nullptr || p2 == nullptr) return false;
    plane1 = p1;
    plane2 = p2;
    busyUs = scanUs;
    return true;
  }
  void labEnd() {
    calls.push_back(Call::End);
    loaded = false;
  }
};

using Call = RecordingLab::Call;

struct Session {
  Tables tables = built(POLARITY_TP, false);
  uint8_t oldPlane[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t newPlane[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint32_t busyUs = 999;

  ScanOutcome run(RecordingLab& lab, const uint8_t pllCode = 0x0E) {
    return runUc8279Scan(lab, tables.data(), pllCode, oldPlane, newPlane, busyUs);
  }
};

TEST(Uc8279Session, AllHoldScanUploadsSetsPllScansAndEnds) {
  for (uint8_t rate = 0; rate < UC8279_RATE_COUNT; ++rate) {
    for (const uint8_t frames : FRAME_PERIOD_TPS) {
      RecordingLab lab;
      Session session;
      session.tables = built(frames, false);
      uint8_t code = 0;
      ASSERT_TRUE(uc8279PllCode(rate, code));
      EXPECT_EQ(session.run(lab, code), ScanOutcome::Done);
      EXPECT_EQ(lab.calls, (std::vector<Call>{Call::Supported, Call::Upload, Call::Rate, Call::Scan, Call::End}));
      EXPECT_EQ(lab.uploaded, std::vector<uint8_t>(session.tables.begin(), session.tables.end()));
      EXPECT_EQ(lab.pll, UC8279_PLL_CODES[rate]);
      EXPECT_EQ(lab.plane1, session.oldPlane);  // DTM1 is the old plane, DTM2 the new
      EXPECT_EQ(lab.plane2, session.newPlane);
      EXPECT_EQ(session.busyUs, lab.scanUs);
      EXPECT_FALSE(lab.loaded);  // labEnd ended the session
    }
  }
}

TEST(Uc8279Session, EveryScanIsItsOwnSession) {
  // D8: the second scan uploads again; the fake refuses a labScan that follows a labEnd without one.
  RecordingLab lab;
  Session session;
  EXPECT_EQ(session.run(lab), ScanOutcome::Done);
  EXPECT_EQ(session.run(lab, 0x0F), ScanOutcome::Done);
  EXPECT_EQ(lab.calls, (std::vector<Call>{Call::Supported, Call::Upload, Call::Rate, Call::Scan, Call::End,
                                          Call::Supported, Call::Upload, Call::Rate, Call::Scan, Call::End}));
  EXPECT_EQ(lab.pll, 0x0F);
}

TEST(Uc8279Session, UnsupportedMakesNoOtherCall) {
  RecordingLab lab;
  lab.supported = false;
  Session session;
  EXPECT_EQ(session.run(lab), ScanOutcome::Unsupported);
  EXPECT_EQ(lab.calls, std::vector<Call>{Call::Supported});
  EXPECT_EQ(session.busyUs, 0u);
}

TEST(Uc8279Session, RefusedUploadEndsTheSession) {
  RecordingLab lab;
  Session session;
  session.tables[3 * UC8279_TABLE_BYTES] = 0x24;  // a wrong prefix, as labTablesValid refuses
  EXPECT_EQ(session.run(lab), ScanOutcome::UploadRefused);
  EXPECT_EQ(lab.calls, (std::vector<Call>{Call::Supported, Call::Upload, Call::End}));
  EXPECT_EQ(session.busyUs, 0u);
}

TEST(Uc8279Session, RefusedFrameRateEndsTheSession) {
  // The X4 Classic's refusal: unreachable from the lab (X4 Pro only), still mapped with labEnd.
  RecordingLab lab;
  lab.refuseRate = true;
  Session session;
  EXPECT_EQ(session.run(lab), ScanOutcome::RateRefused);
  EXPECT_EQ(lab.calls, (std::vector<Call>{Call::Supported, Call::Upload, Call::Rate, Call::End}));
  EXPECT_EQ(session.busyUs, 0u);
}

TEST(Uc8279Session, RefusedScanEndsTheSessionWithNoTime) {
  RecordingLab lab;
  lab.refuseScan = true;
  Session session;
  EXPECT_EQ(session.run(lab), ScanOutcome::ScanRefused);
  EXPECT_EQ(lab.calls, (std::vector<Call>{Call::Supported, Call::Upload, Call::Rate, Call::Scan, Call::End}));
  EXPECT_EQ(session.busyUs, 0u);
}

TEST(Uc8279Session, FakeRefusesAsTheSeamDocuments) {
  // The fake's own rules, so the session tests above lean on a faithful stand-in.
  RecordingLab lab;
  uint32_t busyUs = 1;
  const uint8_t plane[1] = {0xFF};
  EXPECT_FALSE(lab.labScan(plane, plane, busyUs));  // before any upload
  EXPECT_EQ(busyUs, 0u);
  Tables tables = built(1, false);
  EXPECT_FALSE(lab.labUploadWaveform(tables.data(), UC8279_SCAN_TABLE_BYTES - 1));
  EXPECT_FALSE(lab.labUploadWaveform(nullptr, UC8279_SCAN_TABLE_BYTES));
  EXPECT_TRUE(lab.labUploadWaveform(tables.data(), UC8279_SCAN_TABLE_BYTES));
  EXPECT_TRUE(lab.labScan(plane, plane, busyUs));
  lab.labEnd();
  EXPECT_FALSE(lab.labScan(plane, plane, busyUs));  // after labEnd, until the next upload
}

// ScanRequest's fields (src/activities/games/display_lab/ScanBackend.h), as runUc8279Request reads them.
struct Request {
  const uint8_t* blackMask;
  const uint8_t* whiteMask;
  uint8_t frames;
  uint8_t rate;
  bool swapPolarity;
};

TEST(Uc8279Request, BadRequestsMakeNoLabCallAndWriteNothing) {
  Screen screen;
  screen.masks.clear();
  const Request requests[] = {
      {nullptr, screen.white.data(), 10, 0, false},              // NoMasks
      {screen.black.data(), nullptr, 10, 0, false},              // NoMasks
      {screen.black.data(), screen.white.data(), 10, 3, false},  // BadRate
      {screen.black.data(), screen.white.data(), 10, 255, false},
      {screen.black.data(), screen.white.data(), 253, 0, false},  // TooManyFrames
      {screen.black.data(), screen.white.data(), 255, 2, true},
  };
  const ScanOutcome outcomes[] = {ScanOutcome::NoMasks, ScanOutcome::NoMasks,       ScanOutcome::BadRate,
                                  ScanOutcome::BadRate, ScanOutcome::TooManyFrames, ScanOutcome::TooManyFrames};
  for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
    SCOPED_TRACE(i);
    RecordingLab lab;
    Tables tables = poisoned();
    std::vector<uint8_t> oldPlane(screen.oldPlane.size(), 0xA5);
    std::vector<uint8_t> newPlane(screen.newPlane.size(), 0xA5);
    uint32_t busyUs = 999;
    EXPECT_EQ(
        runUc8279Request(lab, requests[i], tables.data(), oldPlane.data(), newPlane.data(), oldPlane.size(), busyUs),
        outcomes[i]);
    EXPECT_TRUE(lab.calls.empty());
    EXPECT_EQ(busyUs, 0u);
    for (const uint8_t b : tables) ASSERT_EQ(b, 0xA5);
    expectEvery(oldPlane, 0xA5, "old");
    expectEvery(newPlane, 0xA5, "new");
  }
}

TEST(Uc8279Request, PolarityRequestSendsItsTablesPllAndPlanes) {
  for (const bool swap : {false, true}) {
    Screen screen;
    screen.masks.clear();
    screen.masks.fill(Mask::White);  // polarity row 1
    const Request request{screen.black.data(), screen.white.data(), POLARITY_TP, 0, swap};
    RecordingLab lab;
    Tables tables = poisoned();
    uint32_t busyUs = 0;
    EXPECT_EQ(runUc8279Request(lab, request, tables.data(), screen.oldPlane.data(), screen.newPlane.data(),
                               screen.oldPlane.size(), busyUs),
              ScanOutcome::Done);
    EXPECT_EQ(lab.calls, (std::vector<Call>{Call::Supported, Call::Upload, Call::Rate, Call::Scan, Call::End}));
    const Tables want = built(POLARITY_TP, swap);
    EXPECT_EQ(lab.uploaded, std::vector<uint8_t>(want.begin(), want.end()));
    EXPECT_EQ(lab.pll, 0x0E);
    EXPECT_EQ(lab.plane1, screen.oldPlane.data());
    EXPECT_EQ(lab.plane2, screen.newPlane.data());
    expectEvery(screen.oldPlane, 0x00, "old");  // (0, 1): KW
    expectEvery(screen.newPlane, 0xFF, "new");
    EXPECT_EQ(busyUs, lab.scanUs);
  }
}

TEST(Uc8279Request, FramePeriodRequestsSendEachRatesPllAndTables) {
  // Every frame-period scan: all-hold masks, every FRAME_PERIOD_TPS against every rate.
  for (uint8_t rate = 0; rate < UC8279_RATE_COUNT; ++rate) {
    for (const uint8_t frames : FRAME_PERIOD_TPS) {
      SCOPED_TRACE(testing::Message() << "rate " << +rate << " frames " << +frames);
      Screen screen;
      screen.masks.clear();
      const Request request{screen.black.data(), screen.white.data(), frames, rate, false};
      RecordingLab lab;
      Tables tables = poisoned();
      uint32_t busyUs = 0;
      EXPECT_EQ(runUc8279Request(lab, request, tables.data(), screen.oldPlane.data(), screen.newPlane.data(),
                                 screen.oldPlane.size(), busyUs),
                ScanOutcome::Done);
      EXPECT_EQ(lab.calls, (std::vector<Call>{Call::Supported, Call::Upload, Call::Rate, Call::Scan, Call::End}));
      EXPECT_EQ(lab.pll, UC8279_PLL_CODES[rate]);
      const Tables want = built(frames, false);
      EXPECT_EQ(lab.uploaded, std::vector<uint8_t>(want.begin(), want.end()));
      expectEvery(screen.oldPlane, 0xFF, "old");  // (1, 1): WW, hold
      expectEvery(screen.newPlane, 0xFF, "new");
      EXPECT_EQ(busyUs, lab.scanUs);
    }
  }
}

TEST(Uc8279Session, OutcomeNamesForTheLog) {
  EXPECT_EQ(scanOutcomeName(ScanOutcome::Done), nullptr);
  EXPECT_STREQ(scanOutcomeName(ScanOutcome::NoMasks), "request without masks");
  EXPECT_STREQ(scanOutcomeName(ScanOutcome::BadRate), "rate index out of range");
  EXPECT_STREQ(scanOutcomeName(ScanOutcome::TooManyFrames), "frames over 252");
  EXPECT_STREQ(scanOutcomeName(ScanOutcome::Unsupported), "lab unsupported");
  EXPECT_STREQ(scanOutcomeName(ScanOutcome::UploadRefused), "upload refused");
  EXPECT_STREQ(scanOutcomeName(ScanOutcome::RateRefused), "frame rate refused");
  EXPECT_STREQ(scanOutcomeName(ScanOutcome::ScanRefused), "scan refused");
}

}  // namespace
