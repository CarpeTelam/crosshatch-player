#pragma once

// millis() on the harness's clock (FakeRtos.h): the match reads it on the loop task and
// GameVM's clock (esp_timer.h) reads the same one.

#include "FakeRtos.h"

inline unsigned long millis() { return static_cast<unsigned long>(fakertos::millis()); }
