// GameHash (the one SHA-256 helper, over OpenSSL on the host) and the package hash and .pkg
// bytes built on it, against entry 2's golden vector, which scripts/pack_game.py also passes.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "GameHash.h"
#include "InstallerSupport.h"

using namespace installer_test;

namespace {

std::string sha256Hex(const std::string& text) {
  GameHash hash;
  EXPECT_TRUE(hash.ok());
  hash.update(text.data(), text.size());
  uint8_t digest[GameHash::DIGEST_BYTES];
  EXPECT_TRUE(hash.finish(digest));
  return hex(Bytes(digest, digest + sizeof(digest)), sizeof(digest));
}

}  // namespace

TEST(GameHash, MatchesTheFipsVectors) {
  EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(GameHash, ChunksOfAnySizeGiveOneDigest) {
  const std::string text(1000, 'x');
  GameHash whole;
  whole.update(text.data(), text.size());
  uint8_t expected[GameHash::DIGEST_BYTES];
  ASSERT_TRUE(whole.finish(expected));
  GameHash pieces;
  for (size_t at = 0; at < text.size(); at += 7) pieces.update(text.data() + at, std::min<size_t>(7, text.size() - at));
  uint8_t actual[GameHash::DIGEST_BYTES];
  ASSERT_TRUE(pieces.finish(actual));
  EXPECT_EQ(0, std::memcmp(expected, actual, sizeof(actual)));
}

TEST(GameHash, FinishEndsTheHash) {
  GameHash hash;
  uint8_t digest[GameHash::DIGEST_BYTES];
  EXPECT_TRUE(hash.finish(digest));
  EXPECT_FALSE(hash.ok());
  EXPECT_FALSE(hash.finish(digest));
}

TEST(PackageHash, TheVectorHashesToItsRecordedValue) {
  const Vector vector = loadVector();
  // The independent formula agrees with the recorded digest (what pack_game.py computes) ...
  const Bytes expected = packageDigest(vector.members);
  ASSERT_EQ(hex(expected, expected.size()), vector.sha256);

  // ... and so does the firmware's helper, fed as the installer feeds it: members sorted by name.
  GameHash hash;
  std::vector<Member> sorted = vector.members;
  std::sort(sorted.begin(), sorted.end(), [](const Member& a, const Member& b) { return a.name < b.name; });
  for (const Member& member : sorted) {
    GamePkg::hashMemberStart(hash, member.name.c_str(), static_cast<uint32_t>(member.data.size()));
    hash.update(member.data.data(), member.data.size());
  }
  uint8_t digest[GameHash::DIGEST_BYTES];
  ASSERT_TRUE(hash.finish(digest));
  EXPECT_EQ(hex(Bytes(digest, digest + sizeof(digest)), sizeof(digest)), vector.sha256);

  uint8_t packageHash[GamePkg::HASH_BYTES];
  GamePkg::packageHash(digest, packageHash);
  EXPECT_EQ(hex(Bytes(packageHash, packageHash + sizeof(packageHash)), sizeof(packageHash)), vector.packageHash);
}

TEST(PackageHash, MemberLengthIsFourLittleEndianBytes) {
  // name, NUL, u32le(0x01020304), no bytes: the digest of exactly those bytes.
  GameHash hash;
  GamePkg::hashMemberStart(hash, "a.lua", 0x01020304u);
  uint8_t digest[GameHash::DIGEST_BYTES];
  ASSERT_TRUE(hash.finish(digest));
  EXPECT_EQ(hex(Bytes(digest, digest + sizeof(digest)), sizeof(digest)),
            sha256Hex(std::string("a.lua\0\x04\x03\x02\x01", 10)));
}

TEST(PkgFile, HoldsTheVersionLineTheHexAndANewline) {
  const uint8_t hash[GamePkg::HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1};
  char bytes[GamePkg::FILE_BYTES];
  GamePkg::formatPkg(hash, bytes);
  EXPECT_EQ(std::string(bytes, sizeof(bytes)), "v1\n0530a15766e91bf1\n");
  uint8_t parsed[GamePkg::HASH_BYTES] = {};
  ASSERT_TRUE(GamePkg::parsePkg(bytes, sizeof(bytes), parsed));
  EXPECT_EQ(0, std::memcmp(hash, parsed, sizeof(hash)));
}

TEST(PkgFile, ParseRefusesAnythingElse) {
  const auto parses = [](const std::string& text) {
    uint8_t hash[GamePkg::HASH_BYTES];
    return GamePkg::parsePkg(text.data(), text.size(), hash);
  };
  EXPECT_TRUE(parses("v1\n0530a15766e91bf1\n"));
  EXPECT_FALSE(parses("v1\n0530a15766e91bf1"));      // no newline
  EXPECT_FALSE(parses("v1\n0530a15766e91bf1\n\n"));  // longer
  EXPECT_FALSE(parses("v2\n0530a15766e91bf1\n"));    // another version
  EXPECT_FALSE(parses("v1\n0530A15766e91bf1\n"));    // uppercase hex
  EXPECT_FALSE(parses("v1\n0530a15766e91bfg\n"));    // not hex
  EXPECT_FALSE(parses("v1\n0530a15766e91b\n"));      // short
  EXPECT_FALSE(parses("v1 0530a15766e91bf1\n"));     // no newline after the version
  EXPECT_FALSE(parses(""));
}

TEST(PkgFile, ARefusalLeavesTheCallersHashAlone) {
  uint8_t hash[GamePkg::HASH_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8};
  const std::string text = "v1\n0530a15766e91bfg\n";
  EXPECT_FALSE(GamePkg::parsePkg(text.data(), text.size(), hash));
  EXPECT_EQ(hash[0], 1);
  EXPECT_EQ(hash[7], 8);
}
