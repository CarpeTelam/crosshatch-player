#pragma once

#include "ScanBackend.h"

#if CROSSHATCH_DISPLAY_LAB

#include <HalDisplay.h>

#include <memory>

class GfxRenderer;

// The scan backend of each panel controller (the display-lab epic, R1): both functions read one switch in the .cpp.

// Whether `controller` has a scan backend; the launcher's Display lab row shows only where it does.
bool hasScanBackend(HalDisplay::Controller controller);

// A new backend for `controller`; null when it has none or the allocation failed. Check available() too: a backend
// whose buffers did not fit refuses every scan.
std::unique_ptr<ScanBackend> makeScanBackend(HalDisplay::Controller controller, GfxRenderer& renderer);

#endif  // CROSSHATCH_DISPLAY_LAB
