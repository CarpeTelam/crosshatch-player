#include "GrayPresetFile.h"

#if CROSSHATCH_DISPLAY_LAB

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <string>

namespace GrayPresetFile {

namespace {

// The encode buffer: the encoded preset is about 340 bytes, so 512 leaves room for every value at its widest.
constexpr size_t ENCODE_BYTES = 512;

}  // namespace

GameDisplayLab::GrayPresetLoad load(const GameDisplayLab::PresetBase base, GameDisplayLab::GrayPreset& preset,
                                    GameDisplayLab::PresetBase& fileBase) {
  using GameDisplayLab::GrayPresetLoad;
  fileBase = base;
  const GameDisplayLab::GrayPreset defaults = GameDisplayLab::presetDefaults(base);
  const char* defaultsName = GameDisplayLab::presetBaseName(base);
  if (!Storage.exists(PATH)) {
    LOG_INF("DLAB", "no preset at %s: %s defaults", PATH, defaultsName);
    return GameDisplayLab::loadGrayPreset(nullptr, 0, defaults, preset);
  }
  // Cold path, once per lab session: a std::string sized to the file (readFileToString caps it at MAX_BYTES).
  std::string text;
  if (!Storage.readFileToString("DLAB", PATH, MAX_BYTES, text)) {
    LOG_ERR("DLAB", "preset %s unreadable (empty, over %u bytes, or a read error): %s defaults", PATH,
            static_cast<unsigned>(MAX_BYTES), defaultsName);
    GameDisplayLab::loadGrayPreset(nullptr, 0, defaults, preset);
    return GrayPresetLoad::Malformed;
  }
  GameDisplayLab::GrayPresetLoadInfo info;
  const GrayPresetLoad result = GameDisplayLab::loadGrayPreset(text.data(), text.size(), defaults, preset, &info);
  switch (result) {
    case GrayPresetLoad::Loaded:
      LOG_INF("DLAB", "preset loaded from %s over %s defaults", PATH, defaultsName);
      // Saved over another controller's defaults: its doses were tuned on another panel. Loaded all the same.
      // The session keeps the file's base, so Save writes it back and this warning stays until the doses are retuned
      // (Tuning's Reset to defaults takes the panel's).
      if (info.base) fileBase = *info.base;
      if (info.base && *info.base != base) {
        LOG_ERR("DLAB", "preset %s was saved over %s defaults, this panel's are %s: loaded anyway", PATH,
                GameDisplayLab::presetBaseName(*info.base), defaultsName);
      }
      if (info.unknownBase) LOG_ERR("DLAB", "preset %s: its base names no known defaults; ignored", PATH);
      // A file saved before the base key (dlab-c1, dlab-c2) holds a whole dose table, most likely an SSD1677 one.
      if (!info.base && base == GameDisplayLab::PresetBase::UltraChip) {
        LOG_ERR("DLAB", "preset %s predates the base key: its doses override the UltraChip defaults", PATH);
      }
      // The file predates the retarget split (dlab-c3): its one value went to each direction it did not set.
      if (info.legacyDark) {
        LOG_INF("DLAB", "tune retarget_percent read as retarget to dark=%u%%",
                static_cast<unsigned>(preset.retargetDarkPercent));
      }
      if (info.legacyLight) {
        LOG_INF("DLAB", "tune retarget_percent read as retarget to light=%u%%",
                static_cast<unsigned>(preset.retargetLightPercent));
      }
      break;
    case GrayPresetLoad::Malformed:
      LOG_ERR("DLAB", "preset %s malformed: %s defaults", PATH, defaultsName);
      break;
    case GrayPresetLoad::Missing:
      break;  // text is never null here
  }
  return result;
}

bool save(const GameDisplayLab::GrayPreset& preset, const GameDisplayLab::PresetBase base) {
  // On the heap, once per save (never per slot): a 512-byte local would break the 256-byte stack rule.
  auto buffer = makeUniqueNoThrow<char[]>(ENCODE_BYTES);
  if (!buffer) {
    LOG_ERR("DLAB", "OOM: %u byte preset buffer", static_cast<unsigned>(ENCODE_BYTES));
    return false;
  }
  const size_t length = GameDisplayLab::encodeGrayPreset(preset, base, buffer.get(), ENCODE_BYTES);
  if (length == 0) {
    LOG_ERR("DLAB", "preset does not fit %u bytes", static_cast<unsigned>(ENCODE_BYTES));
    return false;
  }
  if (!Storage.ensureDirectoryExists(DIR)) {
    LOG_ERR("DLAB", "cannot create %s", DIR);
    return false;
  }
  // The new text goes to TMP_PATH; PATH is replaced only once it is whole, so a failed write keeps the saved table.
  HalFile file;
  if (!Storage.openFileForWrite("DLAB", TMP_PATH, file)) {
    LOG_ERR("DLAB", "cannot open %s for writing", TMP_PATH);
    return false;
  }
  const size_t written = file.write(buffer.get(), length);
  const bool closed = file.close();
  if (written != length || !closed) {
    LOG_ERR("DLAB", "preset write to %s failed (%u of %u bytes)", TMP_PATH, static_cast<unsigned>(written),
            static_cast<unsigned>(length));
    return false;
  }
  if (!Storage.replaceFile(TMP_PATH, PATH)) {
    LOG_ERR("DLAB", "cannot move %s over %s", TMP_PATH, PATH);
    return false;
  }
  LOG_INF("DLAB", "preset saved to %s (%u bytes, %s base)", PATH, static_cast<unsigned>(length),
          GameDisplayLab::presetBaseName(base));
  return true;
}

}  // namespace GrayPresetFile

#endif  // CROSSHATCH_DISPLAY_LAB
