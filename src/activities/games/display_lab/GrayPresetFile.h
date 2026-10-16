#pragma once

#include "ScanBackend.h"  // CROSSHATCH_DISPLAY_LAB, the gate every display_lab file sits behind

#if CROSSHATCH_DISPLAY_LAB

#include <GrayPreset.h>

// The gray engine's preset on the SD card (the display-lab epic, R4): /.crosshatch/display-lab.json, the text
// GameDisplayLab::encodeGrayPreset writes and loadGrayPreset reads (docs/crosshatch/display-lab.md), through Storage.
namespace GrayPresetFile {

constexpr const char* DIR = "/.crosshatch";
constexpr const char* PATH = "/.crosshatch/display-lab.json";
// save() writes here first and moves it over PATH only after a full write, so a failed save keeps the old file.
constexpr const char* TMP_PATH = "/.crosshatch/display-lab.json.tmp";
// The longest file load() reads; the encoded preset is about 260 bytes.
constexpr size_t MAX_BYTES = 4096;

// Reads the file into `preset`: Loaded, or InkBoy's defaults when it is Missing (LOG_INF) or Malformed, which covers
// a file that exists but cannot be read (LOG_ERR).
GameDisplayLab::GrayPresetLoad load(GameDisplayLab::GrayPreset& preset);
// Writes `preset` to the file; false, having logged why, when anything failed (the caller keeps its values).
bool save(const GameDisplayLab::GrayPreset& preset);

}  // namespace GrayPresetFile

#endif  // CROSSHATCH_DISPLAY_LAB
