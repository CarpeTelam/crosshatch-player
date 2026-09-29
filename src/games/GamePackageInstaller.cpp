#if FREEINK_CAP_GAMES

#include "GamePackageInstaller.h"

#include <GameImages.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Manifest.h>
#include <Memory.h>
#include <PackageLimits.h>
#include <PngToBmpConverter.h>
#include <ZipFile.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "GameHash.h"
#include "GameHostCaps.h"
#include "GamePaths.h"

namespace GamePackageInstaller {

namespace {

// Read and inflate buffers of ZipFile::readFileToStream (it allocates two of them).
constexpr size_t CHUNK_BYTES = 1024;
constexpr size_t PNG_SIGNATURE_AND_IHDR_HEADER_BYTES = 16;  // 8-byte signature, IHDR's length and type

// A game's folder, /.games-tmp/<id> or /.games/<id>, and a path inside it (the member's name, or
// ".pkg", after a "/"), each sized for the longest id and name so no path is ever cut short.
constexpr size_t DIR_BYTES =
    std::char_traits<char>::length(GamePaths::TMP_DIR) + 1 + GameCore::Manifest::MAX_ID_BYTES + 1;
constexpr size_t FILE_PATH_BYTES = DIR_BYTES + GameCore::MEMBER_NAME_BYTES;
static_assert(DIR_BYTES > std::char_traits<char>::length(GamePaths::GAMES_DIR) + 1 + GameCore::Manifest::MAX_ID_BYTES,
              "DIR_BYTES holds /.games/<id> too");
static_assert(FILE_PATH_BYTES <= GamePaths::PATH_BYTES, "an extracted member's path fits GamePaths::PATH_BYTES");
static_assert(std::char_traits<char>::length(GamePaths::INBOX_DIR) + 1 + GamePaths::INBOX_NAME_BYTES <=
                  GamePaths::INBOX_PATH_BYTES,
              "an inbox path fits GamePaths::INBOX_PATH_BYTES");
static_assert(MAX_PER_RUN <= UINT8_MAX, "Report counts installs and failures in a byte");

enum class MemberKind : uint8_t { Invalid, Manifest, Lua, Png };

struct MemberName {
  char text[GameCore::MEMBER_NAME_BYTES];
};

struct InboxName {
  char text[GamePaths::INBOX_NAME_BYTES];
};

// Everything one install needs that is too big for the stack; allocated once per installAll.
struct Job {
  GameCore::ManifestReader reader;
  GameCore::Manifest manifest;
  MemberName members[GameCore::PACKAGE_MEMBERS];
  size_t memberCount = 0;
  char inboxPath[GamePaths::INBOX_PATH_BYTES];
  char tmpDir[DIR_BYTES];    // /.games-tmp/<id>, empty until the manifest names the id
  char finalDir[DIR_BYTES];  // /.games/<id>
  char pathA[FILE_PATH_BYTES];
  char pathB[FILE_PATH_BYTES];
  char badPath[GamePaths::INBOX_PATH_BYTES + sizeof(".bad")];
  uint8_t header[GameCore::IMAGE_HEADER_BYTES];
};

// Writes into ManifestReader, so manifest.json is parsed as it streams out of the zip.
class ManifestSink final : public Print {
 public:
  explicit ManifestSink(GameCore::ManifestReader& reader) : reader(reader) {}
  size_t write(const uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* buffer, const size_t size) override {
    reader.feed(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

 private:
  GameCore::ManifestReader& reader;
};

// Writes a member to its file and into the package hash together.
class MemberSink final : public Print {
 public:
  MemberSink(HalFile& file, GameHash& hash) : file(file), hash(hash) {}
  size_t write(const uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* buffer, const size_t size) override {
    const size_t written = file.write(buffer, size);
    hash.update(buffer, written);
    total += written;
    if (written != size) writeFailed = true;
    return written;
  }

  size_t total = 0;
  bool writeFailed = false;

 private:
  HalFile& file;
  GameHash& hash;
};

bool isStem(const std::string_view stem) {
  if (stem.empty() || stem.size() > GameCore::MEMBER_STEM_BYTES) return false;
  return std::all_of(stem.begin(), stem.end(),
                     [](const char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// The whitelist (AD-15): manifest.json, [a-z0-9_]{1,32}.lua, and [a-z0-9_]{1,32}.png.
MemberKind classify(const std::string_view name) {
  if (name == "manifest.json") return MemberKind::Manifest;
  constexpr size_t EXTENSION_BYTES = 4;
  if (name.size() > EXTENSION_BYTES && isStem(name.substr(0, name.size() - EXTENSION_BYTES))) {
    const std::string_view extension = name.substr(name.size() - EXTENSION_BYTES);
    if (extension == ".lua") return MemberKind::Lua;
    if (extension == ".png") return MemberKind::Png;
  }
  return MemberKind::Invalid;
}

bool hasPackageExtension(const char* name, const size_t length) {
  constexpr char EXTENSION[] = ".cpgame";
  constexpr size_t EXTENSION_BYTES = sizeof(EXTENSION) - 1;
  return length > EXTENSION_BYTES && strcasecmp(name + length - EXTENSION_BYTES, EXTENSION) == 0;
}

// Calls `visit(name)` for each .cpgame file in the inbox, until it returns false.
template <typename F>
void forEachInboxFile(F&& visit) {
  auto dir = Storage.open(GamePaths::INBOX_DIR);
  if (!dir || !dir.isDirectory()) return;
  char name[GamePaths::INBOX_NAME_BYTES];
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const size_t length = entry.getName(name, sizeof(name));
    const bool isFile = !entry.isDirectory();
    const bool fits = length > 0 && length < sizeof(name) - 1;  // getName gives 0 or a cut name when it does not fit
    entry.close();
    if (isFile && !fits) {
      LOG_INF("GAME", "Skipping an inbox file with a name over %u bytes", static_cast<unsigned>(sizeof(name) - 2));
    } else if (isFile && hasPackageExtension(name, length) && !visit(name)) {
      return;
    }
  }
}

void removeTmp() {
  if (Storage.exists(GamePaths::TMP_DIR) && !Storage.removeDir(GamePaths::TMP_DIR)) {
    LOG_ERR("GAME", "Cannot remove %s", GamePaths::TMP_DIR);
  }
}

// Lists the members, checks each name against the whitelist, sorts them by name (the order
// the package hash takes), and requires manifest.json and main.lua.
Error listMembers(Job& job, ZipFile& zip) {
  job.memberCount = 0;
  bool hasManifest = false;
  bool hasMain = false;
  Error error = Error::None;
  const bool listed = zip.enumerateFileEntries([&](const std::string_view name, uint32_t, uint32_t) {
    if (error != Error::None) return;
    const MemberKind kind = classify(name);
    if (kind == MemberKind::Invalid) {
      LOG_ERR("GAME", "Member \"%.*s\" is not allowed in a package", static_cast<int>(name.size()), name.data());
      error = Error::BadMember;
      return;
    }
    if (job.memberCount >= GameCore::PACKAGE_MEMBERS) {
      error = Error::TooManyMembers;
      return;
    }
    // classify() bounded the name by MEMBER_NAME_BYTES - 1.
    std::memcpy(job.members[job.memberCount].text, name.data(), name.size());
    job.members[job.memberCount].text[name.size()] = '\0';
    ++job.memberCount;
    hasManifest = hasManifest || kind == MemberKind::Manifest;
    hasMain = hasMain || name == "main.lua";
  });
  if (!listed) return Error::NotAPackage;
  if (error != Error::None) return error;

  // Insertion sort: at most 32 names, and std::sort would cost about 1 KB of flash for them.
  for (size_t i = 1; i < job.memberCount; ++i) {
    const MemberName moving = job.members[i];
    size_t at = i;
    for (; at > 0 && std::strcmp(job.members[at - 1].text, moving.text) > 0; --at)
      job.members[at] = job.members[at - 1];
    job.members[at] = moving;
  }
  for (size_t i = 1; i < job.memberCount; ++i) {
    if (std::strcmp(job.members[i - 1].text, job.members[i].text) == 0) {
      LOG_ERR("GAME", "Member \"%s\" appears twice", job.members[i].text);
      return Error::BadMember;
    }
  }
  if (!hasManifest) return Error::BadManifest;
  if (!hasMain) return Error::NoMain;
  return Error::None;
}

// Parses manifest.json as it streams out of the zip, and applies Manifest::check. An
// Unavailable game is installed: the launcher marks it.
Error readManifest(Job& job, ZipFile& zip) {
  job.reader.begin();
  ManifestSink sink(job.reader);
  if (!zip.readFileToStream("manifest.json", sink, CHUNK_BYTES)) return Error::BadManifest;
  const GameCore::ManifestError parsed = job.reader.finish(job.manifest);
  if (parsed != GameCore::ManifestError::None) {
    LOG_ERR("GAME", "manifest.json: %s", GameCore::describe(parsed));
    return Error::BadManifest;
  }
  const GameCore::CheckResult verdict = job.manifest.check(gameHostCaps());
  if (verdict.status == GameCore::CheckStatus::Invalid) {
    LOG_ERR("GAME", "manifest.json: %s", GameCore::describe(verdict.reason));
    return Error::BadManifest;
  }
  return Error::None;
}

// Reads icon.png's IHDR from the extracted file and requires a square image (the converter
// keeps a shape, so only a square comes out at 64x64). Leaves the file at its start.
Error checkIconIsSquare(HalFile& png) {
  uint8_t dimensions[8];
  if (!png.seek(PNG_SIGNATURE_AND_IHDR_HEADER_BYTES) ||
      png.read(dimensions, sizeof(dimensions)) != sizeof(dimensions)) {
    return Error::BadImage;
  }
  const auto bigEndian = [](const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
           static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
  };
  if (bigEndian(dimensions) != bigEndian(dimensions + 4)) {
    LOG_ERR("GAME", "icon.png is %ux%u: the icon must be square", static_cast<unsigned>(bigEndian(dimensions)),
            static_cast<unsigned>(bigEndian(dimensions + 4)));
    return Error::BadImage;
  }
  return png.seek(0) ? Error::None : Error::SdCard;
}

// Converts the extracted job.pathA (<name>.png) to job.pathB (<name>.bmp) in the converter's
// 1-bit layout, deletes the PNG, and checks the result the way the game loader will: the
// layout checkImageHeader accepts and, for images, the budget; for the icon, 64x64.
// icon.png is scaled to 64x64; any other image keeps its own size.
Error convertImage(Job& job, const bool isIcon, GameCore::ImageBudget& budget) {
  {
    HalFile png;
    if (!Storage.openFileForRead("GAME", job.pathA, png)) return Error::SdCard;
    if (isIcon) {
      const Error square = checkIconIsSquare(png);
      if (square != Error::None) return square;
    }
    HalFile bmp;
    if (!Storage.openFileForWrite("GAME", job.pathB, bmp)) return Error::SdCard;
    // Target 0 x 0 keeps the image's own size.
    const int target = isIcon ? GameCore::ICON_PIXELS : 0;
    const bool converted = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(png, bmp, target, target);
    png.close();
    const bool closed = bmp.close();
    if (!converted) {
      LOG_ERR("GAME", "Cannot convert %s", job.pathA);
      return Error::BadImage;
    }
    if (!closed) return Error::SdCard;
  }
  if (!Storage.remove(job.pathA)) return Error::SdCard;

  HalFile bmp;
  if (!Storage.openFileForRead("GAME", job.pathB, bmp)) return Error::SdCard;
  const size_t fileBytes = bmp.size();
  const int headerBytes = bmp.read(job.header, sizeof(job.header));
  bmp.close();
  if (headerBytes < 0) return Error::SdCard;
  GameCore::ImageHeader header;
  const GameCore::ImageCheck check =
      isIcon ? GameCore::checkImageHeader(job.header, static_cast<size_t>(headerBytes), fileBytes, fileBytes, header)
             : budget.add(job.header, static_cast<size_t>(headerBytes), fileBytes, header);
  if (check != GameCore::ImageCheck::Ok) {
    LOG_ERR("GAME", "%s: %s", job.pathB, GameCore::imageCheckName(check));
    return Error::BadImage;
  }
  if (isIcon && (header.width != GameCore::ICON_PIXELS || header.height != GameCore::ICON_PIXELS)) {
    LOG_ERR("GAME", "icon.bmp came out %ux%u, not %dx%d", static_cast<unsigned>(header.width),
            static_cast<unsigned>(header.height), GameCore::ICON_PIXELS, GameCore::ICON_PIXELS);
    return Error::BadImage;
  }
  return Error::None;
}

// Extracts every member, in name order, to job.tmpDir, hashing it as it goes, and converts
// each .png to a .bmp.
Error extract(Job& job, ZipFile& zip, uint8_t (&packageHash)[GamePkg::HASH_BYTES]) {
  GameHash hash;
  if (!hash.ok()) return Error::OutOfMemory;
  GameCore::ImageBudget budget;
  for (size_t i = 0; i < job.memberCount; ++i) {
    const char* name = job.members[i].text;
    size_t size = 0;
    if (!zip.getInflatedFileSize(name, &size)) return Error::NotAPackage;
    GamePkg::hashMemberStart(hash, name, static_cast<uint32_t>(size));

    snprintf(job.pathA, sizeof(job.pathA), "%s/%s", job.tmpDir, name);
    HalFile out;
    if (!Storage.openFileForWrite("GAME", job.pathA, out)) return Error::SdCard;
    MemberSink sink(out, hash);
    const bool streamed = zip.readFileToStream(name, sink, CHUNK_BYTES);
    const bool closed = out.close();
    if (!streamed || sink.total != size) return sink.writeFailed ? Error::SdCard : Error::NotAPackage;
    if (!closed) return Error::SdCard;

    if (classify(name) == MemberKind::Png) {
      // <name>.png becomes <name>.bmp beside it.
      const size_t stem = std::strlen(job.pathA) - std::strlen(".png");
      std::memcpy(job.pathB, job.pathA, stem);
      std::memcpy(job.pathB + stem, ".bmp", sizeof(".bmp"));
      const Error converted = convertImage(job, std::strcmp(name, "icon.png") == 0, budget);
      if (converted != Error::None) return converted;
    }
  }
  uint8_t digest[GameHash::DIGEST_BYTES];
  if (!hash.finish(digest)) return Error::OutOfMemory;
  GamePkg::packageHash(digest, packageHash);
  return Error::None;
}

// Replaces /.games/<id>/ with the extracted folder and writes .pkg last, as the commit marker:
// a folder without one is not a game, so a failure before it leaves no phantom in the registry.
Error commit(Job& job, const uint8_t (&packageHash)[GamePkg::HASH_BYTES]) {
  if (!Storage.ensureDirectoryExists(GamePaths::GAMES_DIR)) return Error::SdCard;
  if (Storage.exists(job.finalDir) && !Storage.removeDir(job.finalDir)) return Error::SdCard;
  if (!Storage.rename(job.tmpDir, job.finalDir)) return Error::SdCard;

  snprintf(job.pathA, sizeof(job.pathA), "%s/%s", job.finalDir, GamePaths::PKG_NAME);
  char pkg[GamePkg::FILE_BYTES];
  GamePkg::formatPkg(packageHash, pkg);
  HalFile file;
  if (!Storage.openFileForWrite("GAME", job.pathA, file)) return Error::SdCard;
  const size_t written = file.write(pkg, sizeof(pkg));
  const bool closed = file.close();
  return written == sizeof(pkg) && closed ? Error::None : Error::SdCard;
}

Error install(Job& job, const char* fileName) {
  job.tmpDir[0] = '\0';
  snprintf(job.inboxPath, sizeof(job.inboxPath), "%s/%s", GamePaths::INBOX_DIR, fileName);
  // ZipFile keeps a reference to its path.
  const std::string zipPath(job.inboxPath);
  ZipFile zip(zipPath);

  Error error = listMembers(job, zip);
  if (error != Error::None) return error;
  error = readManifest(job, zip);
  if (error != Error::None) return error;

  snprintf(job.tmpDir, sizeof(job.tmpDir), "%s/%s", GamePaths::TMP_DIR, job.manifest.id);
  snprintf(job.finalDir, sizeof(job.finalDir), "%s/%s", GamePaths::GAMES_DIR, job.manifest.id);
  if (!Storage.mkdir(job.tmpDir)) return Error::SdCard;

  uint8_t packageHash[GamePkg::HASH_BYTES];
  error = extract(job, zip, packageHash);
  if (error != Error::None) return error;
  error = commit(job, packageHash);
  if (error != Error::None) return error;

  // The game is installed; a file that stays would only reinstall the same package.
  if (!Storage.remove(job.inboxPath)) LOG_ERR("GAME", "Cannot delete %s", job.inboxPath);
  LOG_INF("GAME", "Installed %s from %s", job.manifest.id, fileName);
  return Error::None;
}

// SdCard and OutOfMemory are not the package's fault: its file stays for the next try.
bool packageIsInvalid(const Error error) { return error != Error::SdCard && error != Error::OutOfMemory; }

void markBad(Job& job) {
  snprintf(job.badPath, sizeof(job.badPath), "%s.bad", job.inboxPath);
  // An earlier .bad of the same name is replaced.
  if (Storage.exists(job.badPath) && !Storage.remove(job.badPath)) LOG_ERR("GAME", "Cannot remove %s", job.badPath);
  if (!Storage.rename(job.inboxPath, job.badPath)) {
    LOG_ERR("GAME", "Cannot rename %s to %s", job.inboxPath, job.badPath);
  }
}

}  // namespace

const char* describe(const Error error) {
  switch (error) {
    case Error::None:
      return "no error";
    case Error::SdCard:
      return "SD card error";
    case Error::OutOfMemory:
      return "out of memory";
    case Error::NotAPackage:
      return "not a readable zip";
    case Error::BadManifest:
      return "manifest.json missing or invalid";
    case Error::BadMember:
      return "a member is not allowed";
    case Error::NoMain:
      return "main.lua missing";
    case Error::TooManyMembers:
      return "too many members";
    case Error::BadImage:
      return "an image is not usable";
  }
  return "unknown error";
}

bool hasInbox() {
  bool found = false;
  forEachInboxFile([&found](const char*) {
    found = true;
    return false;
  });
  return found;
}

Report installAll() {
  Report report;
  removeTmp();  // a leftover from an install that never finished
  if (!hasInbox()) return report;

  auto names = makeUniqueNoThrow<InboxName[]>(MAX_PER_RUN);
  auto job = makeUniqueNoThrow<Job>();
  if (!names || !job) {
    LOG_ERR("GAME", "OOM: installer (%u B)", static_cast<unsigned>(sizeof(Job) + MAX_PER_RUN * sizeof(InboxName)));
    report.failed = 1;
    report.firstError = Error::OutOfMemory;
    return report;
  }
  size_t count = 0;
  forEachInboxFile([&](const char* name) {
    snprintf(names[count].text, sizeof(names[count].text), "%s", name);
    return ++count < MAX_PER_RUN;
  });

  for (size_t i = 0; i < count; ++i) {
    const Error error = install(*job, names[i].text);
    if (error == Error::None) {
      ++report.installed;
      continue;
    }
    LOG_ERR("GAME", "%s not installed: %s", names[i].text, describe(error));
    if (report.failed++ == 0) {
      report.firstError = error;
      snprintf(report.firstFile, sizeof(report.firstFile), "%s", names[i].text);
    }
    if (job->tmpDir[0] != '\0') Storage.removeDir(job->tmpDir);
    if (packageIsInvalid(error)) markBad(*job);
  }
  removeTmp();
  return report;
}

}  // namespace GamePackageInstaller

#endif  // FREEINK_CAP_GAMES
