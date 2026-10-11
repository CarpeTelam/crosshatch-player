#pragma once

#include <string_view>

// The two file-type tests HomeActivity makes (lib/FsHelpers/FsHelpers.h, whose real header needs Arduino's
// String): an EPUB, text, or Markdown book (the reflowable ones Epub parses), and an XTC. Defined in HomeStubs.cpp.
namespace FsHelpers {
bool hasReflowableBookExtension(std::string_view fileName);
bool hasXtcExtension(std::string_view fileName);
}  // namespace FsHelpers
