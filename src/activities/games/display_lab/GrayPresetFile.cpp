#include "GrayPresetFile.h"

#if CROSSHATCH_DISPLAY_LAB

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <string>

namespace GrayPresetFile {

namespace {

// The encode buffer: the encoded preset is about 260 bytes, so 512 leaves room for every value at its widest.
constexpr size_t ENCODE_BYTES = 512;

}  // namespace

GameDisplayLab::GrayPresetLoad load(GameDisplayLab::GrayPreset& preset) {
  using GameDisplayLab::GrayPresetLoad;
  if (!Storage.exists(PATH)) {
    LOG_INF("DLAB", "no preset at %s: InkBoy's defaults", PATH);
    return GameDisplayLab::loadGrayPreset(nullptr, 0, preset);
  }
  // Cold path, once per lab session: a std::string sized to the file (readFileToString caps it at MAX_BYTES).
  std::string text;
  if (!Storage.readFileToString("DLAB", PATH, MAX_BYTES, text)) {
    LOG_ERR("DLAB", "preset %s unreadable (empty, over %u bytes, or a read error): InkBoy's defaults", PATH,
            static_cast<unsigned>(MAX_BYTES));
    GameDisplayLab::loadGrayPreset(nullptr, 0, preset);
    return GrayPresetLoad::Malformed;
  }
  const GrayPresetLoad result = GameDisplayLab::loadGrayPreset(text.data(), text.size(), preset);
  switch (result) {
    case GrayPresetLoad::Loaded:
      LOG_INF("DLAB", "preset loaded from %s", PATH);
      break;
    case GrayPresetLoad::Malformed:
      LOG_ERR("DLAB", "preset %s malformed: InkBoy's defaults", PATH);
      break;
    case GrayPresetLoad::Missing:
      break;  // text is never null here
  }
  return result;
}

bool save(const GameDisplayLab::GrayPreset& preset) {
  // On the heap, once per save (never per slot): a 512-byte local would break the 256-byte stack rule.
  auto buffer = makeUniqueNoThrow<char[]>(ENCODE_BYTES);
  if (!buffer) {
    LOG_ERR("DLAB", "OOM: %u byte preset buffer", static_cast<unsigned>(ENCODE_BYTES));
    return false;
  }
  const size_t length = GameDisplayLab::encodeGrayPreset(preset, buffer.get(), ENCODE_BYTES);
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
  LOG_INF("DLAB", "preset saved to %s (%u bytes)", PATH, static_cast<unsigned>(length));
  return true;
}

}  // namespace GrayPresetFile

#endif  // CROSSHATCH_DISPLAY_LAB
