#pragma once

// The real HalMemory (lib/hal/HalMemory.h has no platform includes); HalMemoryStub.cpp
// defines allocatePsram and PsramDeleter over the heap, and HalMemoryStub.h steers it.
#include <hal/HalMemory.h>
