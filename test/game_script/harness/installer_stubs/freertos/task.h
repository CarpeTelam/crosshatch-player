#pragma once

#include "FreeRTOS.h"

// The converter yields to the idle task between row batches; a host run has none.
inline void vTaskDelay(int) {}
