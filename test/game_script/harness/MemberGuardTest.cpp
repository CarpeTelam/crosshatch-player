// MemberGuard, what the installer checks of a member's bytes as they stream out of the zip, driven chunk by
// chunk (entry 6 of epic-install-and-launcher). Through lib/ZipFile the cap never fires, so this is the only place
// it is pinned.

#include <gtest/gtest.h>

#include <string>

#include "InstallerSupport.h"
#include "MemberGuard.h"

using namespace installer_test;

namespace {

bool admit(MemberGuard& guard, const std::string& chunk) {
  return guard.admit(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
}

}  // namespace

TEST(MemberGuardTest, AdmitsExactlyTheDeclaredSizeInAnyChunksAndKeepsItsCrc) {
  const std::string text = "return { name = 'guard' }\n";
  MemberGuard guard(static_cast<uint32_t>(text.size()), true);
  EXPECT_TRUE(admit(guard, text.substr(0, 7)));
  EXPECT_TRUE(admit(guard, ""));
  EXPECT_TRUE(admit(guard, text.substr(7)));
  EXPECT_EQ(guard.total, text.size());
  EXPECT_EQ(guard.crc, crc32(toBytes(text)));
  EXPECT_FALSE(guard.overrun);
  EXPECT_FALSE(guard.binary);
}

TEST(MemberGuardTest, RefusesTheChunkThatPassesTheDeclaredSizeAndCountsNothingOfIt) {
  MemberGuard guard(10, false);
  EXPECT_TRUE(admit(guard, "12345678"));
  EXPECT_FALSE(admit(guard, "9012"));  // 12 bytes in all
  EXPECT_TRUE(guard.overrun);
  EXPECT_EQ(guard.total, 8u);
  EXPECT_EQ(guard.crc, crc32(toBytes("12345678")));
  EXPECT_TRUE(admit(guard, "90")) << "a chunk that still fits is admitted; it is the caller's business to stop";
  EXPECT_EQ(guard.total, 10u);
}

TEST(MemberGuardTest, AnEmptyWriteChangesNothingEvenWithANullPointer) {
  MemberGuard guard(8, true);
  EXPECT_TRUE(admit(guard, "-- ab"));
  const uint32_t crc = guard.crc;
  EXPECT_TRUE(guard.admit(nullptr, 0));
  EXPECT_EQ(guard.crc, crc);
  EXPECT_EQ(guard.total, 5u);
}

TEST(MemberGuardTest, AMemberDeclaringNothingAdmitsNothing) {
  MemberGuard guard(0, false);
  EXPECT_TRUE(admit(guard, ""));
  EXPECT_FALSE(admit(guard, "x"));
  EXPECT_TRUE(guard.overrun);
}

TEST(MemberGuardTest, AllowsTheSizeExactlyAtTheLimitOfAnUnsignedCount) {
  MemberGuard guard(0xFFFFFFFFu, false);
  const std::string chunk(1024, 'a');
  EXPECT_TRUE(admit(guard, chunk));
  EXPECT_EQ(guard.total, 1024u);
  EXPECT_FALSE(guard.overrun);
}

TEST(MemberGuardTest, ALuaMemberThatStartsWithTheBytecodeSignatureIsRefusedWithoutCountingIt) {
  MemberGuard guard(20, true);
  EXPECT_FALSE(admit(guard, std::string("\x1bLua", 4)));
  EXPECT_TRUE(guard.binary);
  EXPECT_FALSE(guard.overrun);
  EXPECT_EQ(guard.total, 0u);
}

TEST(MemberGuardTest, TheSignatureMattersOnlyAsTheFirstByteOfALuaMember) {
  MemberGuard later(64, true);
  EXPECT_TRUE(admit(later, "-- color: "));
  EXPECT_TRUE(admit(later, "\x1b[0m\n"));  // the first byte of a later chunk
  EXPECT_FALSE(later.binary);

  MemberGuard notLua(8, false);
  EXPECT_TRUE(admit(notLua,
                    "\x1b"
                    "abc"));  // a manifest or a picture is not judged by it
  EXPECT_FALSE(notLua.binary);

  MemberGuard empty(4, true);
  EXPECT_TRUE(admit(empty, ""));  // an empty first chunk has no first byte
  EXPECT_TRUE(admit(empty, "-- x"));
  EXPECT_FALSE(empty.binary);
}
