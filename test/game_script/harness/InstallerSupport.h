#pragma once

// What the installer suite builds packages and checks them with: a stored-zip writer, a
// grayscale PNG writer, an independent package hash (the formula of docs/crosshatch/formats.md,
// through OpenSSL's one-shot SHA-256 and not the code under test), and the golden vector.

#include <gtest/gtest.h>
#include <openssl/evp.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "HalStorage.h"
#include "Logging.h"

namespace installer_test {

using Bytes = std::vector<uint8_t>;

inline Bytes toBytes(const std::string& text) { return Bytes(text.begin(), text.end()); }
inline std::string toText(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

inline Bytes readHostFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << path;
  return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline uint32_t crc32(const Bytes& data) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const uint8_t byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

inline void put16(Bytes& out, const uint32_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}
inline void put32(Bytes& out, const uint32_t value) {
  put16(out, value & 0xFFFFu);
  put16(out, value >> 16);
}
inline void putBE32(Bytes& out, const uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

struct Member {
  std::string name;
  Bytes data;
};

// A zip of stored members, in the order given, with correct CRCs.
inline Bytes makeZip(const std::vector<Member>& members) {
  Bytes zip;
  Bytes central;
  for (const Member& member : members) {
    const uint32_t offset = static_cast<uint32_t>(zip.size());
    const uint32_t crc = crc32(member.data);
    for (Bytes* out : {&zip, &central}) {
      const bool isCentral = out == &central;
      put32(*out, isCentral ? 0x02014b50u : 0x04034b50u);
      if (isCentral) put16(*out, 20);  // version made by
      put16(*out, 20);                 // version needed
      put16(*out, 0);                  // flags
      put16(*out, 0);                  // stored
      put16(*out, 0);                  // time
      put16(*out, 0x21);               // date: 1980-01-01
      put32(*out, crc);
      put32(*out, static_cast<uint32_t>(member.data.size()));
      put32(*out, static_cast<uint32_t>(member.data.size()));
      put16(*out, static_cast<uint32_t>(member.name.size()));
      put16(*out, 0);  // extra
      if (isCentral) {
        put16(*out, 0);  // comment
        put16(*out, 0);  // disk
        put16(*out, 0);  // internal attributes
        put32(*out, 0);  // external attributes
        put32(*out, offset);
      }
      out->insert(out->end(), member.name.begin(), member.name.end());
      if (!isCentral) out->insert(out->end(), member.data.begin(), member.data.end());
    }
  }
  const uint32_t centralOffset = static_cast<uint32_t>(zip.size());
  zip.insert(zip.end(), central.begin(), central.end());
  put32(zip, 0x06054b50u);
  put16(zip, 0);
  put16(zip, 0);
  put16(zip, static_cast<uint32_t>(members.size()));
  put16(zip, static_cast<uint32_t>(members.size()));
  put32(zip, static_cast<uint32_t>(central.size()));
  put32(zip, centralOffset);
  put16(zip, 0);
  return zip;
}

// An 8-bit grayscale PNG (zlib stored blocks), `gray(x, y)` the value of a pixel. `interlace`
// only sets the header's flag: the converter refuses on it before it reads any pixel.
inline Bytes makePng(const int width, const int height, const std::function<uint8_t(int, int)>& gray,
                     const bool interlace = false) {
  Bytes raw;
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < width; ++x) raw.push_back(gray(x, y));
  }
  Bytes zlib = {0x78, 0x01};
  size_t at = 0;
  bool last = false;
  while (!last) {
    const size_t n = std::min<size_t>(raw.size() - at, 65535);
    last = at + n >= raw.size();
    zlib.push_back(last ? 1 : 0);
    put16(zlib, static_cast<uint32_t>(n));
    put16(zlib, static_cast<uint32_t>(~n) & 0xFFFFu);
    zlib.insert(zlib.end(), raw.begin() + at, raw.begin() + at + n);
    at += n;
  }
  uint32_t a = 1;
  uint32_t b = 0;
  for (const uint8_t byte : raw) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  putBE32(zlib, b << 16 | a);

  Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  const auto chunk = [&png](const char* type, const Bytes& data) {
    putBE32(png, static_cast<uint32_t>(data.size()));
    Bytes body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    putBE32(png, crc32(body));
  };
  Bytes ihdr;
  putBE32(ihdr, static_cast<uint32_t>(width));
  putBE32(ihdr, static_cast<uint32_t>(height));
  ihdr.insert(ihdr.end(), {8, 0, 0, 0, static_cast<uint8_t>(interlace ? 1 : 0)});
  chunk("IHDR", ihdr);
  chunk("IDAT", zlib);
  chunk("IEND", {});
  return png;
}

inline Bytes solidPng(const int width, const int height, const uint8_t value) {
  return makePng(width, height, [value](int, int) { return value; });
}

// The package hash of docs/crosshatch/formats.md: the first 8 bytes of a SHA-256 over the
// members sorted by name, each as `name \0 u32le(length) bytes`.
inline Bytes packageDigest(std::vector<Member> members) {
  std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) { return a.name < b.name; });
  Bytes input;
  for (const Member& member : members) {
    input.insert(input.end(), member.name.begin(), member.name.end());
    input.push_back(0);
    put32(input, static_cast<uint32_t>(member.data.size()));
    input.insert(input.end(), member.data.begin(), member.data.end());
  }
  Bytes digest(EVP_MAX_MD_SIZE);
  unsigned int length = 0;
  EXPECT_EQ(EVP_Digest(input.data(), input.size(), digest.data(), &length, EVP_sha256(), nullptr), 1);
  digest.resize(length);
  return digest;
}

inline std::string hex(const Bytes& bytes, const size_t count) {
  std::string out;
  char pair[3];
  for (size_t i = 0; i < count && i < bytes.size(); ++i) {
    std::snprintf(pair, sizeof(pair), "%02x", bytes[i]);
    out += pair;
  }
  return out;
}

// A JSON string value from the vector file, unescaped (\n, \", \\ are all it holds), read
// after the first occurrence of `key` at or after `from`.
inline std::string jsonString(const std::string& json, const std::string& key, size_t from = 0) {
  const size_t keyAt = json.find("\"" + key + "\"", from);
  EXPECT_NE(keyAt, std::string::npos) << key;
  size_t at = json.find('"', json.find(':', keyAt)) + 1;
  std::string out;
  for (; at < json.size() && json[at] != '"'; ++at) {
    if (json[at] != '\\') {
      out += json[at];
      continue;
    }
    ++at;
    out += json[at] == 'n' ? '\n' : json[at];
  }
  return out;
}

// entry 2's golden vector: test/game_core/package_vectors.json and package_vector.chgame.
struct Vector {
  std::string id;
  std::string packageHash;  // 16 hex digits
  std::string sha256;
  std::vector<Member> members;
  Bytes package;
};

inline Vector loadVector() {
  const std::string dir = PACKAGE_VECTOR_DIR;
  const Bytes file = readHostFile(dir + "/package_vectors.json");
  const std::string json = toText(file);
  const size_t vector = json.find("\"hash_vector\"");
  Vector out;
  out.id = jsonString(json, "id", vector);
  out.packageHash = jsonString(json, "package_hash", vector);
  out.sha256 = jsonString(json, "sha256", vector);
  const size_t members = json.find("\"members\"", vector);
  for (const char* name : {"manifest.json", "main.lua", "util.lua"}) {
    out.members.push_back({name, toBytes(jsonString(json, name, members))});
  }
  out.package = readHostFile(dir + "/" + jsonString(json, "package", vector));
  return out;
}

// A manifest.json for a game that can start solo.
inline std::string manifestJson(const std::string& id, const std::string& name = "Test Game", const int api = 1,
                                const std::string& seats = R"({"min": 1, "max": 1})",
                                const std::string& modes = R"(["solo"])") {
  return "{\"id\": \"" + id + "\", \"name\": \"" + name + "\", \"version\": \"1\", \"api\": " + std::to_string(api) +
         ", \"seats\": " + seats + ", \"modes\": " + modes + "}";
}

// A package of a manifest, a main.lua, and `extra` members.
inline Bytes gamePackage(const std::string& id, const std::vector<Member>& extra = {},
                         const std::string& manifest = "") {
  std::vector<Member> members = {{"manifest.json", toBytes(manifest.empty() ? manifestJson(id) : manifest)},
                                 {"main.lua", toBytes("return {}\n")}};
  members.insert(members.end(), extra.begin(), extra.end());
  return makeZip(members);
}

inline bool exists(const std::string& path) { return fakesd::has(path); }

// The paths of a folder's direct children, sorted.
inline std::vector<std::string> childrenOf(const std::string& dir) {
  std::vector<std::string> out;
  for (const auto& entry : fakesd::sim().entries) {
    if (!entry.dead && fakesd::parentOf(entry.path) == dir) out.push_back(entry.path);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// Where `prefix` first starts an operation the card recorded, or -1.
inline int opIndex(const std::string& prefix) {
  const auto& ops = fakesd::sim().ops;
  for (size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].compare(0, prefix.size(), prefix) == 0) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace installer_test
