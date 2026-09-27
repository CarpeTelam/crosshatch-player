#pragma once

#include <cstddef>
#include <cstdint>

// The header every persisted codec blob starts with (AD-10): four magic bytes naming
// the file, the file's layout version, and the codec version of what follows. A blob
// with an unknown version is discarded with a log line, never decoded. The layout is
// in docs/crosshatch/formats.md; game_codec.py writes and checks the same bytes.
namespace GameScript {

inline constexpr size_t BLOB_MAGIC_BYTES = 4;
inline constexpr size_t BLOB_HEADER_BYTES = BLOB_MAGIC_BYTES + 2;

enum class BlobHeaderStatus : uint8_t { Ok, Truncated, BadMagic, UnknownFileVersion, UnknownCodecVersion };

// The snake_case name the vector file uses.
const char* blobHeaderStatusName(BlobHeaderStatus status);

// Writes `magic` (its first BLOB_MAGIC_BYTES bytes), `fileVersion`, and
// Codec::VERSION to out[0..BLOB_HEADER_BYTES).
void writeBlobHeader(uint8_t* out, const char* magic, uint8_t fileVersion);

// Checks, in this order, that data holds a whole header, that it names `magic`,
// that its file version is `fileVersion`, and that its codec version is
// Codec::VERSION. On Ok the payload starts at data + BLOB_HEADER_BYTES.
BlobHeaderStatus checkBlobHeader(const uint8_t* data, size_t length, const char* magic, uint8_t fileVersion);

}  // namespace GameScript
