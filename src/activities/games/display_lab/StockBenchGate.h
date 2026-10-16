#pragma once

#include "ScanBackend.h"  // CROSSHATCH_DISPLAY_LAB, the gate every display_lab file sits behind

#if CROSSHATCH_DISPLAY_LAB

#include <HalDisplay.h>
#include <StockBench.h>

// Which lab an X4 Pro gets (story dlab-u1; the display-lab epic, D10). A controller with a scan backend gets the scan
// screens. An UltraChip controller (UC8179 or UC8279) with a scan backend also gets the stock-path bench's two rows
// (Transition timing, Gray ramp) before Exit on the scan menu, so the stock transition and the OEM ramp stay there to
// compare against the lab; an SSD1677 does not. An UltraChip controller with no scan backend gets the stock-path bench
// alone (Transition timing, Gray ramp, Exit). Any other controller gets no lab.

// The menu `controller` gets: GameDisplayLab::labMenuFor over (UC8179 or UC8279, hasScanBackend).
GameDisplayLab::LabMenu labMenuFor(HalDisplay::Controller controller);

// True when `controller` gets the stock-path bench alone: UC8179 or UC8279, and no scan backend.
bool usesStockBench(HalDisplay::Controller controller);

// The launcher's Display lab row subtitle: "UltraChip stock-path bench" on an X4 Pro whose controller uses the stock
// bench alone, else the scan bench's "Panel waveform bench".
const char* labRowSubtitle();

#endif  // CROSSHATCH_DISPLAY_LAB
