#if FREEINK_CAP_GAMES

#include "GamePackageInstaller.h"

#include <GameIcons.h>
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
#include "MemberGuard.h"
#include "ZipDirectory.h"

namespace GamePackageInstaller {

namespace {

// Read and inflate buffers of ZipFile::readFileToStream (it allocates two of them).
constexpr size_t CHUNK_BYTES = 1024;

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

// What the directory said of a member: its name, and the CRC and size the streamed bytes must match.
struct Member {
  char name[GameCore::MEMBER_NAME_BYTES];
  uint32_t crc;
  uint32_t size;
  uint32_t localAt;  // the bytes it takes in the zip, [localAt, dataEnd)
  uint32_t dataEnd;
};

struct InboxName {
  char text[GamePaths::INBOX_NAME_BYTES];
};

// Everything one install needs that is too big for the stack; allocated once per installAll.
struct Job {
  GameCore::ManifestReader reader;
  GameCore::Manifest manifest;
  Member members[GameCore::PACKAGE_MEMBERS];
  size_t memberCount = 0;
  char inboxPath[GamePaths::INBOX_PATH_BYTES];
  bool renameTried = false;  // commit reached the folder rename: /.games-tmp/<id> may share clusters with /.games/<id>
  char tmpDir[DIR_BYTES];    // /.games-tmp/<id> once this install has made it, else empty
  char finalDir[DIR_BYTES];  // /.games/<id>
  char pathA[FILE_PATH_BYTES];
  char pathB[FILE_PATH_BYTES];
  char badPath[GamePaths::INBOX_PATH_BYTES + sizeof(".bad")];
  uint8_t header[GameCore::IMAGE_HEADER_BYTES];
};

// Writes into ManifestReader, so manifest.json is parsed as it streams out of the zip.
class ManifestSink final : public Print {
 public:
  ManifestSink(GameCore::ManifestReader& reader, MemberGuard& guard) : reader(reader), guard(guard) {}
  size_t write(const uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* buffer, const size_t size) override {
    if (!guard.admit(buffer, size)) return 0;
    reader.feed(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

 private:
  GameCore::ManifestReader& reader;
  MemberGuard& guard;
};

// Writes to a file, and into the package hash and the guard too when there are any, and notes any
// short write: the converter ignores write results, so this is how a card fault is told from bad data.
class FileSink final : public Print {
 public:
  FileSink(HalFile& file, GameHash* hash, MemberGuard* guard = nullptr) : file(file), hash(hash), guard(guard) {}
  size_t write(const uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* buffer, const size_t size) override {
    if (guard && !guard->admit(buffer, size)) return 0;
    const size_t written = file.write(buffer, size);
    if (hash) hash->update(buffer, written);
    if (written != size) writeFailed = true;
    return written;
  }

  bool writeFailed = false;

 private:
  HalFile& file;
  GameHash* hash;
  MemberGuard* guard;
};

// (A backstop: the stored name is already bounded by MEMBER_NAME_BYTES, so no stem over the limit gets here.)
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
    } else if (isFile && name[0] != '.' && hasPackageExtension(name, length) && !visit(name)) {
      // (A name that starts with "." is a file manager's hidden or AppleDouble sidecar, never a package.)
      return;
    }
  }
}

// A file no package can hold (it is off the member whitelist), made in /.games-tmp/<id> to see
// whether /.games/<id> shows it: two folders on one cluster chain share their directory data.
constexpr char PROBE_NAME[] = ".xlink";

// True when /.games-tmp/<id> and /.games/<id> may share clusters: the probe file shows in both, or
// could not be made, or would not go (the safe side when unsure).
bool foldersShareClusters(const char* id) {
  char tmpProbe[GamePaths::PATH_BYTES];
  char finalProbe[GamePaths::PATH_BYTES];
  snprintf(tmpProbe, sizeof(tmpProbe), "%s/%s/%s", GamePaths::TMP_DIR, id, PROBE_NAME);
  snprintf(finalProbe, sizeof(finalProbe), "%s/%s/%s", GamePaths::GAMES_DIR, id, PROBE_NAME);
  HalFile probe;
  if (!Storage.openFileForWrite("GAME", tmpProbe, probe)) return true;
  const bool made = probe.close();
  const bool shown = Storage.exists(finalProbe);
  const bool removed = Storage.remove(tmpProbe);
  return shown || !made || !removed;
}

// SdFat moves a folder by making the new entry before it removes the old one, so a power loss
// in between leaves /.games-tmp/<id> and /.games/<id> on one cluster chain, and freeing either
// frees clusters the other uses. A /.games/<id> without a .pkg may be that (the .pkg is written
// after the move) or the leftover of a removal that stopped partway, beside an independent
// scratch folder; foldersShareClusters tells them apart. A shared one is left alone: the card is
// safe, and an install of that id reports SdCard until a person clears both from a computer.
bool mayRemoveTmp(const char* id) {
  char path[GamePaths::PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s", GamePaths::GAMES_DIR, id);
  if (!Storage.exists(path)) return true;
  snprintf(path, sizeof(path), "%s/%s/%s", GamePaths::GAMES_DIR, id, GamePaths::PKG_NAME);
  if (Storage.exists(path) || !foldersShareClusters(id)) return true;
  LOG_ERR("GAME", "Keeping %s/%s: it shares clusters with %s/%s, which has no %s", GamePaths::TMP_DIR, id,
          GamePaths::GAMES_DIR, id, GamePaths::PKG_NAME);
  return false;
}

// Deletes what an install that never finished left in /.games-tmp, except what mayRemoveTmp keeps.
void removeTmp() {
  {
    auto dir = Storage.open(GamePaths::TMP_DIR);
    if (!dir) return;
    if (!dir.isDirectory()) {  // a file in its place would stop every mkdir under it
      dir.close();
      if (!Storage.remove(GamePaths::TMP_DIR)) LOG_ERR("GAME", "Cannot remove %s", GamePaths::TMP_DIR);
      return;
    }
    char name[GamePaths::INBOX_NAME_BYTES];
    char path[GamePaths::PATH_BYTES];
    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      const size_t length = entry.getName(name, sizeof(name));
      const bool isFolder = entry.isDirectory();
      entry.close();
      if (length == 0 || length >= sizeof(name) - 1) continue;  // too long to be a game's id
      snprintf(path, sizeof(path), "%s/%s", GamePaths::TMP_DIR, name);
      if (isFolder && !mayRemoveTmp(name)) continue;
      if (!(isFolder ? Storage.removeDir(path) : Storage.remove(path))) LOG_ERR("GAME", "Cannot remove %s", path);
    }
  }
  Storage.rmdir(GamePaths::TMP_DIR);  // only an empty folder goes, so a kept one stays
}

// Reads the file into ZipDirectory's terms: bytes at an offset.
class FileReader {
 public:
  explicit FileReader(HalFile& file) : file(file) {}
  bool read(const uint32_t offset, void* out, const size_t count) {
    return file.seek(offset) && file.read(out, count) == static_cast<int>(count);
  }

 private:
  HalFile& file;
};

// Reads the zip's directory (ZipDirectory), checks each name against the whitelist and each declared
// size against the member limit, sorts the members by name (the order the package hash takes), and
// requires manifest.json and main.lua. Nothing is extracted yet, so a rejection here has touched no card
// folder. A file of the wrong size is judged before its directory is read.
Error listMembers(Job& job) {
  HalFile file;
  if (!Storage.openFileForRead("GAME", job.inboxPath, file)) return Error::SdCard;
  const size_t fileBytes = file.size();
  if (fileBytes > GameCore::PACKAGE_BYTES) return Error::PackageTooBig;

  job.memberCount = 0;
  bool hasManifest = false;
  bool hasMain = false;
  uint32_t luaBytes = 0;
  Error error = Error::None;
  FileReader reader(file);
  const ZipDirectory::Status status =
      ZipDirectory::read(reader, static_cast<uint32_t>(fileBytes), [&](const ZipDirectory::Entry& entry) {
        const MemberKind kind = entry.nameUsable ? classify(entry.name) : MemberKind::Invalid;
        if (kind == MemberKind::Invalid) {
          LOG_ERR("GAME", "Member \"%s\" is not allowed in a package", entry.name);
          error = Error::BadMember;
        } else if (job.memberCount >= GameCore::PACKAGE_MEMBERS) {
          error = Error::TooManyMembers;
        } else if (entry.uncompressedSize > GameCore::MEMBER_BYTES) {
          LOG_ERR("GAME", "Member \"%s\" declares %lu bytes", entry.name,
                  static_cast<unsigned long>(entry.uncompressedSize));
          error = Error::MemberTooBig;
        }
        // Two members on the same bytes would extract far more than the package holds.
        for (size_t i = 0; i < job.memberCount && error == Error::None; ++i) {
          if (entry.localAt < job.members[i].dataEnd && job.members[i].localAt < entry.dataEnd) {
            LOG_ERR("GAME", "Members \"%s\" and \"%s\" overlap", job.members[i].name, entry.name);
            error = Error::BadDirectory;
          }
        }
        // Each member is at most MEMBER_BYTES, so the total cannot wrap.
        if (kind == MemberKind::Lua) luaBytes += entry.uncompressedSize;
        if (error == Error::None && luaBytes > GameCore::LUA_SOURCES_BYTES) error = Error::SourcesTooBig;
        if (error != Error::None) return false;
        // classify() bounded the name by MEMBER_NAME_BYTES - 1.
        Member& member = job.members[job.memberCount++];
        std::memcpy(member.name, entry.name, std::strlen(entry.name) + 1);
        member.crc = entry.crc;
        member.size = entry.uncompressedSize;
        member.localAt = entry.localAt;
        member.dataEnd = entry.dataEnd;
        hasManifest = hasManifest || kind == MemberKind::Manifest;
        hasMain = hasMain || std::strcmp(entry.name, "main.lua") == 0;
        return true;
      });
  file.close();
  switch (status) {
    case ZipDirectory::Status::Ok:
      break;
    case ZipDirectory::Status::Stopped:
      return error;
    case ZipDirectory::Status::ReadError:
      return Error::SdCard;
    case ZipDirectory::Status::Unsupported:
      return Error::Unsupported;
    case ZipDirectory::Status::CountMismatch:
      return Error::BadDirectory;
    case ZipDirectory::Status::TooMany:
      return Error::TooManyMembers;
    case ZipDirectory::Status::BadSize:
      return Error::BadSize;
    case ZipDirectory::Status::Malformed:
      return Error::NotAPackage;
  }

  // Insertion sort: at most 32 names, and std::sort would cost about 1 KB of flash for them.
  for (size_t i = 1; i < job.memberCount; ++i) {
    const Member moving = job.members[i];
    size_t at = i;
    for (; at > 0 && std::strcmp(job.members[at - 1].name, moving.name) > 0; --at)
      job.members[at] = job.members[at - 1];
    job.members[at] = moving;
  }
  for (size_t i = 1; i < job.memberCount; ++i) {
    if (std::strcmp(job.members[i - 1].name, job.members[i].name) == 0) {
      LOG_ERR("GAME", "Member \"%s\" appears twice", job.members[i].name);
      return Error::BadMember;
    }
  }
  if (!hasManifest) return Error::BadManifest;
  if (!hasMain) return Error::NoMain;
  return Error::None;
}

// What a member streamed through a guard was: Lua bytecode (the guard then refused the chunk, so no
// write was made and the card cannot be at fault), else the card's fault (a failed write), else the
// package's (more, fewer, or other bytes than the directory declared, or a stream ZipFile could not
// finish, which includes its own read and allocation failures; a wrong CRC).
Error judge(const MemberGuard& guard, const Member& member, const bool streamed, const bool writeFailed) {
  if (guard.binary) return Error::BinaryLua;
  if (writeFailed) return Error::SdCard;
  if (guard.overrun || !streamed || guard.total != member.size) return Error::BadSize;
  return guard.crc == member.crc ? Error::None : Error::BadCrc;
}

// Parses manifest.json as it streams out of the zip, and applies Manifest::check. An
// Unavailable game is installed: the launcher marks it.
Error readManifest(Job& job, ZipFile& zip) {
  const Member* member = nullptr;
  for (size_t i = 0; i < job.memberCount && !member; ++i) {
    if (std::strcmp(job.members[i].name, "manifest.json") == 0) member = &job.members[i];
  }
  if (!member) return Error::BadManifest;
  job.reader.begin();
  MemberGuard guard(member->size, false);
  ManifestSink sink(job.reader, guard);
  const bool streamed = zip.readFileToStream("manifest.json", sink, CHUNK_BYTES);
  const Error judged = judge(guard, *member, streamed, false);
  if (judged != Error::None) return judged;
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
  // Manifest::parse checks the icon's grammar; only this side of the GameCore boundary can see the library.
  const size_t iconLength = std::strlen(job.manifest.icon);
  if (iconLength > 0 && GameIcons::find(job.manifest.icon, iconLength) < 0) {
    LOG_ERR("GAME", "manifest.json: icon \"%s\" is not in the game icon library", job.manifest.icon);
    return Error::UnknownIcon;
  }
  return Error::None;
}

uint32_t bigEndian(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
         static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
}

// Reads a PNG's header by the rules of scripts/pack_game.py's png_size (less the IHDR CRC, which
// the converter skips): true when PngToBmpConverter can start on it, with its size in
// `width` and `height`. The file is left after the header.
bool readPngSize(HalFile& png, uint32_t& width, uint32_t& height) {
  constexpr size_t HEADER_BYTES = 8 + 8 + 13;  // signature, IHDR's length and type, IHDR's data
  constexpr uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  uint8_t h[HEADER_BYTES];
  if (png.read(h, sizeof(h)) != static_cast<int>(sizeof(h)) || std::memcmp(h, SIGNATURE, sizeof(SIGNATURE)) != 0 ||
      bigEndian(h + 8) != 13 || std::memcmp(h + 12, "IHDR", 4) != 0) {
    return false;
  }
  width = bigEndian(h + 16);
  height = bigEndian(h + 20);
  const uint8_t depth = h[24];
  const uint8_t colour = h[25];
  // The bit depths PNG allows for each colour type: greyscale 1 2 4 8 16, RGB 8 16, palette 1 2 4 8,
  // grey+alpha 8 16, RGBA 8 16 (one bit per depth value, so a depth that is not a power of two has none).
  const uint8_t allowed = colour == 0                                   ? 0x1F
                          : colour == 3                                 ? 0x0F
                          : (colour == 2 || colour == 4 || colour == 6) ? 0x18
                                                                        : 0;
  return (depth & (depth - 1)) == 0 && (allowed & depth) != 0 && h[26] == 0 && h[27] == 0 && h[28] == 0 && width > 0 &&
         width <= GameCore::IMAGE_MAX_WIDTH && height > 0 && height <= GameCore::IMAGE_MAX_HEIGHT;
}

// Converts the extracted job.pathA (<name>.png) to job.pathB (<name>.bmp) in the converter's
// 1-bit layout, deletes the PNG, and checks the result the way the game loader will: the
// layout checkImageHeader accepts and, for images, the budget; for the icon, 64x64.
// icon.png is scaled to 64x64 (so it must be square); any other image keeps its own size.
// The converter answers only true or false and ignores failed writes, so the output goes through
// a FileSink: a short write, or an output shorter than its own header says, is the card's fault
// (SdCard, the package stays); any other failure is the image's (BadImage), which includes the
// converter running out of memory, a case it cannot tell apart.
Error convertImage(Job& job, const bool isIcon, GameCore::ImageBudget& budget) {
  {
    HalFile png;
    if (!Storage.openFileForRead("GAME", job.pathA, png)) return Error::SdCard;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!readPngSize(png, width, height)) {
      LOG_ERR("GAME", "%s is not a PNG this device can convert", job.pathA);
      return Error::BadImage;
    }
    if (isIcon && width != height) {
      LOG_ERR("GAME", "icon.png is %ux%u: the icon must be square", static_cast<unsigned>(width),
              static_cast<unsigned>(height));
      return Error::BadImage;
    }
    if (!png.seek(0)) return Error::SdCard;
    HalFile bmp;
    if (!Storage.openFileForWrite("GAME", job.pathB, bmp)) return Error::SdCard;
    // Target 0 x 0 keeps the image's own size.
    const int target = isIcon ? GameCore::ICON_PIXELS : 0;
    FileSink sink(bmp, nullptr);
    const bool converted = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(png, sink, target, target);
    png.close();
    const bool closed = bmp.close();
    if (sink.writeFailed || !closed) return Error::SdCard;
    if (!converted) {
      LOG_ERR("GAME", "Cannot convert %s", job.pathA);
      return Error::BadImage;
    }
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
    // The converter writes a whole file, so a short one lost a write to the card.
    if (check == GameCore::ImageCheck::Truncated) return Error::SdCard;
    return check == GameCore::ImageCheck::OverBudget || check == GameCore::ImageCheck::TooMany ? Error::ImagesTooBig
                                                                                               : Error::BadImage;
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
    const Member& member = job.members[i];
    const char* name = member.name;
    GamePkg::hashMemberStart(hash, name, member.size);

    snprintf(job.pathA, sizeof(job.pathA), "%s/%s", job.tmpDir, name);
    HalFile out;
    if (!Storage.openFileForWrite("GAME", job.pathA, out)) return Error::SdCard;
    MemberGuard guard(member.size, classify(name) == MemberKind::Lua);
    FileSink sink(out, &hash, &guard);
    const bool streamed = zip.readFileToStream(name, sink, CHUNK_BYTES);
    const bool closed = out.close();
    const Error judged = judge(guard, member, streamed, sink.writeFailed);
    if (judged != Error::None) return judged;
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
  if (Storage.exists(job.finalDir)) {
    // removeDir deletes in directory order, so the .pkg (written last) would go last and a stop
    // partway would leave a listed game with files missing. Take the marker away first: a folder
    // without one is not a game.
    snprintf(job.pathA, sizeof(job.pathA), "%s/%s", job.finalDir, GamePaths::PKG_NAME);
    if (Storage.exists(job.pathA) && !Storage.remove(job.pathA)) return Error::SdCard;
    if (!Storage.removeDir(job.finalDir)) return Error::SdCard;
  }
  job.renameTried = true;
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
  job.renameTried = false;
  snprintf(job.inboxPath, sizeof(job.inboxPath), "%s/%s", GamePaths::INBOX_DIR, fileName);
  Error error = listMembers(job);
  if (error != Error::None) return error;
  // ZipFile keeps a reference to its path.
  const std::string zipPath(job.inboxPath);
  ZipFile zip(zipPath);
  error = readManifest(job, zip);
  if (error != Error::None) return error;

  snprintf(job.tmpDir, sizeof(job.tmpDir), "%s/%s", GamePaths::TMP_DIR, job.manifest.id);
  snprintf(job.finalDir, sizeof(job.finalDir), "%s/%s", GamePaths::GAMES_DIR, job.manifest.id);
  if (!Storage.mkdir(job.tmpDir)) {
    // Not made here, so not ours to delete: it may be a kept folder (see mayRemoveTmp).
    LOG_ERR("GAME", "Cannot make %s", job.tmpDir);
    job.tmpDir[0] = '\0';
    return Error::SdCard;
  }

  uint8_t packageHash[GamePkg::HASH_BYTES];
  error = extract(job, zip, packageHash);
  if (error != Error::None) return error;
  error = commit(job, packageHash);
  if (error != Error::None) return error;

  LOG_INF("GAME", "Installed %s from %s", job.manifest.id, fileName);
  // The game is installed, but a file that stays reinstalls it on every visit, so that is a failure to report.
  if (!Storage.remove(job.inboxPath)) {
    LOG_ERR("GAME", "Cannot delete %s", job.inboxPath);
    return Error::SdCard;
  }
  return Error::None;
}

// SdCard and OutOfMemory are not the package's fault: its file stays for the next try.
bool packageIsInvalid(const Error error) { return error != Error::SdCard && error != Error::OutOfMemory; }

// Renames the inbox file <name>.bad, replacing an earlier one; false when it would not move.
bool markBad(Job& job) {
  snprintf(job.badPath, sizeof(job.badPath), "%s.bad", job.inboxPath);
  if (Storage.exists(job.badPath) && !Storage.remove(job.badPath)) LOG_ERR("GAME", "Cannot remove %s", job.badPath);
  if (Storage.rename(job.inboxPath, job.badPath)) return true;
  LOG_ERR("GAME", "Cannot rename %s to %s", job.inboxPath, job.badPath);
  return false;
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
    case Error::PackageTooBig:
      return "the package is over its size limit";
    case Error::MemberTooBig:
      return "a member is over its size limit";
    case Error::ImagesTooBig:
      return "the images are over their budget";
    case Error::BadSize:
      return "a member is not the size it declares";
    case Error::BadCrc:
      return "a member fails its CRC";
    case Error::BinaryLua:
      return "a Lua member is a bytecode chunk";
    case Error::Unsupported:
      return "the zip uses ZIP64, encryption, or another compression method";
    case Error::BadDirectory:
      return "the zip's entry count does not match its directory, or members overlap";
    case Error::SourcesTooBig:
      return "the Lua members are over their size limit together";
    case Error::UnknownIcon:
      return "the manifest's icon is not in the game icon library";
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
    Error error = install(*job, names[i].text);
    if (error == Error::None) {
      ++report.installed;
      continue;
    }
    LOG_ERR("GAME", "%s not installed: %s", names[i].text, describe(error));
    // Until the folder rename has been tried, /.games-tmp/<id> is ours alone; after it, mayRemoveTmp decides.
    if (job->tmpDir[0] != '\0' && Storage.exists(job->tmpDir) &&
        (!job->renameTried || mayRemoveTmp(job->manifest.id))) {
      Storage.removeDir(job->tmpDir);
    }
    // A package that will not move aside would be judged again, and its reason shown, on every visit.
    if (packageIsInvalid(error) && !markBad(*job)) error = Error::SdCard;
    if (report.failed++ == 0) {
      report.firstError = error;
      snprintf(report.firstFile, sizeof(report.firstFile), "%s", names[i].text);
    }
  }
  removeTmp();
  return report;
}

Error remove(const char* id) {
  // The launcher passes a listed game's id, which Manifest::check has vetted; this keeps a path out of /.games
  // for any other caller.
  const size_t length = id ? std::strlen(id) : 0;
  bool valid = length > 0 && length <= GameCore::Manifest::MAX_ID_BYTES && id[0] != '-';
  for (size_t i = 0; valid && i < length; ++i)
    valid = (id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-';
  if (!valid) return Error::BadManifest;

  char dir[GamePaths::PATH_BYTES];
  char pkg[GamePaths::PATH_BYTES];
  snprintf(dir, sizeof(dir), "%s/%s", GamePaths::GAMES_DIR, id);
  snprintf(pkg, sizeof(pkg), "%s/%s", dir, GamePaths::PKG_NAME);
  if (!Storage.exists(dir)) return Error::None;
  // The marker first, as commit() does: a stop or a failure from here on leaves an unlisted folder, never a
  // listed game with files missing.
  if (Storage.exists(pkg) && !Storage.remove(pkg)) {
    LOG_ERR("GAME", "Cannot remove %s", pkg);
    return Error::SdCard;
  }
  if (!Storage.removeDir(dir)) {
    LOG_ERR("GAME", "Cannot remove %s", dir);
    return Error::SdCard;
  }
  LOG_INF("GAME", "Removed %s", id);
  return Error::None;
}

}  // namespace GamePackageInstaller

#endif  // FREEINK_CAP_GAMES
