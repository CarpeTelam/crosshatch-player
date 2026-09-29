#pragma once

#include <cstddef>
#include <cstdint>

// The one SHA-256 helper in src/games (spine AD-2, AD-16), and the package hash and
// .pkg bytes built on it (docs/crosshatch/formats.md). mbedTLS on the device; OpenSSL
// under SIMULATOR and in the host tests (which define GAME_HASH_OPENSSL), since the
// simulator and the host have no mbedTLS.
#if defined(SIMULATOR) || defined(GAME_HASH_OPENSSL)
struct evp_md_ctx_st;
#else
#include <mbedtls/sha256.h>
#endif

class GameHash {
 public:
  static constexpr size_t DIGEST_BYTES = 32;

  GameHash();
  ~GameHash();
  GameHash(const GameHash&) = delete;
  GameHash& operator=(const GameHash&) = delete;

  // False when the hash could not start (OpenSSL out of memory); update and finish then do nothing.
  bool ok() const { return started; }
  void update(const void* data, size_t length);
  // Writes the digest and ends the hash; false when it was not ok().
  bool finish(uint8_t (&digest)[DIGEST_BYTES]);

 private:
#if defined(SIMULATOR) || defined(GAME_HASH_OPENSSL)
  evp_md_ctx_st* context = nullptr;
#else
  mbedtls_sha256_context context;
#endif
  bool started = false;
};

// The package hash and the .pkg file (AD-16).
namespace GamePkg {

// The package hash is the first 8 bytes of a SHA-256 over the zip members sorted by name,
// each as `name \0 u32le(length) uncompressed-bytes`.
inline constexpr size_t HASH_BYTES = 8;
// .pkg: "v1\n", the hash as 16 lowercase hex digits, and "\n".
inline constexpr size_t FILE_BYTES = 3 + HASH_BYTES * 2 + 1;

// Feeds the start of one member's entry: its name, the NUL, and its length as four
// little-endian bytes. The member's bytes follow, through GameHash::update.
void hashMemberStart(GameHash& hash, const char* name, uint32_t length);

// The first HASH_BYTES bytes of a SHA-256 digest, as the package hash.
void packageHash(const uint8_t (&digest)[GameHash::DIGEST_BYTES], uint8_t (&hash)[HASH_BYTES]);

// The .pkg bytes for `hash`; not NUL-terminated.
void formatPkg(const uint8_t (&hash)[HASH_BYTES], char (&out)[FILE_BYTES]);
// Reads `length` bytes of a .pkg into `hash`; false unless they are exactly a .pkg
// (a longer file, uppercase hex, or another version is not one).
bool parsePkg(const char* bytes, size_t length, uint8_t (&hash)[HASH_BYTES]);

}  // namespace GamePkg
