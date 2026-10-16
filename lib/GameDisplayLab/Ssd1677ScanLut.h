#pragma once

#include <cstddef>
#include <cstdint>

// The SSD1677 one-frame scan waveform the display lab drives the X4 Pro's panel with (the spike plan
// `plan-spike-ssd1677-grayscale-calibration.md`, Part A). Pure C++: no Arduino or SDK include, so the host suite
// (test/game_display_lab) pins every byte.
//
// The SDK's 112-byte layout (freeink-sdk `Ssd1677Luts.h`): bytes 0-49 are five LUT rows of 10 bytes, each byte the
// four 2-bit phases A-D (A in the top two bits) of one of ten groups; 50-99 are the ten groups' TP A, B, C, D and RP;
// 100-104 the frame-rate bytes; 105-109 the voltages VGH, VSH1, VSH2, VSL, VCOM; 110-111 reserved. A pixel selects its
// row by its two plane bits (RED bit, BW bit): 00 LUT0, 01 LUT1, 10 LUT2, 11 LUT3; LUT4 is VCOM.
//
// A scan LUT has one phase: group 0's phase A, TP[0A] frames long, on rows 1 and 2 only. LUT0 (both bits clear) is all
// VSS, so a pixel whose bits are clear holds; LUT3 and LUT4 are all VSS too. Everything else is zero, and the voltage
// tail is the panel's stock values (STOCK_VOLTAGES): the lab never changes a voltage.
namespace GameDisplayLab {

// The source voltage code of one phase.
enum class VsCode : uint8_t { Vss = 0, Vsh1 = 1, Vsl = 2, Vsh2 = 3 };

// The LUT rows a scan can select per pixel, by its plane bits.
enum class LutRow : uint8_t { Hold, Row1, Row2 };

// The byte every pixel of each plane is set to so the whole panel selects one row: the BW plane (RAM 0x24, the low
// bit) and the RED plane (RAM 0x26, the high bit), as the facade's copyGrayscaleBuffers(lsb, msb) writes them raw.
struct PlaneFill {
  uint8_t bw;
  uint8_t red;
};

constexpr size_t LUT_BYTES = 112;
constexpr size_t ROW_BYTES = 10;
constexpr size_t TP_BASE = 50;  // group 0's TP A
constexpr size_t FRAME_RATE_BASE = 100;
constexpr size_t FRAME_RATE_BYTES = 5;
constexpr size_t VOLTAGE_BASE = 105;
constexpr size_t VOLTAGE_BYTES = 5;
// VGH, VSH1, VSH2, VSL, VCOM: the X4's stock grayscale LUT tail (freeink-sdk `lut_grayscale`).
inline constexpr uint8_t STOCK_VOLTAGES[VOLTAGE_BYTES] = {0x17, 0x41, 0xA8, 0x32, 0x30};

// The frame-period experiment: every TP[0A] in FRAME_PERIOD_TPS against every frame-rate byte in FRAME_PERIOD_RATES,
// in that nesting order (tp outer), all-hold masks. 0x22 is the frame-rate byte Waveshare's 4-gray LUT uses on this
// panel; 0x11 and 0x44 bracket it. The byte's encoding is undocumented, so the three are an experiment, not a claim.
inline constexpr uint8_t FRAME_PERIOD_TPS[] = {1, 10, 100};
inline constexpr uint8_t FRAME_PERIOD_RATES[] = {0x22, 0x11, 0x44};
constexpr size_t FRAME_PERIOD_SCANS = 9;
static_assert(sizeof(FRAME_PERIOD_TPS) * sizeof(FRAME_PERIOD_RATES) == FRAME_PERIOD_SCANS);

// Scan i of the frame-period experiment (0 <= i < FRAME_PERIOD_SCANS): tp outer, rate inner.
struct ScanParams {
  uint8_t tp;
  uint8_t rate;
};
constexpr ScanParams framePeriodScan(const size_t i) {
  constexpr size_t rates = sizeof(FRAME_PERIOD_RATES);
  return {FRAME_PERIOD_TPS[i / rates], FRAME_PERIOD_RATES[i % rates]};
}

// The polarity experiment drives the panel for this many frames.
constexpr uint8_t POLARITY_TP = 10;
// InkBoy's SSD1681 convention, the default the polarity screen tries first: VSL on the BW-plane row (row 1) and VSH1
// on the RED-plane row (row 2). The panel says which one darkens.
constexpr VsCode POLARITY_ROW1 = VsCode::Vsl;
constexpr VsCode POLARITY_ROW2 = VsCode::Vsh1;

// Writes the scan LUT into out[LUT_BYTES]: row 1's and row 2's phase A of group 0 as given, TP[0A] = tp, every
// frame-rate byte = frameRateByte, the stock voltage tail, and zero everywhere else.
void buildOneFrameLut(uint8_t* out, uint8_t tp, uint8_t frameRateByte, VsCode row1, VsCode row2);

// The plane bytes that make every pixel select `row`.
PlaneFill planeFillFor(LutRow row);

// The name of a voltage code for a log line ("VSS", "VSH1", "VSL", "VSH2").
const char* vsName(VsCode code);

}  // namespace GameDisplayLab
