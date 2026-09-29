#pragma once

// HomeActivity.cpp includes <Bitmap.h> and uses nothing from it (the cover decoding is HomeCoverCache's,
// which is a double here); the real header needs the SD card's file types.
