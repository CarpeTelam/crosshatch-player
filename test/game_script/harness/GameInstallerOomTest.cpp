// GamePackageInstaller::installAll with each nothrow allocation of an install failed in turn (e4-r1, AI-15): the
// inbox file list and the Job (installAll), the zip reader's scratch (install), and the package hash (extract). Out of
// memory is the device's fault, not the package's, so the file must stay in the inbox as it is, never renamed
// .chgame.bad, and install on the next visit.
// Not swept (deferred-work.md, ## e4-r1): ZipFile::readFileToStream's and InflateStream's buffers, which are malloc
// and whose failure the installer reads as BadSize; the deflate branch, since these packages are stored zips; and the
// PNG converter, which reports its own running out of memory as a failed image (BadImage), so the package has none.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#include "GamePackageInstaller.h"
#include "GameRegistry.h"
#include "HalDisplay.h"
#include "InstallerSupport.h"

HalDisplay display;

using namespace installer_test;
using GamePackageInstaller::Error;

// The nothrow operator new and new[], which makeUniqueNoThrow calls: while armed, each one is counted, and the one
// numbered failAt (from 1) returns null. Every other allocation is malloc, as the default operator's, so delete is
// unchanged (GamesLauncherTest.cpp replaces new[] the same way). Throwing new, which gtest and std::string use, is
// not replaced.
namespace oom {
bool armed = false;
std::size_t count = 0;
std::size_t failAt = 0;  // 0: none fails

void* take(const std::size_t size) {
  if (armed && ++count == failAt) return nullptr;
  return std::malloc(size == 0 ? 1 : size);
}
}  // namespace oom

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept { return oom::take(size); }
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept { return oom::take(size); }

namespace {

const std::string INBOX_FILE = "/games/g.chgame";

// One visit with the allocation numbered `failAt` failed (0: none); oom::count says how many there were.
GamePackageInstaller::Report visitFailing(const std::size_t failAt) {
  oom::count = 0;
  oom::failAt = failAt;
  oom::armed = true;
  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
  oom::armed = false;
  return report;
}

size_t listed() {
  GameRegistry::Listing listing;
  EXPECT_TRUE(GameRegistry::load(listing));
  return listing.count;
}

}  // namespace

TEST(InstallerOomTest, EachFailedAllocationLeavesThePackageInTheInboxForTheNextVisit) {
  fakesd::reset();
  fakesd::addFile(INBOX_FILE, gamePackage("g"));
  ASSERT_EQ(visitFailing(0).installed, 1);
  const std::size_t total = oom::count;
  // names[] and the Job (installAll), the zip reader's scratch (install), and the package hash (extract).
  ASSERT_EQ(total, 4u) << "the install's nothrow allocations changed: re-read what this sweep covers (header)";

  for (std::size_t k = 1; k <= total; ++k) {
    SCOPED_TRACE("allocation " + std::to_string(k) + " of " + std::to_string(total) + " fails");
    fakesd::reset();
    fakelog::lines.clear();
    const Bytes package = gamePackage("g");
    fakesd::addFile(INBOX_FILE, package);

    const GamePackageInstaller::Report report = visitFailing(k);
    EXPECT_EQ(report.installed, 0);
    EXPECT_EQ(report.failed, 1);
    EXPECT_EQ(report.firstError, Error::OutOfMemory) << GamePackageInstaller::describe(report.firstError);
    // The first two are installAll's own (names[] and the Job), made before any file is judged, so no file is named.
    EXPECT_STREQ(report.firstFile, k <= 2 ? "" : "g.chgame");
    ASSERT_TRUE(exists(INBOX_FILE)) << "the file stays in the inbox";
    EXPECT_EQ(fakesd::bytesOf(INBOX_FILE), package);
    EXPECT_FALSE(exists(INBOX_FILE + ".bad"));
    EXPECT_EQ(childrenOf("/games").size(), 1u) << "nothing else is left in the inbox";
    EXPECT_FALSE(exists("/.games/g"));
    EXPECT_FALSE(exists("/.games-tmp/g"));
    EXPECT_EQ(listed(), 0u);
    EXPECT_TRUE(fakelog::any("OOM") || fakelog::any("out of memory"));

    // The next visit, with memory to spare, installs it.
    const GamePackageInstaller::Report next = GamePackageInstaller::installAll();
    EXPECT_EQ(next.installed, 1);
    EXPECT_EQ(next.failed, 0);
    EXPECT_TRUE(exists("/.games/g/.pkg"));
    EXPECT_FALSE(exists(INBOX_FILE));
    EXPECT_EQ(listed(), 1u);
  }
}
