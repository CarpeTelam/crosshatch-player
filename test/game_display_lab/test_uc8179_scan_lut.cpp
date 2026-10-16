// The UC8179 scan backend's pure parts (lib/GameDisplayLab/UC8179ScanLut and UC8179ScanSession): the five tables of a
// scan pinned byte for byte (D3's one group of one phase, D9's levels), the rate list and labels, the planes from the
// two masks (D2), and the request-to-session step's call sequence (D8) against a fake that refuses as the SDK seam
// documents for the UC8179 (freeink-sdk docs/display-lab.md, Uc8179Driver's labTablesValid, labSetFrameRate and
// labScan).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "LabExperiments.h"
#include "MaskPlanes.h"
#include "UC8179ScanLut.h"
#include "UC8179ScanSession.h"

namespace {

using namespace GameDisplayLab;
using Mask = MaskPlanes::Mask;

constexpr uint16_t WIDTH = 800;
constexpr uint16_t HEIGHT = 480;

using Tables = std::array<uint8_t, UC8179_SCAN_TABLE_BYTES>;

Tables poisoned() {
  Tables tables;
  tables.fill(0xA5);
  return tables;
}

Tables built(const uint8_t frames, const bool swap) {
  Tables tables = poisoned();
  buildUc8179ScanTables(tables.data(), frames, swap);
  return tables;
}

// The expected tables, written out independently of the builder: each table's prefix, the group
// [level, frames, 0, 0, 0, 0x01] with the table's level in phase A's bits 7-6, then zeros. Level 00 is GND on WW and KK
// and VCOM DC on the VCOM table.
constexpr uint8_t GND = 0x00;
constexpr uint8_t VDH = 0x40;
constexpr uint8_t VDL = 0x80;

Tables expected(const uint8_t frames, const bool swap) {
  const uint8_t levels[UC8179_TABLES] = {GND, GND, swap ? VDH : VDL, swap ? VDL : VDH, GND};
  Tables tables{};
  for (size_t t = 0; t < UC8179_TABLES; ++t) {
    uint8_t* table = tables.data() + t * UC8179_TABLE_BYTES;
    table[0] = static_cast<uint8_t>(0x20 + t);
    table[1] = levels[t];
    table[2] = frames;
    table[6] = 0x01;
  }
  return tables;
}

void expectTables(const Tables& got, const Tables& want, const char* what) {
  for (size_t i = 0; i < UC8179_SCAN_TABLE_BYTES; ++i) {
    ASSERT_EQ(got[i], want[i]) << what << ": table " << i / UC8179_TABLE_BYTES << " byte " << i % UC8179_TABLE_BYTES;
  }
}

constexpr size_t KW_LEVEL = 2 * UC8179_TABLE_BYTES + 1;
constexpr size_t WK_LEVEL = 3 * UC8179_TABLE_BYTES + 1;

TEST(Uc8179ScanLut, Sizes) {
  EXPECT_EQ(UC8179_TABLES, 5u);
  EXPECT_EQ(UC8179_TABLE_BYTES, 43u);
  EXPECT_EQ(UC8179_SCAN_TABLE_BYTES, 215u);
}

TEST(Uc8179ScanLut, EveryByteForEachFrameCountAndPolarity) {
  for (const uint8_t frames : {static_cast<uint8_t>(0), static_cast<uint8_t>(1), static_cast<uint8_t>(10),
                               static_cast<uint8_t>(100), static_cast<uint8_t>(255)}) {
    for (const bool swap : {false, true}) {
      const std::string what = "frames " + std::to_string(frames) + " swap " + std::to_string(swap);
      expectTables(built(frames, swap), expected(frames, swap), what.c_str());
    }
  }
}

TEST(Uc8179ScanLut, TenFramesUnswappedIsTheDesignNotesExample) {
  const Tables tables = built(10, false);
  const uint8_t vcom[] = {0x20, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01};
  const uint8_t ww[] = {0x21, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01};
  const uint8_t kw[] = {0x22, 0x80, 0x0A, 0x00, 0x00, 0x00, 0x01};
  const uint8_t wk[] = {0x23, 0x40, 0x0A, 0x00, 0x00, 0x00, 0x01};
  const uint8_t kk[] = {0x24, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01};
  const uint8_t* heads[UC8179_TABLES] = {vcom, ww, kw, wk, kk};
  for (size_t t = 0; t < UC8179_TABLES; ++t) {
    const uint8_t* table = tables.data() + t * UC8179_TABLE_BYTES;
    EXPECT_EQ(memcmp(table, heads[t], 7), 0) << "table " << t;
    for (size_t i = 7; i < UC8179_TABLE_BYTES; ++i) EXPECT_EQ(table[i], 0x00) << "table " << t << " byte " << i;
  }
}

TEST(Uc8179ScanLut, SwapExchangesKwAndWkLevelsOnly) {
  for (const uint8_t frames : {static_cast<uint8_t>(1), static_cast<uint8_t>(10), static_cast<uint8_t>(255)}) {
    const Tables plain = built(frames, false);
    const Tables swapped = built(frames, true);
    EXPECT_EQ(plain[KW_LEVEL], VDL);
    EXPECT_EQ(plain[WK_LEVEL], VDH);
    EXPECT_EQ(swapped[KW_LEVEL], VDH);
    EXPECT_EQ(swapped[WK_LEVEL], VDL);
    for (size_t i = 0; i < UC8179_SCAN_TABLE_BYTES; ++i) {
      if (i == KW_LEVEL || i == WK_LEVEL) continue;
      EXPECT_EQ(swapped[i], plain[i]) << "frames " << +frames << " byte " << i;
    }
  }
}

TEST(Uc8179ScanLut, EveryExperimentFrameCountCarriesItsFrames) {
  for (const bool swap : {false, true}) {
    for (const uint8_t tp : FRAME_PERIOD_TPS) {
      const Tables tables = built(tp, swap);
      for (size_t t = 0; t < UC8179_TABLES; ++t) EXPECT_EQ(tables[t * UC8179_TABLE_BYTES + 2], tp) << "table " << t;
    }
    expectTables(built(POLARITY_TP, swap), expected(POLARITY_TP, swap), "polarity");
  }
}

TEST(Uc8179ScanLut, PrefixesAreTheSeamsRegisterOrder) {
  for (const bool swap : {false, true}) {
    const Tables tables = built(POLARITY_TP, swap);
    for (size_t t = 0; t < UC8179_TABLES; ++t) EXPECT_EQ(tables[t * UC8179_TABLE_BYTES], 0x20 + t) << "table " << t;
  }
}

TEST(Uc8179ScanLut, RateListIsThePllCodesInOrder) {
  EXPECT_EQ(UC8179_RATE_COUNT, 3u);
  EXPECT_EQ(UC8179_PLL_CODES[0], 0x06);  // the driver's 50 Hz, the code labEnd restores, so rate index 0 is stock
  EXPECT_EQ(UC8179_PLL_CODES[1], 0x05);  // the driver's PLL_40_HZ
  EXPECT_EQ(UC8179_PLL_CODES[2], 0x07);
  EXPECT_STREQ(uc8179RateLabel(0), "0x06");
  EXPECT_STREQ(uc8179RateLabel(1), "0x05");
  EXPECT_STREQ(uc8179RateLabel(2), "0x07");
  for (uint8_t i = 0; i < UC8179_RATE_COUNT; ++i) {
    char label[8];
    snprintf(label, sizeof(label), "0x%02X", static_cast<unsigned>(UC8179_PLL_CODES[i]));
    EXPECT_STREQ(uc8179RateLabel(i), label) << "rate " << +i;
    uint8_t code = 0;
    EXPECT_TRUE(uc8179PllCode(i, code)) << "rate " << +i;
    EXPECT_EQ(code, UC8179_PLL_CODES[i]) << "rate " << +i;
  }
  for (const uint8_t bad : {static_cast<uint8_t>(3), static_cast<uint8_t>(16), static_cast<uint8_t>(255)}) {
    EXPECT_EQ(uc8179RateLabel(bad), nullptr) << "rate " << +bad;
    uint8_t code = 0x5A;
    EXPECT_FALSE(uc8179PllCode(bad, code)) << "rate " << +bad;
    EXPECT_EQ(code, 0x5A) << "rate " << +bad;
  }
}

TEST(Uc8179ScanLut, RowLabelsNameEachRowsTableAndTheLevelItCarries) {
  auto levelName = [](const uint8_t select) {
    return (select & 0xC0) == VDL ? "VDL" : (select & 0xC0) == VDH ? "VDH" : "?";
  };
  for (const bool swap : {false, true}) {
    const Tables tables = built(POLARITY_TP, swap);
    EXPECT_EQ(std::string("KW ") + levelName(tables[KW_LEVEL]), uc8179RowLabel(1, swap)) << "swap " << swap;
    EXPECT_EQ(std::string("WK ") + levelName(tables[WK_LEVEL]), uc8179RowLabel(2, swap)) << "swap " << swap;
    EXPECT_EQ(uc8179RowLabel(0, swap), nullptr);
    EXPECT_EQ(uc8179RowLabel(3, swap), nullptr);
  }
  // The labels the polarity screen shows: "Row 1 (KW VDL)", "Row 2 (WK VDH)".
  EXPECT_STREQ(uc8179RowLabel(1, false), "KW VDL");
  EXPECT_STREQ(uc8179RowLabel(2, false), "WK VDH");
  EXPECT_STREQ(uc8179RowLabel(1, true), "KW VDH");
  EXPECT_STREQ(uc8179RowLabel(2, true), "WK VDL");
}

// Full-screen masks and the planes encoded from them.
struct Screen {
  std::vector<uint8_t> black = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> white = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> oldPlane = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> newPlane = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  MaskPlanes masks{black.data(), white.data(), WIDTH, HEIGHT};

  void encode() { encodeUc8179Planes(black.data(), white.data(), oldPlane.data(), newPlane.data(), oldPlane.size()); }
};

void expectEvery(const std::vector<uint8_t>& plane, const uint8_t value, const char* name) {
  for (size_t i = 0; i < plane.size(); ++i) ASSERT_EQ(plane[i], value) << name << " byte " << i;
}

TEST(Uc8179Planes, AllHoldIsWhiteInBothPlanes) {
  Screen screen;
  screen.masks.clear();
  screen.encode();
  expectEvery(screen.oldPlane, 0xFF, "old");  // (1, 1): WW
  expectEvery(screen.newPlane, 0xFF, "new");
}

TEST(Uc8179Planes, PolarityRowsSelectKwThenWk) {
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

TEST(Uc8179Planes, PlanesAreTheInvertedMasksPixelForPixel) {
  Screen screen;
  screen.masks.clear();
  screen.masks.setPixel(Mask::White, 0, 0);
  screen.masks.setPixel(Mask::Black, 0, 0);  // both masks: (0, 0), KK, which holds (level 00)
  screen.masks.setRect(Mask::White, 8, 0, 8, 1);
  screen.masks.setPixel(Mask::Black, 799, 479);
  screen.encode();
  EXPECT_EQ(screen.oldPlane[0], 0x7F);
  EXPECT_EQ(screen.newPlane[0], 0x7F);
  EXPECT_EQ(screen.oldPlane[1], 0x00);  // drive to white: old 0
  EXPECT_EQ(screen.newPlane[1], 0xFF);  //                  new 1
  EXPECT_EQ(screen.oldPlane[47999], 0xFF);
  EXPECT_EQ(screen.newPlane[47999], 0xFE);  // drive to black: old 1, new 0
  for (size_t i = 0; i < screen.oldPlane.size(); ++i) {
    ASSERT_EQ(screen.oldPlane[i], static_cast<uint8_t>(~screen.white[i])) << "byte " << i;
    ASSERT_EQ(screen.newPlane[i], static_cast<uint8_t>(~screen.black[i])) << "byte " << i;
  }
}

// FreeInkDisplay's lab API as freeink-sdk docs/display-lab.md and Uc8179Driver define it, recorded: it refuses an
// upload whose length is not 215 or whose prefixes are not 0x20-0x24, and a labScan before an upload since the last
// labEnd; labSetFrameRate never refuses on the UC8179. More permissive than the device: it never refuses for inversion
// (the lab wrapper and the facade do) and never times BUSY. The refuse* knobs force the shared session's other paths.
struct RecordingLab {
  enum class Call { Supported, Upload, Rate, Scan, End };

  bool supported = true;
  bool refuseUpload = false;
  bool refuseRate = false;  // never on a UC8179; exercises runUc8279Scan's path all the same
  bool refuseScan = false;
  uint32_t scanUs = 23456;

  mutable std::vector<Call> calls;  // labSupported() is const, as on FreeInkDisplay, and still recorded
  std::vector<uint8_t> uploaded;
  std::vector<uint8_t> pllCodes;
  const uint8_t* plane1 = nullptr;
  const uint8_t* plane2 = nullptr;
  bool loaded = false;

  bool labSupported() const {
    calls.push_back(Call::Supported);
    return supported;
  }
  bool labUploadWaveform(const uint8_t* bytes, const size_t len) {
    calls.push_back(Call::Upload);
    if (refuseUpload || bytes == nullptr || len != UC8179_SCAN_TABLE_BYTES) return false;
    for (size_t t = 0; t < UC8179_TABLES; ++t) {
      if (bytes[t * UC8179_TABLE_BYTES] != 0x20 + t) return false;
    }
    uploaded.assign(bytes, bytes + len);
    loaded = true;
    return true;
  }
  bool labSetFrameRate(const uint8_t code) {
    calls.push_back(Call::Rate);
    if (refuseRate) return false;
    pllCodes.push_back(code);  // written verbatim to PLL
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

const std::vector<Call> FULL_SESSION = {Call::Supported, Call::Upload, Call::Rate, Call::Scan, Call::End};

// ScanRequest's fields (src/activities/games/display_lab/ScanBackend.h), as runUc8179Request reads them.
struct Request {
  const uint8_t* blackMask;
  const uint8_t* whiteMask;
  uint8_t frames;
  uint8_t rate;
  bool swapPolarity;
};

ScanOutcome runOn(RecordingLab& lab, Screen& screen, const Request& request, Tables& tables, uint32_t& busyUs) {
  return runUc8179Request(lab, request, tables.data(), screen.oldPlane.data(), screen.newPlane.data(),
                          screen.oldPlane.size(), busyUs);
}

TEST(Uc8179Request, FakeRefusesAsTheSeamDocuments) {
  // The fake's own rules, so the request tests below lean on a faithful stand-in.
  RecordingLab lab;
  uint32_t busyUs = 1;
  const uint8_t plane[1] = {0xFF};
  EXPECT_FALSE(lab.labScan(plane, plane, busyUs));  // before any upload
  EXPECT_EQ(busyUs, 0u);
  Tables tables = built(1, false);
  EXPECT_FALSE(lab.labUploadWaveform(tables.data(), UC8179_SCAN_TABLE_BYTES - 1));
  EXPECT_FALSE(lab.labUploadWaveform(nullptr, UC8179_SCAN_TABLE_BYTES));
  tables[4 * UC8179_TABLE_BYTES] = 0x23;  // a wrong prefix, as labTablesValid refuses
  EXPECT_FALSE(lab.labUploadWaveform(tables.data(), UC8179_SCAN_TABLE_BYTES));
  tables = built(1, false);
  EXPECT_TRUE(lab.labSetFrameRate(0xFF));  // the UC8179 writes any code verbatim
  EXPECT_TRUE(lab.labUploadWaveform(tables.data(), UC8179_SCAN_TABLE_BYTES));
  EXPECT_TRUE(lab.labScan(plane, plane, busyUs));
  lab.labEnd();
  EXPECT_FALSE(lab.labScan(plane, plane, busyUs));  // after labEnd, until the next upload
}

TEST(Uc8179Request, AllHoldScansSendEachRatesPllAndTables) {
  // Every frame-period scan: all-hold masks, every FRAME_PERIOD_TPS against every rate.
  for (uint8_t rate = 0; rate < UC8179_RATE_COUNT; ++rate) {
    for (const uint8_t frames : FRAME_PERIOD_TPS) {
      SCOPED_TRACE(testing::Message() << "rate " << +rate << " frames " << +frames);
      Screen screen;
      screen.masks.clear();
      const Request request{screen.black.data(), screen.white.data(), frames, rate, false};
      RecordingLab lab;
      Tables tables = poisoned();
      uint32_t busyUs = 0;
      EXPECT_EQ(runOn(lab, screen, request, tables, busyUs), ScanOutcome::Done);
      EXPECT_EQ(lab.calls, FULL_SESSION);
      EXPECT_EQ(lab.pllCodes, std::vector<uint8_t>{UC8179_PLL_CODES[rate]});
      const Tables want = expected(frames, false);
      EXPECT_EQ(lab.uploaded, std::vector<uint8_t>(want.begin(), want.end()));
      EXPECT_EQ(lab.plane1, screen.oldPlane.data());  // DTM1 is the old plane, DTM2 the new
      EXPECT_EQ(lab.plane2, screen.newPlane.data());
      expectEvery(screen.oldPlane, 0xFF, "old");  // (1, 1): WW, hold
      expectEvery(screen.newPlane, 0xFF, "new");
      EXPECT_EQ(busyUs, lab.scanUs);
      EXPECT_FALSE(lab.loaded);  // labEnd ended the session
    }
  }
  // The rate codes in order, as the matrix lists them.
  const uint8_t codes[] = {0x06, 0x05, 0x07};
  for (uint8_t rate = 0; rate < UC8179_RATE_COUNT; ++rate) {
    Screen screen;
    screen.masks.clear();
    RecordingLab lab;
    Tables tables = poisoned();
    uint32_t busyUs = 0;
    ASSERT_EQ(runOn(lab, screen, {screen.black.data(), screen.white.data(), 1, rate, false}, tables, busyUs),
              ScanOutcome::Done);
    EXPECT_EQ(lab.pllCodes, std::vector<uint8_t>{codes[rate]}) << "rate " << +rate;
  }
}

TEST(Uc8179Request, PolarityRequestsSendTheirTablesPllAndPlanes) {
  // Row 1 (white mask): old 0x00, new 0xFF, KW. Row 2 (black mask): old 0xFF, new 0x00, WK.
  constexpr uint8_t OLD[POLARITY_ROWS] = {0x00, 0xFF};
  constexpr uint8_t NEW[POLARITY_ROWS] = {0xFF, 0x00};
  for (const bool swap : {false, true}) {
    for (size_t row = 0; row < POLARITY_ROWS; ++row) {
      SCOPED_TRACE(testing::Message() << "swap " << swap << " row " << row + 1);
      Screen screen;
      screen.masks.clear();
      screen.masks.fill(POLARITY_ROW_MASKS[row]);
      const Request request{screen.black.data(), screen.white.data(), POLARITY_TP, 0, swap};
      RecordingLab lab;
      Tables tables = poisoned();
      uint32_t busyUs = 0;
      EXPECT_EQ(runOn(lab, screen, request, tables, busyUs), ScanOutcome::Done);
      EXPECT_EQ(lab.calls, FULL_SESSION);
      const Tables want = expected(POLARITY_TP, swap);
      EXPECT_EQ(lab.uploaded, std::vector<uint8_t>(want.begin(), want.end()));
      EXPECT_EQ(lab.uploaded[KW_LEVEL], swap ? VDH : VDL);
      EXPECT_EQ(lab.uploaded[WK_LEVEL], swap ? VDL : VDH);
      EXPECT_EQ(lab.pllCodes, std::vector<uint8_t>{0x06});
      expectEvery(screen.oldPlane, OLD[row], "old");
      expectEvery(screen.newPlane, NEW[row], "new");
      EXPECT_EQ(busyUs, lab.scanUs);
    }
  }
}

TEST(Uc8179Request, BadRequestsMakeNoLabCallAndWriteNothing) {
  Screen screen;
  screen.masks.clear();
  const Request requests[] = {
      {nullptr, screen.white.data(), 10, 0, false},              // NoMasks
      {screen.black.data(), nullptr, 10, 0, false},              // NoMasks
      {nullptr, nullptr, 10, 3, false},                          // NoMasks first
      {screen.black.data(), screen.white.data(), 10, 3, false},  // BadRate
      {screen.black.data(), screen.white.data(), 255, 255, true},
  };
  const ScanOutcome outcomes[] = {ScanOutcome::NoMasks, ScanOutcome::NoMasks, ScanOutcome::NoMasks,
                                  ScanOutcome::BadRate, ScanOutcome::BadRate};
  for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
    SCOPED_TRACE(i);
    RecordingLab lab;
    Tables tables = poisoned();
    std::vector<uint8_t> oldPlane(screen.oldPlane.size(), 0xA5);
    std::vector<uint8_t> newPlane(screen.newPlane.size(), 0xA5);
    uint32_t busyUs = 999;
    EXPECT_EQ(
        runUc8179Request(lab, requests[i], tables.data(), oldPlane.data(), newPlane.data(), oldPlane.size(), busyUs),
        outcomes[i]);
    EXPECT_TRUE(lab.calls.empty());
    EXPECT_EQ(busyUs, 0u);
    for (const uint8_t b : tables) ASSERT_EQ(b, 0xA5);
    expectEvery(oldPlane, 0xA5, "old");
    expectEvery(newPlane, 0xA5, "new");
  }
}

TEST(Uc8179Request, EveryFrameCountIsSent) {
  // No TooManyFrames on the UC8179: the 8-bit TA takes 255.
  Screen screen;
  screen.masks.clear();
  RecordingLab lab;
  Tables tables = poisoned();
  uint32_t busyUs = 0;
  EXPECT_EQ(runOn(lab, screen, {screen.black.data(), screen.white.data(), 255, 2, false}, tables, busyUs),
            ScanOutcome::Done);
  EXPECT_EQ(lab.uploaded[2 * UC8179_TABLE_BYTES + 2], 255);
}

struct Refusal {
  const char* what;
  void (*arm)(RecordingLab&);
  ScanOutcome outcome;
  std::vector<Call> calls;
};

TEST(Uc8179Request, RefusalsEndTheSessionWithLabEndAndNoTime) {
  const Refusal refusals[] = {
      {"upload",
       [](RecordingLab& lab) { lab.refuseUpload = true; },
       ScanOutcome::UploadRefused,
       {Call::Supported, Call::Upload, Call::End}},
      {"rate",
       [](RecordingLab& lab) { lab.refuseRate = true; },
       ScanOutcome::RateRefused,
       {Call::Supported, Call::Upload, Call::Rate, Call::End}},
      {"scan", [](RecordingLab& lab) { lab.refuseScan = true; }, ScanOutcome::ScanRefused, FULL_SESSION},
  };
  for (const Refusal& refusal : refusals) {
    SCOPED_TRACE(refusal.what);
    Screen screen;
    screen.masks.clear();
    RecordingLab lab;
    refusal.arm(lab);
    Tables tables = poisoned();
    uint32_t busyUs = 999;
    EXPECT_EQ(runOn(lab, screen, {screen.black.data(), screen.white.data(), 10, 0, false}, tables, busyUs),
              refusal.outcome);
    EXPECT_EQ(lab.calls, refusal.calls);  // labEnd last, nothing after it
    EXPECT_EQ(busyUs, 0u);
    EXPECT_FALSE(lab.loaded);
  }
}

TEST(Uc8179Request, UnsupportedMakesNoOtherCall) {
  Screen screen;
  screen.masks.clear();
  RecordingLab lab;
  lab.supported = false;
  Tables tables = poisoned();
  uint32_t busyUs = 999;
  EXPECT_EQ(runOn(lab, screen, {screen.black.data(), screen.white.data(), 10, 0, false}, tables, busyUs),
            ScanOutcome::Unsupported);
  EXPECT_EQ(lab.calls, std::vector<Call>{Call::Supported});
  EXPECT_EQ(busyUs, 0u);
  EXPECT_EQ(scanOutcomeName(ScanOutcome::Unsupported), std::string("lab unsupported"));
}

TEST(Uc8179Request, EveryScanIsItsOwnSession) {
  // D8: the second scan uploads again; the fake refuses a labScan that follows a labEnd without one.
  Screen screen;
  screen.masks.clear();
  RecordingLab lab;
  Tables tables = poisoned();
  uint32_t busyUs = 0;
  EXPECT_EQ(runOn(lab, screen, {screen.black.data(), screen.white.data(), 1, 0, false}, tables, busyUs),
            ScanOutcome::Done);
  EXPECT_EQ(runOn(lab, screen, {screen.black.data(), screen.white.data(), 100, 2, true}, tables, busyUs),
            ScanOutcome::Done);
  std::vector<Call> twice = FULL_SESSION;
  twice.insert(twice.end(), FULL_SESSION.begin(), FULL_SESSION.end());
  EXPECT_EQ(lab.calls, twice);
  EXPECT_EQ(lab.pllCodes, (std::vector<uint8_t>{0x06, 0x07}));
  const Tables want = expected(100, true);
  EXPECT_EQ(lab.uploaded, std::vector<uint8_t>(want.begin(), want.end()));
}

}  // namespace
