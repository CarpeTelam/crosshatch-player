#pragma once

#include <string_view>

// The two file-type tests HomeActivity makes (lib/FsHelpers/FsHelpers.h, whose real header needs Arduino's
// String). Defined in HomeStubs.cpp.
namespace FsHelpers {
bool hasEpubExtension(std::string_view fileName);
bool hasXtcExtension(std::string_view fileName);
}  // namespace FsHelpers
