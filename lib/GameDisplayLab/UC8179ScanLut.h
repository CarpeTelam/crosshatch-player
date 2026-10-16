#pragma once

#include <cstddef>
#include <cstdint>

// The UC8179 scan backend's pure parts (the display-lab epic, D1, D2, D3, D8, D9; story dlab-a2): the five 43-byte
// register-prefixed tables of one scan, its PLL code list, its row labels, and the two DTM planes from the two drive
// masks. Pure C++, pinned byte for byte by test/game_display_lab/test_uc8179_scan_lut.cpp; the facade calls live in
// UC8179ScanSession.h and src/activities/games/display_lab/UC8179ScanBackend.
//
// Table layout (freeink-sdk docs/display-lab.md, "UltraChip"; Uc8179Driver's labTablesValid): five tables in the order
// 0x20 VCOM, 0x21 WW, 0x22 KW, 0x23 WK, 0x24 KK, each its register prefix and 42 data bytes. Uc8179Driver.cpp ("UC8179
// datasheet R23h: each group is [rail selectors, four frame counts, repeat count]"): data byte 0 selects the four
// phases' levels, phase A in bits 7-6; bytes 1-4 are the 8-bit frame counts TA-TD; byte 5 is the repeat count, 0x01 in
// every stock group (kGrayLuts, kGrayPreBwMid). A scan's table is one group of one phase, `[L << 6, frames, 0, 0, 0,
// 0x01]`, and 36 zero bytes. Levels: 00 (GND on WW and KK, VCOM DC on the VCOM table), 01 VDH, 10 VDL (kDarkGrayLut's
// "VDL phase" carries level bits 10). Whether the controller runs a group RP or RP + 1 times is not documented in the
// tree; the frame-period slope shows it.
namespace GameDisplayLab {

constexpr size_t UC8179_TABLES = 5;
constexpr size_t UC8179_TABLE_BYTES = 43;
constexpr size_t UC8179_SCAN_TABLE_BYTES = UC8179_TABLES * UC8179_TABLE_BYTES;  // 215, the length labTablesValid takes

// The PLL (0x30) codes the frame-rate list sends, in order. The tree documents two UC8179 codes, PLL_40_HZ 0x05 and
// PLL_50_HZ 0x06 (Uc8179Driver.cpp; freeink-sdk docs/display-lab.md: "UC8179 PLL 0x06 is the 50 Hz rate"), and no full
// table, so these bracket the driver's 0x06, the stock code first so rate index 0 (the polarity screen's) is stock, as
// on the UC8279 list. 0x05 is the documented 40 Hz; 0x07 is undocumented.
inline constexpr uint8_t UC8179_PLL_CODES[] = {0x06, 0x05, 0x07};
constexpr uint8_t UC8179_RATE_COUNT = static_cast<uint8_t>(sizeof(UC8179_PLL_CODES));

// The rate's label ("0x06", "0x05", "0x07"), or nullptr when `index` is not below UC8179_RATE_COUNT.
const char* uc8179RateLabel(uint8_t index);

// The PLL code of rate `index` into `code`; false, with `code` untouched, when `index` is not below UC8179_RATE_COUNT.
bool uc8179PllCode(uint8_t index, uint8_t& code);

// The name of drive row `row` (1: the white mask's, KW; 2: the black mask's, WK) and its level: "KW VDL", "WK VDH", or
// "KW VDH", "WK VDL" when `swap`; nullptr for any other row. Unswapped, KW (drive to white) gets VDL and WK (drive to
// black) VDH, the direction of the stock pre-BW conditioning tables (kGrayPreBwMid KW 0xAA, WK 0x55) and of the UC8279
// backend; which table darkens on this panel is what the polarity screen confirms on hardware.
const char* uc8179RowLabel(uint8_t row, bool swap);

// Writes the five tables (UC8179_SCAN_TABLE_BYTES) for `frames` frames into `out`: each `[prefix, L << 6, frames, 0, 0,
// 0, 0x01]` and zeros. D9: VCOM, WW and KK carry the same frames at level 00 (GND on WW and KK, VCOM DC on the VCOM
// table); KW carries VDL and WK VDH, exchanged when `swap`. Every `frames` fits the 8-bit count (D7 is UC8279-only), so
// nothing is refused; frames 0 gives TA 0, as the UC8279 builder accepts it (no caller sends it).
void buildUc8179ScanTables(uint8_t* out, uint8_t frames, bool swap);

// The two DTM planes for one scan, `bytes` each (D2): old = ~white, new = ~black. Under the lab's CDI (`cdiActive`, the
// AA activation's), Uc8179Driver's plane select (old = DTM1, new = DTM2) is (0, 1) KW, (1, 0) WK, (1, 1) WW, (0, 0) KK.
// So a pixel in neither mask is (1, 1), WW, which holds; one in the white mask (0, 1), KW, drives to white; one in the
// black mask (1, 0), WK, drives to black; one in both (0, 0), KK, which holds (level 00) too.
void encodeUc8179Planes(const uint8_t* black, const uint8_t* white, uint8_t* oldPlane, uint8_t* newPlane, size_t bytes);

}  // namespace GameDisplayLab
