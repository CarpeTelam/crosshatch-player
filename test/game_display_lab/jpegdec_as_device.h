#pragma once

// JPEGDEC's header as the device sees it, for the host tests (test_image_decoders.cpp, jpegdec_as_device.cpp): on the
// ESP32-S3 its bit reader is 32 bits wide (REGISTER_WIDTH 32: __x86_64__, __aarch64__ and __arm64__ pick 64) and it
// reads unaligned words (ALLOWS_UNALIGNED, from the core's HAL_ESP32_HAL_H_). The host build hides __x86_64__ while
// the header is read, which also leaves out its SSE paths, so the host runs the device's scalar code. The system
// headers it includes are included first, with the host's macros. The S3's own SIMD (ESP32S3_SIMD) is device-only.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma push_macro("__x86_64__")
#undef __x86_64__
#ifndef ALLOWS_UNALIGNED
#define ALLOWS_UNALIGNED
#endif
#include <JPEGDEC.h>
#pragma pop_macro("__x86_64__")

static_assert(REGISTER_WIDTH == 32, "JPEGDEC's bit reader is the device's 32 bits wide");
