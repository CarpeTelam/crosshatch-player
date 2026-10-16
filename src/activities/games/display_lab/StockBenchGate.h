#pragma once

#include "ScanBackend.h"  // CROSSHATCH_DISPLAY_LAB, the gate every display_lab file sits behind

#if CROSSHATCH_DISPLAY_LAB

#include <HalDisplay.h>

// Which lab an X4 Pro gets (story dlab-u1). An UltraChip controller (UC8179 or UC8279) with no scan backend gets the
// UltraChip stock-path bench (Transition timing, Gray ramp) instead of the scan screens; a controller with a scan
// backend always gets the scan screens, so an UltraChip that later gains a backend leaves the stock bench (which of
// the two it should show then is the backend lanes' call).

// True when `controller` gets the stock-path bench: UC8179 or UC8279, and no scan backend.
bool usesStockBench(HalDisplay::Controller controller);

// The launcher's Display lab row subtitle: "UltraChip stock-path bench" on an X4 Pro whose controller uses the stock
// bench, else the scan bench's "Panel waveform bench".
const char* labRowSubtitle();

#endif  // CROSSHATCH_DISPLAY_LAB
