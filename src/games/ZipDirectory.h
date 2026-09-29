#pragma once

#include <PackageLimits.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// A .cpgame's zip directory read the way the installer needs it (spine AD-15). lib/ZipFile keeps its
// EOCD entry count private, checks no CRC, and skips a name of 256 bytes or more without a word, so the
// installer reads the EOCD and the central directory itself, before it extracts anything, and streams
// the members through ZipFile afterwards. Pure: it reads through `reader`, so the host tests feed it
// bytes in memory and the installer feeds it the file.
//
// The rules are strict on purpose, so that ZipFile, which reads the same bytes later, sees what this
// parser saw: the EOCD is the last 22 bytes (no zip comment), and the central directory ends where the
// EOCD begins. Header-only because the harness suites build the installer from explicit source lists.
namespace ZipDirectory {

enum class Status : uint8_t {
  Ok,
  ReadError,      // the reader failed: the card's fault, not the package's
  Malformed,      // no EOCD at the end, a comment, a signature or an offset that does not fit
  Unsupported,    // ZIP64, encryption, or a method other than stored (0) and deflate (8)
  CountMismatch,  // the EOCD's entry count is not what the directory holds
  TooMany,        // the directory holds more than PACKAGE_MEMBERS entries, as its EOCD counts them
  BadSize,        // a stored member whose compressed and uncompressed sizes differ
  Stopped,        // the visitor refused an entry
};

struct Entry {
  char name[GameCore::MEMBER_NAME_BYTES];  // NUL-terminated
  // False when the stored name is too long for `name`, or holds a NUL: no whitelisted name is.
  bool nameUsable;
  uint32_t crc;
  uint32_t uncompressedSize;
  // The bytes the member takes in the file: its local header through its data, [localAt, dataEnd).
  uint32_t localAt;
  uint32_t dataEnd;
};

namespace detail {

inline uint16_t le16(const uint8_t* bytes) { return static_cast<uint16_t>(bytes[0] | bytes[1] << 8); }
inline uint32_t le32(const uint8_t* bytes) { return le16(bytes) | static_cast<uint32_t>(le16(bytes + 2)) << 16; }

constexpr uint32_t EOCD_SIGNATURE = 0x06054b50;
constexpr uint32_t ZIP64_LOCATOR_SIGNATURE = 0x07064b50;
constexpr uint32_t CENTRAL_SIGNATURE = 0x02014b50;
constexpr uint32_t LOCAL_SIGNATURE = 0x04034b50;
constexpr uint32_t EOCD_BYTES = 22;
constexpr uint32_t ZIP64_LOCATOR_BYTES = 20;
constexpr uint32_t CENTRAL_BYTES = 46;
constexpr uint32_t LOCAL_BYTES = 30;
constexpr uint32_t ZIP64_SENTINEL = 0xFFFFFFFF;

}  // namespace detail

// Walks the directory of a `fileBytes`-byte zip and calls `visit(const Entry&)` for each entry, in
// directory order, until it returns false (Stopped). `reader.read(offset, out, count)` is true when it
// filled `out` with `count` bytes at `offset`. At most PACKAGE_MEMBERS calls to `visit`, and no call
// at all for a zip whose EOCD is not sound. A directory that holds more entries than its EOCD counts is
// CountMismatch, whatever the count; TooMany is a directory the EOCD counts right that is over the limit.
template <typename Reader, typename Visit>
Status read(Reader& reader, const uint32_t fileBytes, Visit&& visit) {
  using namespace detail;
  if (fileBytes < EOCD_BYTES) return Status::Malformed;
  const uint32_t eocdAt = fileBytes - EOCD_BYTES;
  uint8_t eocd[EOCD_BYTES];
  if (!reader.read(eocdAt, eocd, sizeof(eocd))) return Status::ReadError;
  if (le32(eocd) != EOCD_SIGNATURE) return Status::Malformed;

  // ZIP64 puts a locator right before the EOCD, and sentinels in the EOCD's fields.
  if (eocdAt >= ZIP64_LOCATOR_BYTES) {
    uint8_t locator[4];
    if (!reader.read(eocdAt - ZIP64_LOCATOR_BYTES, locator, sizeof(locator))) return Status::ReadError;
    if (le32(locator) == ZIP64_LOCATOR_SIGNATURE) return Status::Unsupported;
  }
  const uint16_t total = le16(eocd + 10);
  const uint32_t directoryBytes = le32(eocd + 12);
  const uint32_t directoryAt = le32(eocd + 16);
  if (total == 0xFFFF || directoryBytes == ZIP64_SENTINEL || directoryAt == ZIP64_SENTINEL) return Status::Unsupported;

  if (le16(eocd + 4) != 0 || le16(eocd + 6) != 0 || le16(eocd + 20) != 0) return Status::Malformed;
  if (le16(eocd + 8) != total) return Status::CountMismatch;
  if (static_cast<uint64_t>(directoryAt) + directoryBytes != eocdAt) return Status::Malformed;

  uint32_t seen = 0;
  uint32_t at = directoryAt;
  while (at < eocdAt) {
    if (eocdAt - at < CENTRAL_BYTES) return Status::Malformed;
    uint8_t central[CENTRAL_BYTES];
    if (!reader.read(at, central, sizeof(central))) return Status::ReadError;
    if (le32(central) != CENTRAL_SIGNATURE) return Status::Malformed;
    const uint16_t flags = le16(central + 8);
    const uint16_t method = le16(central + 10);
    const uint32_t compressedSize = le32(central + 20);
    const uint32_t uncompressedSize = le32(central + 24);
    const uint16_t nameBytes = le16(central + 28);
    const uint32_t localAt = le32(central + 42);
    const uint64_t next =
        static_cast<uint64_t>(at) + CENTRAL_BYTES + nameBytes + le16(central + 30) + le16(central + 32);
    if (next > eocdAt) return Status::Malformed;
    // The walk ends at the entry past the EOCD's count, or past what a package may hold, whichever comes first,
    // so it visits at most PACKAGE_MEMBERS entries however the directory is built.
    if (++seen > total) return Status::CountMismatch;
    if (seen > GameCore::PACKAGE_MEMBERS) return Status::TooMany;
    const bool zip64 =
        compressedSize == ZIP64_SENTINEL || uncompressedSize == ZIP64_SENTINEL || localAt == ZIP64_SENTINEL;
    if ((flags & 1) != 0 || (method != 0 && method != 8) || zip64) return Status::Unsupported;
    if (method == 0 && compressedSize != uncompressedSize) return Status::BadSize;

    // Where the member's data sits: its local header, then the name and extra field it states.
    if (static_cast<uint64_t>(localAt) + LOCAL_BYTES > directoryAt) return Status::Malformed;
    uint8_t local[LOCAL_BYTES];
    if (!reader.read(localAt, local, sizeof(local))) return Status::ReadError;
    if (le32(local) != LOCAL_SIGNATURE) return Status::Malformed;
    const uint64_t dataAt = static_cast<uint64_t>(localAt) + LOCAL_BYTES + le16(local + 26) + le16(local + 28);
    if (dataAt + compressedSize > directoryAt) return Status::Malformed;

    Entry entry;
    const size_t kept = nameBytes < sizeof(entry.name) ? nameBytes : sizeof(entry.name) - 1;
    if (kept > 0 && !reader.read(at + CENTRAL_BYTES, entry.name, kept)) return Status::ReadError;
    entry.name[kept] = '\0';
    entry.nameUsable = nameBytes < sizeof(entry.name) && std::strlen(entry.name) == nameBytes;
    entry.crc = le32(central + 16);
    entry.uncompressedSize = uncompressedSize;
    entry.localAt = localAt;
    entry.dataEnd = static_cast<uint32_t>(dataAt + compressedSize);
    if (!visit(entry)) return Status::Stopped;
    at = static_cast<uint32_t>(next);
  }
  return seen == total ? Status::Ok : Status::CountMismatch;
}

}  // namespace ZipDirectory
