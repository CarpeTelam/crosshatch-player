#include <DiagLogStore.h>

#if CROSSHATCH_SD_LOG

#include <HalStorage.h>

#include <vector>

// The diagnostics log's file store over Storage / HalFile (never SdFat directly). Every call is one whole operation and
// checks ready() first, because the card is unmounted for sleep and for the USB drive. Nothing here logs.

namespace {

constexpr int MAX_DIRECTORY_FILES = 200;

class StorageFileStore final : public DiagLog::FileStore {
 public:
  bool ready() override { return Storage.ready(); }
  bool makeDirectory(const char* path) override { return Storage.ready() && Storage.ensureDirectoryExists(path); }
  bool listFiles(const char* directory, NameSink& sink) override {
    // Storage.listFiles answers an empty vector for a folder it cannot open, the same as for an empty one, and an
    // empty answer would number this boot 1 and appends its header to an older boot's file: so a missing folder is a
    // failure here.
    if (!Storage.ready() || !Storage.exists(directory)) return false;
    const std::vector<String> names = Storage.listFiles(directory, MAX_DIRECTORY_FILES);
    for (const String& name : names) sink.name(name.c_str());
    return true;
  }
  bool remove(const char* path) override { return Storage.ready() && Storage.remove(path); }
  bool create(const char* path, const char* data, size_t length) override {
    if (!Storage.ready()) return false;
    HalFile file = Storage.open(path, O_WRONLY | O_CREAT | O_APPEND);
    return file && writeAndClose(file, data, length);
  }
  Appended append(const char* path, const char* data, size_t length) override {
    if (!Storage.ready()) return Appended::Failed;
    // Without the create flag SdFat's open (FatFile::open, ExFatFile::open) fails for a name it does not find and makes
    // nothing, so a file deleted mid-run is not remade without its header. Storage.exists is itself a read-only open,
    // so a card that refuses opens reads as Missing too; the writer bounds that to one new segment per streak of
    // failures.
    HalFile file = Storage.open(path, O_WRONLY | O_APPEND);
    if (!file) return Storage.ready() && !Storage.exists(path) ? Appended::Missing : Appended::Failed;
    return writeAndClose(file, data, length) ? Appended::Written : Appended::Failed;
  }

 private:
  // An unmount can land between the open and this write, since Storage locks per call. SdFat 2.3.1's FsVolume::end()
  // only nulls its pointers, and the volume lives in the static SDCardManager, so nothing freed is touched; the SDMMC
  // block device then refuses sector I/O, so the write or the close fails and this batch reports Failed (counted as
  // lost bytes in the next file). After the USB drive's detach the device stays live, so a file opened before it would
  // write to a card the host owns: unverified (deferred-work ## 6.13).
  static bool writeAndClose(HalFile& file, const char* data, size_t length) {
    const size_t written = file.write(reinterpret_cast<const uint8_t*>(data), length);
    file.flush();
    const bool closed = file.close();
    return written == length && closed;
  }
};

StorageFileStore store;

}  // namespace

DiagLog::FileStore& DiagLog::deviceFileStore() { return store; }

#endif  // CROSSHATCH_SD_LOG
