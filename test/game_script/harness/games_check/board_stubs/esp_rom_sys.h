#pragma once

// BoardConfig.h's esp_rom_printf, as a no-op for BoardInsetsTest.cpp (see Arduino.h).

inline int esp_rom_printf(const char*, ...) { return 0; }
