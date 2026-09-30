#pragma once

// millis() on the harness's clock (FakeRtos.h): the match reads it on the loop task and
// GameVM's clock (esp_timer.h) reads the same one.

#include <chrono>
#include <thread>

#include "FakeRtos.h"

inline unsigned long millis() { return static_cast<unsigned long>(fakertos::millis()); }

// A blocking delay, as vTaskDelay: it sleeps, and the fake clock moves by it.
inline void delay(const unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  fakertos::advance(ms);
}
