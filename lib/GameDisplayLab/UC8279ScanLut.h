#pragma once

#include <cstddef>
#include <cstdint>

// The UC8279 (X4 Pro) scan backend's pure parts (the display-lab epic, D1, D2, D7 to D9): the five 43-byte
// register-prefixed tables of one scan, its PLL code list, its row labels, and the two DTM planes from the two drive
// masks. Pure C++, pinned byte for byte by test/game_display_lab/test_uc8279_scan_lut.cpp; the facade calls live in
// UC8279ScanSession.h and src/activities/games/display_lab/UC8279ScanBackend.
//
// Table layout (freeink-sdk docs/display-lab.md, "UltraChip"; Uc8279X4Driver's labTablesValid): five tables in the
// order 0x20 VCOM, 0x21 WW, 0x22 KW, 0x23 WK, 0x24 KK, each its register prefix and 42 data bytes, which the driver
// pads to the 49-byte LUT register. The data is one 7-byte group `[0x01, P1, P2, P3, P4, 0x01, 0x01]` and 35 zero
// bytes, as Uc8279X4Driver's makeQualityBank writes groups and as every stock group of kXtfPreBwMid and
// kUc8279X3_BwGc reads; a phase byte is `level << 6 | frames` with a 6-bit frame count, level 0 GND, 1 VSH, 2 VSL.
namespace GameDisplayLab {

constexpr size_t UC8279_TABLES = 5;
constexpr size_t UC8279_TABLE_BYTES = 43;
constexpr size_t UC8279_SCAN_TABLE_BYTES = UC8279_TABLES * UC8279_TABLE_BYTES;  // 215, the length labTablesValid takes
// D7: a phase holds at most 63 frames and the group has four phases at one level.
constexpr uint8_t UC8279_PHASE_MAX_FRAMES = 63;
constexpr uint8_t UC8279_PHASES = 4;
constexpr uint8_t UC8279_MAX_FRAMES = UC8279_PHASE_MAX_FRAMES * UC8279_PHASES;  // 252

// The PLL (0x30) codes the frame-rate list sends, in order. No UC8279 PLL code table is documented in the tree, the SDK
// or the research run, so these bracket the driver's stock `pll = 0x0E` (uc8279X4DefaultConfig), the stock code first
// so rate index 0 (the polarity screen's) is stock, as on the SSD1677 list; 0x0F is also Ink-boy's and the X3 init's.
inline constexpr uint8_t UC8279_PLL_CODES[] = {0x0E, 0x0D, 0x0F};
constexpr uint8_t UC8279_RATE_COUNT = static_cast<uint8_t>(sizeof(UC8279_PLL_CODES));

// The rate's label ("0x0E", "0x0D", "0x0F"), or nullptr when `index` is not below UC8279_RATE_COUNT.
const char* uc8279RateLabel(uint8_t index);

// The PLL code of rate `index` into `code`; false, with `code` untouched, when `index` is not below UC8279_RATE_COUNT.
bool uc8279PllCode(uint8_t index, uint8_t& code);

// The name of drive row `row` (1: the white mask's, KW; 2: the black mask's, WK) and its level: "KW VSL", "WK VSH", or
// "KW VSH", "WK VSL" when `swap`; nullptr for any other row. Unswapped, KW (drive to white) gets VSL and WK (drive to
// black) VSH, the direction of the stock GC tables (kUc8279X3_BwGc KW 0x9A, WK 0x5A) and the pre-BW tables. The names
// follow the UltraChip level-select convention (01 VDH/VSH, 10 VDL/VSL; makeQualityBank treats levels 1 and 2 as
// opposite rails); which table darkens on this panel is what the polarity screen confirms on hardware.
const char* uc8279RowLabel(uint8_t row, bool swap);

// Writes the five tables (UC8279_SCAN_TABLE_BYTES) for `frames` frames into `out`. D7: the frames fill the group's
// four phases 63 at a time in order (100 = 63 + 37), unused phases 0x00. D9: VCOM, WW and KK carry the same phases at
// GND; KW carries VSL and WK VSH, exchanged when `swap`. Frames 0 gives all four phase bytes 0x00. False, with `out`
// untouched, when `frames` is over UC8279_MAX_FRAMES.
bool buildUc8279ScanTables(uint8_t* out, uint8_t frames, bool swap);

// The two DTM planes for one scan, `bytes` each (D2; framebuffer bit 1 = white, and the lab's CDI equals the B/W
// refresh's, so plane bits mean what they do in a B/W refresh): old = ~white, new = ~black. A pixel in neither mask is
// (1, 1), WW, which holds; one in the white mask (0, 1), KW, drives to white; one in the black mask (1, 0), WK, drives
// to black; one in both (0, 0), KK, which holds at GND too. The driver's quality bank (kQualityLutReg, 0x22 and 0x23
// exchanged) streams inverted gray planes, not B/W old/new planes, so it is no direct evidence for this selection; the
// polarity screen is what confirms on hardware which table darkens.
void encodeUc8279Planes(const uint8_t* black, const uint8_t* white, uint8_t* oldPlane, uint8_t* newPlane, size_t bytes);

}  // namespace GameDisplayLab
