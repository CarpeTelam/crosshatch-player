#include "BlobHeader.h"

#include <cstring>

#include "Codec.h"

namespace GameScript {

const char* blobHeaderStatusName(const BlobHeaderStatus status) {
  switch (status) {
    case BlobHeaderStatus::Ok:
      return "ok";
    case BlobHeaderStatus::Truncated:
      return "truncated";
    case BlobHeaderStatus::BadMagic:
      return "bad_magic";
    case BlobHeaderStatus::UnknownFileVersion:
      return "unknown_file_version";
    case BlobHeaderStatus::UnknownCodecVersion:
      return "unknown_codec_version";
  }
  return "unknown";
}

void writeBlobHeader(uint8_t* out, const char* magic, const uint8_t fileVersion) {
  std::memcpy(out, magic, BLOB_MAGIC_BYTES);
  out[BLOB_MAGIC_BYTES] = fileVersion;
  out[BLOB_MAGIC_BYTES + 1] = Codec::VERSION;
}

BlobHeaderStatus checkBlobHeader(const uint8_t* data, const size_t length, const char* magic,
                                 const uint8_t fileVersion) {
  if (length < BLOB_HEADER_BYTES) return BlobHeaderStatus::Truncated;
  if (std::memcmp(data, magic, BLOB_MAGIC_BYTES) != 0) return BlobHeaderStatus::BadMagic;
  if (data[BLOB_MAGIC_BYTES] != fileVersion) return BlobHeaderStatus::UnknownFileVersion;
  if (data[BLOB_MAGIC_BYTES + 1] != Codec::VERSION) return BlobHeaderStatus::UnknownCodecVersion;
  return BlobHeaderStatus::Ok;
}

}  // namespace GameScript
