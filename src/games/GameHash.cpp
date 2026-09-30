#if FREEINK_CAP_GAMES

#include "GameHash.h"

#include <cstring>

#if defined(SIMULATOR) || defined(GAME_HASH_OPENSSL)
#include <openssl/evp.h>
#endif

#if defined(SIMULATOR) || defined(GAME_HASH_OPENSSL)

GameHash::GameHash() {
  context = EVP_MD_CTX_new();
  started = context != nullptr && EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
}

GameHash::~GameHash() { EVP_MD_CTX_free(context); }

void GameHash::update(const void* data, const size_t length) {
  if (started) EVP_DigestUpdate(context, data, length);
}

bool GameHash::finish(uint8_t (&digest)[DIGEST_BYTES]) {
  if (!started) return false;
  started = false;
  unsigned int written = 0;
  return EVP_DigestFinal_ex(context, digest, &written) == 1 && written == DIGEST_BYTES;
}

#else

GameHash::GameHash() {
  mbedtls_sha256_init(&context);
  started = mbedtls_sha256_starts(&context, /*is224=*/0) == 0;
}

GameHash::~GameHash() { mbedtls_sha256_free(&context); }

void GameHash::update(const void* data, const size_t length) {
  if (started) mbedtls_sha256_update(&context, static_cast<const unsigned char*>(data), length);
}

bool GameHash::finish(uint8_t (&digest)[DIGEST_BYTES]) {
  if (!started) return false;
  started = false;
  return mbedtls_sha256_finish(&context, digest) == 0;
}

#endif

namespace GamePkg {

void hashMemberStart(GameHash& hash, const char* name, const uint32_t length) {
  const uint8_t terminator = 0;
  const uint8_t size[4] = {static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8),
                           static_cast<uint8_t>(length >> 16), static_cast<uint8_t>(length >> 24)};
  hash.update(name, std::strlen(name));
  hash.update(&terminator, 1);
  hash.update(size, sizeof(size));
}

void packageHash(const uint8_t (&digest)[GameHash::DIGEST_BYTES], uint8_t (&hash)[HASH_BYTES]) {
  std::memcpy(hash, digest, HASH_BYTES);
}

namespace {
constexpr char HEX[] = "0123456789abcdef";
constexpr char VERSION_LINE[] = "v1\n";
constexpr size_t VERSION_BYTES = sizeof(VERSION_LINE) - 1;

// The value of a lowercase hex digit, or -1.
int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
}  // namespace

void formatPkg(const uint8_t (&hash)[HASH_BYTES], char (&out)[FILE_BYTES]) {
  std::memcpy(out, VERSION_LINE, VERSION_BYTES);
  for (size_t i = 0; i < HASH_BYTES; ++i) {
    out[VERSION_BYTES + i * 2] = HEX[hash[i] >> 4];
    out[VERSION_BYTES + i * 2 + 1] = HEX[hash[i] & 0x0F];
  }
  out[FILE_BYTES - 1] = '\n';
}

bool parsePkg(const char* bytes, const size_t length, uint8_t (&hash)[HASH_BYTES]) {
  if (length != FILE_BYTES || std::memcmp(bytes, VERSION_LINE, VERSION_BYTES) != 0 || bytes[FILE_BYTES - 1] != '\n') {
    return false;
  }
  uint8_t parsed[HASH_BYTES];
  for (size_t i = 0; i < HASH_BYTES; ++i) {
    const int high = hexValue(bytes[VERSION_BYTES + i * 2]);
    const int low = hexValue(bytes[VERSION_BYTES + i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    parsed[i] = static_cast<uint8_t>(high << 4 | low);
  }
  std::memcpy(hash, parsed, HASH_BYTES);
  return true;
}

}  // namespace GamePkg

#endif  // FREEINK_CAP_GAMES
