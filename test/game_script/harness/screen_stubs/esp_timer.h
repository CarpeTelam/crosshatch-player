#pragma once

// esp_timer_get_time on the harness's clock (FakeRtos.h): GameClock::nowMs reads it.

#include <cstdint>

#include "FakeRtos.h"

inline int64_t esp_timer_get_time() { return static_cast<int64_t>(fakertos::millis()) * 1000; }
