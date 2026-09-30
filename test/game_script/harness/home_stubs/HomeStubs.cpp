// The definitions behind the Home doubles (this folder), for the calls HomeActivity and CoverGridHomeUi make
// that the real libraries answer from the SD card's books and the library builder.

#include <FsHelpers.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>

#include <cctype>
#include <cstddef>
#include <string>

#include "util/BookProgress.h"

namespace FsHelpers {

namespace {
// As the real test: the extension in any case.
bool endsWith(const std::string_view name, const std::string_view suffix) {
  if (name.size() < suffix.size()) return false;
  const std::string_view tail = name.substr(name.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(tail[i])) != suffix[i]) return false;
  return true;
}
}  // namespace

bool hasEpubExtension(const std::string_view fileName) { return endsWith(fileName, ".epub"); }
bool hasXtcExtension(const std::string_view fileName) {
  return endsWith(fileName, ".xtc") || endsWith(fileName, ".xtch");
}

}  // namespace FsHelpers

// The reading percentage of a book: none saved.
int loadBookProgress(const std::string&) { return -1; }

namespace library {

const char* libraryIndexPath() { return "/.crosspoint/library.idx"; }

// The walk that builds the library index is not run: it did not produce an index.
bool buildLibraryIndex(const char*, BuildStats&, bool) { return false; }

// No library index exists on the fake card, and none is built: the cover grid fills from the recent books only.
LibraryIndexFile::~LibraryIndexFile() = default;
bool LibraryIndexFile::open(const char*) { return false; }
void LibraryIndexFile::close() {}
uint16_t LibraryIndexFile::ordinalForRow(SortOrder, uint16_t) { return 0xFFFF; }
bool LibraryIndexFile::readRecord(uint16_t, ClixRecord&) { return false; }
bool LibraryIndexFile::readName(const ClixRecord&, std::string&) { return false; }
bool LibraryIndexFile::readAuthor(const ClixRecord&, std::string&) { return false; }
bool LibraryIndexFile::readTitle(const ClixRecord&, std::string&) { return false; }
bool LibraryIndexFile::readPath(const ClixRecord&, std::string&) { return false; }

}  // namespace library
