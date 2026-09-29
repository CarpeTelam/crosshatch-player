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
#include "GameRegistry.h"
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
static_assert(MAX_PER_RUN <= UINT8_MAX, "Report counts installs in a byte");
static_assert(GamePaths::INBOX_PATH_BYTES <= FILE_PATH_BYTES, "installAll builds an inbox path in Job::pathA");

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
  char asidePath[GamePaths::INBOX_PATH_BYTES + sizeof(".installed.9")];  // the inbox file renamed out of the inbox
  bool gamesCounted = false;  // installedGames is read (once a call, when the first package needs it)
  bool addsGame = false;      // the package being installed is a game whose id is not installed yet
  size_t installedGames = 0;  // folders with a valid .pkg, stopping at GameRegistry::MAX_GAMES
  uint8_t header[GameCore::IMAGE_HEADER_BYTES];
};

// A zip reader and the path it reads, on the heap: ZipFile keeps a reference to its path, and with its cache it is
// over 100 B, which would put install() past the 256 B rule for locals.
struct ZipScratch {
  explicit ZipScratch(const char* zipPath) : path(zipPath), file(path) {}
  ZipScratch(const ZipScratch&) = delete;
  ZipScratch& operator=(const ZipScratch&) = delete;
  std::string path;
  ZipFile file;
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

// Sorts the members by name (the order the package hash takes), and refuses a name that appears twice. Its own function
// because the Member copy it moves through, in listMembers' frame, took that past 256 B.
[[gnu::noinline]] Error sortMembers(Job& job) {
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
  return Error::None;
}

// What one pass over the zip's directory found.
struct DirectoryScan {
  bool hasManifest = false;
  bool hasMain = false;
  uint32_t luaBytes = 0;
  Error error = Error::None;  // why the pass stopped, when it did
};

// Reads the directory through ZipDirectory, filling job.members. Its own function because ZipDirectory::read carries
// its header buffers in its frame, which in listMembers' took that past 256 B.
[[gnu::noinline]] ZipDirectory::Status readDirectory(Job& job, HalFile& file, const size_t fileBytes,
                                                     DirectoryScan& scan) {
  FileReader reader(file);
  return ZipDirectory::read(reader, static_cast<uint32_t>(fileBytes), [&](const ZipDirectory::Entry& entry) {
    const MemberKind kind = entry.nameUsable ? classify(entry.name) : MemberKind::Invalid;
    if (kind == MemberKind::Invalid) {
      LOG_ERR("GAME", "Member \"%s\" is not allowed in a package", entry.name);
      scan.error = Error::BadMember;
    } else if (job.memberCount >= GameCore::PACKAGE_MEMBERS) {
      scan.error = Error::TooManyMembers;
    } else if (entry.uncompressedSize > GameCore::MEMBER_BYTES) {
      LOG_ERR("GAME", "Member \"%s\" declares %lu bytes", entry.name,
              static_cast<unsigned long>(entry.uncompressedSize));
      scan.error = Error::MemberTooBig;
    }
    // Two members on the same bytes would extract far more than the package holds.
    for (size_t i = 0; i < job.memberCount && scan.error == Error::None; ++i) {
      if (entry.localAt < job.members[i].dataEnd && job.members[i].localAt < entry.dataEnd) {
        LOG_ERR("GAME", "Members \"%s\" and \"%s\" overlap", job.members[i].name, entry.name);
        scan.error = Error::BadDirectory;
      }
    }
    // Each member is at most MEMBER_BYTES, so the total cannot wrap.
    if (kind == MemberKind::Lua) scan.luaBytes += entry.uncompressedSize;
    if (scan.error == Error::None && scan.luaBytes > GameCore::LUA_SOURCES_BYTES) scan.error = Error::SourcesTooBig;
    if (scan.error != Error::None) return false;
    // classify() bounded the name by MEMBER_NAME_BYTES - 1.
    Member& member = job.members[job.memberCount++];
    std::memcpy(member.name, entry.name, std::strlen(entry.name) + 1);
    member.crc = entry.crc;
    member.size = entry.uncompressedSize;
    member.localAt = entry.localAt;
    member.dataEnd = entry.dataEnd;
    scan.hasManifest = scan.hasManifest || kind == MemberKind::Manifest;
    scan.hasMain = scan.hasMain || std::strcmp(entry.name, "main.lua") == 0;
    return true;
  });
}

// Reads the zip's directory (ZipDirectory), checks each name against the whitelist and each declared
// size against the member limit, sorts the members by name (the order the package hash takes), and
// requires manifest.json and main.lua. Nothing is extracted yet, so a rejection here has touched no card
// folder. A file of the wrong size is judged before its directory is read.
[[gnu::noinline]] Error listMembers(Job& job) {
  HalFile file;
  if (!Storage.openFileForRead("GAME", job.inboxPath, file)) return Error::SdCard;
  const size_t fileBytes = file.size();
  if (fileBytes > GameCore::PACKAGE_BYTES) return Error::PackageTooBig;

  job.memberCount = 0;
  DirectoryScan scan;
  const ZipDirectory::Status status = readDirectory(job, file, fileBytes, scan);
  file.close();
  switch (status) {
    case ZipDirectory::Status::Ok:
      break;
    case ZipDirectory::Status::Stopped:
      return scan.error;
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

  const Error sorted = sortMembers(job);
  if (sorted != Error::None) return sorted;
  if (!scan.hasManifest) return Error::BadManifest;
  if (!scan.hasMain) return Error::NoMain;
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
[[gnu::noinline]] Error readManifest(Job& job, ZipFile& zip) {
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
[[gnu::noinline]] Error convertImage(Job& job, const bool isIcon, GameCore::ImageBudget& budget) {
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
[[gnu::noinline]] Error extract(Job& job, ZipFile& zip, uint8_t (&packageHash)[GamePkg::HASH_BYTES]) {
  // On the heap: the SHA-256 context is over 100 B, which with the rest of this frame passes the 256 B rule.
  auto hash = makeUniqueNoThrow<GameHash>();
  if (!hash || !hash->ok()) return Error::OutOfMemory;
  GameCore::ImageBudget budget;
  for (size_t i = 0; i < job.memberCount; ++i) {
    const Member& member = job.members[i];
    const char* name = member.name;
    GamePkg::hashMemberStart(*hash, name, member.size);

    snprintf(job.pathA, sizeof(job.pathA), "%s/%s", job.tmpDir, name);
    HalFile out;
    if (!Storage.openFileForWrite("GAME", job.pathA, out)) return Error::SdCard;
    MemberGuard guard(member.size, classify(name) == MemberKind::Lua);
    FileSink sink(out, hash.get(), &guard);
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
  if (!hash->finish(digest)) return Error::OutOfMemory;
  GamePkg::packageHash(digest, packageHash);
  return Error::None;
}

// Replaces /.games/<id>/ with the extracted folder and writes .pkg last, as the commit marker:
// a folder without one is not a game, so a failure before it leaves no phantom in the registry.
[[gnu::noinline]] Error commit(Job& job, const uint8_t (&packageHash)[GamePkg::HASH_BYTES]) {
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

// The suffix of an inbox file that installed but would not delete. Neither it nor ".bad" ends in ".cpgame", so the
// inbox scan skips both.
constexpr char INSTALLED_SUFFIX[] = ".installed";

// How many names moveAside tries: <name><suffix>, then <name><suffix>.2 and on.
constexpr unsigned ASIDE_NAMES = 5;

// Renames the inbox file to <name><suffix>, replacing an earlier copy. A copy that will not go (a file the card marks
// read-only keeps that mark when it is renamed, and SdFat's rename will not replace a name) leaves the next name, so
// an update of the same file cannot be stuck behind it; false when no name is free or the rename itself fails.
bool moveAside(Job& job, const char* suffix) {
  for (unsigned n = 1; n <= ASIDE_NAMES; ++n) {
    if (n == 1) {
      snprintf(job.asidePath, sizeof(job.asidePath), "%s%s", job.inboxPath, suffix);
    } else {
      snprintf(job.asidePath, sizeof(job.asidePath), "%s%s.%u", job.inboxPath, suffix, n);
    }
    if (Storage.exists(job.asidePath) && !Storage.remove(job.asidePath)) {
      LOG_ERR("GAME", "Cannot remove %s", job.asidePath);
      continue;
    }
    if (Storage.rename(job.inboxPath, job.asidePath)) return true;
    LOG_ERR("GAME", "Cannot rename %s to %s", job.inboxPath, job.asidePath);
    return false;  // the name was free, so this is the card's fault and another name would not help
  }
  LOG_ERR("GAME", "No free name for %s%s", job.inboxPath, suffix);
  return false;
}

// The folders of /.games with a valid .pkg (the first test GameRegistry::load applies), counted up to
// GameRegistry::MAX_GAMES. A folder whose manifest the registry then skips still counts: the count can only be high,
// never let a hidden game through. A card that cannot list /.games counts as none: the install then fails on its own
// card fault, if it is one.
size_t countInstalledGames() {
  auto dir = Storage.open(GamePaths::GAMES_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  char name[GamePaths::INBOX_NAME_BYTES];
  uint8_t hash[GamePkg::HASH_BYTES];
  size_t games = 0;
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const size_t length = entry.getName(name, sizeof(name));
    const bool isFolder = entry.isDirectory();
    entry.close();
    if (!isFolder || length == 0 || length >= sizeof(name) - 1 || name[0] == '.') continue;
    if (GameRegistry::readPackageHash(name, hash) && ++games >= GameRegistry::MAX_GAMES) break;
  }
  return games;
}

// True when installing job.manifest.id would make more than GameRegistry::MAX_GAMES games: the registry lists that
// many, in directory order, so a game past them would be installed and not shown, and could not be removed. A package
// that replaces an installed id is always allowed. The folders are counted once a call (the first package that is not
// a replacement), and installAll keeps the count as installs land.
bool wouldBeOverTheLimit(Job& job) {
  uint8_t hash[GamePkg::HASH_BYTES];
  job.addsGame = !GameRegistry::readPackageHash(job.manifest.id, hash);
  if (!job.addsGame) return false;
  if (!job.gamesCounted) {
    job.installedGames = countInstalledGames();
    job.gamesCounted = true;
  }
  return job.installedGames >= GameRegistry::MAX_GAMES;
}

[[gnu::noinline]] Error install(Job& job, const char* fileName) {
  job.tmpDir[0] = '\0';
  job.renameTried = false;
  snprintf(job.inboxPath, sizeof(job.inboxPath), "%s/%s", GamePaths::INBOX_DIR, fileName);
  Error error = listMembers(job);
  if (error != Error::None) return error;
  auto zip = makeUniqueNoThrow<ZipScratch>(job.inboxPath);
  if (!zip) {
    LOG_ERR("GAME", "OOM: zip reader for %s", job.inboxPath);
    return Error::OutOfMemory;
  }
  error = readManifest(job, zip->file);
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
  error = extract(job, zip->file, packageHash);
  if (error != Error::None) return error;
  // After the package has proved valid (an invalid one at the limit is .bad with its real reason, not "too many"), and
  // before it touches /.games.
  if (wouldBeOverTheLimit(job)) {
    LOG_ERR("GAME", "Not installing %s: %u games are installed already", job.manifest.id,
            static_cast<unsigned>(GameRegistry::MAX_GAMES));
    return Error::TooManyGames;
  }
  error = commit(job, packageHash);
  if (error != Error::None) return error;
  if (job.gamesCounted && job.addsGame) ++job.installedGames;

  LOG_INF("GAME", "Installed %s from %s", job.manifest.id, fileName);
  if (Storage.remove(job.inboxPath)) return Error::None;
  LOG_ERR("GAME", "Cannot delete %s", job.inboxPath);
  // The game is installed, but a file that stays reinstalls it on every visit, which would undo a Remove. Moving it
  // out of the inbox (the scan ignores the new name) does the same as deleting it; a file that will not move either
  // is a failure to report.
  return moveAside(job, INSTALLED_SUFFIX) ? Error::None : Error::SdCard;
}

// SdCard, OutOfMemory, and TooManyGames are not the package's fault: its file stays for the next try (a game removed
// meanwhile makes room).
bool packageIsInvalid(const Error error) {
  return error != Error::SdCard && error != Error::OutOfMemory && error != Error::TooManyGames;
}

// Renames the inbox file <name>.bad, replacing an earlier one; false when it would not move.
bool markBad(Job& job) { return moveAside(job, ".bad"); }

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
    case Error::TooManyGames:
      return "the maximum number of games is installed already";
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
  // Files are taken in batches of the cap that is left. A file that stays in the inbox (a card fault, or a package
  // that waits for room) is skipped by the next batch and does not count against the cap when it waits for room, so
  // a package behind 32 or more that wait, an update of an installed game especially, is still reached.
  size_t judged = 0;  // files that used the cap: everything but the ones that wait for room
  size_t stayed = 0;  // files judged this call that are still in the inbox
  while (judged < MAX_PER_RUN) {
    const size_t room = MAX_PER_RUN - judged;
    size_t count = 0;
    size_t seen = 0;
    forEachInboxFile([&](const char* name) {
      if (seen++ < stayed) return true;
      snprintf(names[count].text, sizeof(names[count].text), "%s", name);
      return ++count < room;
    });
    if (count == 0) break;

    for (size_t i = 0; i < count; ++i) {
      Error error = install(*job, names[i].text);
      if (error == Error::None) {
        ++report.installed;
        ++judged;
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
      if (report.failed == 0) {
        report.firstError = error;
        snprintf(report.firstFile, sizeof(report.firstFile), "%s", names[i].text);
      }
      if (report.failed < UINT8_MAX) ++report.failed;  // a saturated count is still a failure
      if (error != Error::TooManyGames) ++judged;
    }
    // The renames and deletes above take files out of the inbox and leave the others in their order, so the ones
    // that are still there are the first `stayed` of the next scan.
    for (size_t i = 0; i < count; ++i) {
      snprintf(job->pathA, sizeof(job->pathA), "%s/%s", GamePaths::INBOX_DIR, names[i].text);
      if (Storage.exists(job->pathA)) ++stayed;
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
  for (size_t i = 0; valid && i < length; ++i) {
    valid = (id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-';
  }
  if (!valid) return Error::BadManifest;

  // "Not there" needs a card that answers: a /.games that cannot be opened is the card's fault, not a game that is
  // gone.
  {
    auto games = Storage.open(GamePaths::GAMES_DIR);
    if (!games) {
      LOG_ERR("GAME", "Cannot open %s", GamePaths::GAMES_DIR);
      return Error::SdCard;
    }
  }
  // One buffer, used in turn for the folder, its .pkg, and /.games-tmp/<id> (three of them would be 288 B of locals).
  char path[GamePaths::PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s", GamePaths::GAMES_DIR, id);
  if (!Storage.exists(path)) return Error::None;
  snprintf(path, sizeof(path), "%s/%s/%s", GamePaths::GAMES_DIR, id, GamePaths::PKG_NAME);
  const bool marked = Storage.exists(path);
  // A folder without a .pkg beside a /.games-tmp/<id> may be the two halves of an interrupted folder move, on one
  // cluster chain; removing one would free clusters the other uses. The installer's probe tells them apart, and a
  // shared pair is left alone (as removeTmp leaves it).
  if (!marked) {
    snprintf(path, sizeof(path), "%s/%s", GamePaths::TMP_DIR, id);
    if (Storage.exists(path) && foldersShareClusters(id)) {
      LOG_ERR("GAME", "Keeping %s/%s: it may share clusters with %s, which has no %s", GamePaths::GAMES_DIR, id, path,
              GamePaths::PKG_NAME);
      return Error::SdCard;
    }
  }
  // The marker first, as commit() does: a stop or a failure from here on leaves an unlisted folder, never a
  // listed game with files missing.
  if (marked) {
    snprintf(path, sizeof(path), "%s/%s/%s", GamePaths::GAMES_DIR, id, GamePaths::PKG_NAME);
    if (!Storage.remove(path)) {
      LOG_ERR("GAME", "Cannot remove %s", path);
      return Error::SdCard;
    }
  }
  snprintf(path, sizeof(path), "%s/%s", GamePaths::GAMES_DIR, id);
  if (!Storage.removeDir(path)) {
    LOG_ERR("GAME", "Cannot remove %s", path);
    return Error::SdCard;
  }
  LOG_INF("GAME", "Removed %s", id);
  return Error::None;
}

}  // namespace GamePackageInstaller

#endif  // FREEINK_CAP_GAMES
