#pragma once

// The game API level this host implements (spine AD-19).
//
// API_LEVEL: the newest level; ch.api, ctx.api, and HostCaps::api report it.
// API_MIN_LEVEL: the oldest level whose games still run unchanged.
// API_LEVEL_FROZEN: whether API_LEVEL is frozen; every level below it is. Once
//   true it never reverts (a fork CI job enforces it).
// API_SURFACE_CRC: CRC-32 of the entry lines of docs/crosshatch/api-level-<n>.txt
//   for n from API_MIN_LEVEL to API_LEVEL (the list's header defines the bytes);
//   test/game_core/ApiLevelTest.cpp recomputes it, so change it with the list.
//
// Fork scripts read these values with a regular expression: keep each define on
// one line as `#define NAME VALUE`, with a decimal integer, true or false, or an
// 8-digit hex CRC.

#define API_LEVEL 1
#define API_MIN_LEVEL 1
#define API_LEVEL_FROZEN false
#define API_SURFACE_CRC 0xAEB225B0

static_assert(API_MIN_LEVEL >= 1 && API_MIN_LEVEL <= API_LEVEL, "API_MIN_LEVEL must be in 1..API_LEVEL");
