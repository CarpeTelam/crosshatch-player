#pragma once

// FreeRTOS's base types and constants for the host (see ../FakeRtos.h).

#include <cstdint>

using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned int;
using StackType_t = uint8_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFFu
#define tskNO_AFFINITY 0x7FFFFFFF
// One tick is one millisecond.
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define portTICK_PERIOD_MS 1
