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
// The longest file load() reads; a version 2 preset is about 700 bytes.
constexpr size_t MAX_BYTES = 4096;

// Reads the file into `preset` over `base`'s defaults (absent keys keep them; a version 1 file is expanded to seven
// levels and logged): Loaded, or those defaults when it is
// Missing (LOG_INF) or Malformed, which covers a file that exists but cannot be read (LOG_ERR); the log names the
// defaults. A file with the older `retarget_percent` key logs which retarget directions it filled. A file whose `base`
// key names other defaults than `base` (tuned on another controller) logs an error naming both and loads anyway; one
// without the key (saved before it existed) on an UltraChip panel logs an error that its doses override the UltraChip
// defaults; a `base` naming no defaults is logged and ignored. `fileBase` is the file's base when it was loaded with
// one, else `base`: the base the session's values carry, which Save writes back.
// `*expandedVersion1`, when given, says whether a version 1 file was loaded (the lab menu says so until Save or Reset).
GameDisplayLab::GrayPresetLoad load(GameDisplayLab::PresetBase base, GameDisplayLab::GrayPreset& preset,
                                    GameDisplayLab::PresetBase& fileBase, bool* expandedVersion1 = nullptr);
// Writes `preset`, with `base` (the defaults the session's values carry) as its `base` key, to the file; false, having
// logged why, when anything failed (the caller keeps its values). The log names the base written.
bool save(const GameDisplayLab::GrayPreset& preset, GameDisplayLab::PresetBase base);

}  // namespace GrayPresetFile

#endif  // CROSSHATCH_DISPLAY_LAB
