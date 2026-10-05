#pragma once

// BoardConfig.h's gpio_hold_dis / gpio_hold_en and gpio_num_t, as no-ops for BoardInsetsTest.cpp (see ../Arduino.h).

typedef int gpio_num_t;
inline void gpio_hold_dis(gpio_num_t) {}
inline void gpio_hold_en(gpio_num_t) {}
