// ZipDirectory, the installer's pure EOCD and central-directory reader, over zips in memory
// (entry 6 of epic-install-and-launcher).

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "InstallerSupport.h"
#include "PackageLimits.h"
#include "ZipDirectory.h"

using namespace installer_test;
using ZipDirectory::Status;

namespace {

// Bytes in memory; a read outside them is counted and fails, so a test sees any read past the file.
struct MemoryReader {
  const Bytes& bytes;
  bool failEverything = false;
  int outsideTheFile = 0;

  bool read(const uint32_t offset, void* out, const size_t count) {
    if (failEverything) return false;
    if (static_cast<uint64_t>(offset) + count > bytes.size()) {
      ++outsideTheFile;
      return false;
    }
    std::memcpy(out, bytes.data() + offset, count);
    return true;
  }
};

struct Seen {
  std::string name;
  bool usable;
  uint32_t crc;
  uint32_t size;
};

struct Result {
  Status status;
  std::vector<Seen> entries;
  int outsideTheFile;
};

Result walk(const Bytes& zip, const bool failEverything = false) {
  MemoryReader reader{zip, failEverything};
  Result result{Status::Ok, {}, 0};
  result.status = ZipDirectory::read(reader, static_cast<uint32_t>(zip.size()), [&](const ZipDirectory::Entry& entry) {
    result.entries.push_back({entry.name, entry.nameUsable, entry.crc, entry.uncompressedSize});
    return true;
  });
  result.outsideTheFile = reader.outsideTheFile;
  return result;
}

const std::vector<Member> THREE = {
    {"manifest.json", toBytes("{}")}, {"main.lua", toBytes("return {}\n")}, {"util.lua", toBytes("return 1\n")}};

// Where the EOCD's fields are: the zip's last 22 bytes.
size_t eocdAt(const Bytes& zip) { return zip.size() - 22; }

void put16At(Bytes& zip, const size_t at, const uint16_t value) {
  zip[at] = static_cast<uint8_t>(value);
  zip[at + 1] = static_cast<uint8_t>(value >> 8);
}

// The offset of the first central-directory entry.
size_t centralAt(const Bytes& zip) {
  return zip[eocdAt(zip) + 16] | zip[eocdAt(zip) + 17] << 8 | zip[eocdAt(zip) + 18] << 16;
}

}  // namespace

TEST(ZipDirectoryTest, ListsEveryEntryInDirectoryOrderWithItsCrcAndSize) {
  const Result result = walk(makeZip(THREE));
  EXPECT_EQ(result.status, Status::Ok);
  ASSERT_EQ(result.entries.size(), THREE.size());
  for (size_t i = 0; i < THREE.size(); ++i) {
    EXPECT_EQ(result.entries[i].name, THREE[i].name);
    EXPECT_TRUE(result.entries[i].usable);
    EXPECT_EQ(result.entries[i].crc, crc32(THREE[i].data));
    EXPECT_EQ(result.entries[i].size, THREE[i].data.size());
  }
}

TEST(ZipDirectoryTest, AZipWithNoEntriesIsSoundAndVisitsNothing) {
  const Result result = walk(makeZip({}));
  EXPECT_EQ(result.status, Status::Ok);
  EXPECT_TRUE(result.entries.empty());
}

TEST(ZipDirectoryTest, TheVisitorCanStopTheWalk) {
  const Bytes zip = makeZip(THREE);
  MemoryReader reader{zip};
  int visits = 0;
  const Status status = ZipDirectory::read(reader, static_cast<uint32_t>(zip.size()),
                                           [&](const ZipDirectory::Entry&) { return ++visits < 2; });
  EXPECT_EQ(status, Status::Stopped);
  EXPECT_EQ(visits, 2);
}

TEST(ZipDirectoryTest, ARequestTheReaderCannotAnswerIsAReadErrorNotBadData) {
  const Result result = walk(makeZip(THREE), true);
  EXPECT_EQ(result.status, Status::ReadError);
  EXPECT_TRUE(result.entries.empty());
}

TEST(ZipDirectoryTest, AFileTooShortForAnEocdIsMalformed) {
  for (size_t size = 0; size < 22; ++size) EXPECT_EQ(walk(Bytes(size, 0)).status, Status::Malformed) << size;
}

TEST(ZipDirectoryTest, TheEocdMustBeTheLastTwentyTwoBytes) {
  Bytes zip = makeZip(THREE);
  zip.push_back('x');  // a comment byte the EOCD does not declare
  EXPECT_EQ(walk(zip).status, Status::Malformed);
  Bytes withComment = makeZip(THREE);
  put16At(withComment, eocdAt(withComment) + 20, 1);  // declares a comment it does not have
  EXPECT_EQ(walk(withComment).status, Status::Malformed);
}

TEST(ZipDirectoryTest, TheEocdCountMustBeTheDirectorysCount) {
  const Bytes zip = makeZip(THREE);
  for (const uint16_t wrong : {0, 1, 2, 4, 32, 33, 100, 0xFFFE}) {
    Bytes total = zip;
    put16At(total, eocdAt(total) + 10, wrong);
    put16At(total, eocdAt(total) + 8, wrong);
    const Result result = walk(total);
    EXPECT_EQ(result.status, Status::CountMismatch) << wrong;
    EXPECT_LE(result.entries.size(), wrong) << "no entry past the EOCD's count is visited";
  }
  Bytes disk = zip;
  put16At(disk, eocdAt(disk) + 8, 2);  // entries on this disk, not the total
  EXPECT_EQ(walk(disk).status, Status::CountMismatch);
}

TEST(ZipDirectoryTest, MoreEntriesThanAPackageMayHoldAreRefusedAfterAtMostTheLimitAreVisited) {
  std::vector<Member> many;
  for (size_t i = 0; i <= GameCore::PACKAGE_MEMBERS; ++i) many.push_back({"f" + std::to_string(i) + ".lua", {}});
  const Result result = walk(makeZip(many));
  EXPECT_EQ(result.status, Status::TooMany);
  EXPECT_EQ(result.entries.size(), GameCore::PACKAGE_MEMBERS);
  many.pop_back();
  EXPECT_EQ(walk(makeZip(many)).status, Status::Ok);
}

TEST(ZipDirectoryTest, AnEocdCountAboveTheLimitOverAShortDirectoryIsACountMismatchNotTooMany) {
  Bytes zip = makeZip(THREE);
  put16At(zip, eocdAt(zip) + 10, 40);
  put16At(zip, eocdAt(zip) + 8, 40);
  EXPECT_EQ(walk(zip).status, Status::CountMismatch);
}

TEST(ZipDirectoryTest, ZipSixtyFourIsUnsupported) {
  Bytes locator = makeZip(THREE);
  // A ZIP64 end-of-directory locator sits right before the EOCD; here it takes the place of the last
  // 20 bytes of the directory, which is all the parser reads of it.
  const uint8_t signature[4] = {0x50, 0x4b, 0x06, 0x07};
  std::memcpy(locator.data() + eocdAt(locator) - 20, signature, 4);
  EXPECT_EQ(walk(locator).status, Status::Unsupported);

  Bytes sentinel = makeZip(THREE);
  put16At(sentinel, eocdAt(sentinel) + 10, 0xFFFF);
  put16At(sentinel, eocdAt(sentinel) + 8, 0xFFFF);
  EXPECT_EQ(walk(sentinel).status, Status::Unsupported);

  Bytes entry = makeZip(THREE);
  const size_t sizeField = centralAt(entry) + 24;  // uncompressed size of the first entry
  for (int i = 0; i < 4; ++i) entry[sizeField + i] = 0xFF;
  EXPECT_EQ(walk(entry).status, Status::Unsupported);
}

TEST(ZipDirectoryTest, OnlyStoredAndDeflatedAndUnencryptedEntriesAreSupported) {
  for (const uint16_t method : {1, 9, 12, 14, 93}) {
    Bytes zip = makeZip(THREE);
    put16At(zip, centralAt(zip) + 10, method);
    EXPECT_EQ(walk(zip).status, Status::Unsupported) << method;
  }
  Bytes deflated = makeZip(THREE);
  put16At(deflated, centralAt(deflated) + 10, 8);  // deflate is allowed (its data is then not checked here)
  EXPECT_EQ(walk(deflated).status, Status::Ok);
  Bytes encrypted = makeZip(THREE);
  put16At(encrypted, centralAt(encrypted) + 8, 1);
  EXPECT_EQ(walk(encrypted).status, Status::Unsupported);
}

TEST(ZipDirectoryTest, AStoredEntryMustHoldExactlyItsDeclaredSize) {
  Bytes zip = makeZip(THREE);
  const size_t sizeField = centralAt(zip) + 24;
  zip[sizeField] = static_cast<uint8_t>(zip[sizeField] + 5);  // declares 5 more than it holds
  EXPECT_EQ(walk(zip).status, Status::BadSize);
}

TEST(ZipDirectoryTest, TheDirectoryMustEndWhereTheEocdBegins) {
  Bytes gap = makeZip(THREE);
  gap[eocdAt(gap) + 16] = static_cast<uint8_t>(gap[eocdAt(gap) + 16] + 1);  // one byte off
  EXPECT_EQ(walk(gap).status, Status::Malformed);
  Bytes size = makeZip(THREE);
  size[eocdAt(size) + 12] = static_cast<uint8_t>(size[eocdAt(size) + 12] + 1);
  EXPECT_EQ(walk(size).status, Status::Malformed);
}

TEST(ZipDirectoryTest, ALocalHeaderThatIsMissingOrPointsOutsideIsMalformed) {
  Bytes signature = makeZip(THREE);
  signature[0] = 'X';
  EXPECT_EQ(walk(signature).status, Status::Malformed);

  Bytes offset = makeZip(THREE);
  const size_t offsetField = centralAt(offset) + 42;
  offset[offsetField + 2] = 0x10;  // the first entry's local header claimed to be 1 MB in
  const Result result = walk(offset);
  EXPECT_EQ(result.status, Status::Malformed);
  EXPECT_EQ(result.outsideTheFile, 0);

  Bytes data = makeZip(THREE);
  for (const size_t field : {centralAt(data) + 20, centralAt(data) + 24}) data[field + 1] = 0x40;  // 16 KB, both sizes
  EXPECT_EQ(walk(data).status, Status::Malformed);  // its data would run into the directory
}

TEST(ZipDirectoryTest, ANameThatCannotBeWhitelistedIsNotUsable) {
  const std::string longName(GameCore::MEMBER_NAME_BYTES, 'a');
  const std::string nul("ab\0cd.lua", 9);
  for (const std::string& name : {longName, nul, std::string(300, 'b')}) {
    const Result result = walk(makeZip({{name, {}}}));
    ASSERT_EQ(result.status, Status::Ok) << name.size();
    ASSERT_EQ(result.entries.size(), 1u);
    EXPECT_FALSE(result.entries[0].usable) << name.size();
  }
  const std::string longest(GameCore::MEMBER_NAME_BYTES - 1, 'a');
  const Result fits = walk(makeZip({{longest, {}}}));
  ASSERT_EQ(fits.entries.size(), 1u);
  EXPECT_TRUE(fits.entries[0].usable);
  EXPECT_EQ(fits.entries[0].name, longest);
}

// The parser never asks the reader for a byte outside the file, whatever the bytes say, and never visits
// more than a package may hold.
TEST(ZipDirectoryTest, NoSingleByteChangeOrTruncationMakesItReadOutsideTheFile) {
  const Bytes zip = makeZip(THREE);
  for (size_t at = 0; at < zip.size(); ++at) {
    for (const uint8_t value : {0x00, 0x01, 0x7F, 0x80, 0xFF}) {
      Bytes changed = zip;
      changed[at] = value;
      const Result result = walk(changed);
      EXPECT_EQ(result.outsideTheFile, 0) << "byte " << at << " set to " << int(value);
      EXPECT_LE(result.entries.size(), GameCore::PACKAGE_MEMBERS);
    }
  }
  for (size_t size = 0; size < zip.size(); ++size) {
    const Result result = walk(Bytes(zip.begin(), zip.begin() + size));
    EXPECT_EQ(result.outsideTheFile, 0) << "truncated to " << size;
    EXPECT_NE(result.status, Status::Ok) << "truncated to " << size;
  }
}
