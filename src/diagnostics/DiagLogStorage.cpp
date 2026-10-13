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
    if (!Storage.ready()) return false;
    const std::vector<String> names = Storage.listFiles(directory, MAX_DIRECTORY_FILES);
    for (const String& name : names) sink.name(name.c_str());
    return true;
  }
  bool remove(const char* path) override { return Storage.ready() && Storage.remove(path); }
  bool append(const char* path, const char* data, size_t length) override {
    if (!Storage.ready()) return false;
    HalFile file = Storage.open(path, O_WRONLY | O_CREAT | O_APPEND);
    if (!file) return false;
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
