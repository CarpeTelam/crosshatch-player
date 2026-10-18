// JPEGDEC built for the host tests as the device builds it (jpegdec_as_device.h says how): its sources with
// __x86_64__ hidden, after the system headers they include.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef __x86_64__
#define ALLOWS_UNALIGNED
#include "JPEGDEC.cpp"

static_assert(REGISTER_WIDTH == 32, "JPEGDEC's bit reader is the device's 32 bits wide");
