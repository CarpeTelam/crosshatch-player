#pragma once

#include <cstddef>
#include <cstdint>

#include "GamePaths.h"

// The one installer (spine AD-16): it installs every /games/*.cpgame when Games opens.
// For each file it validates the package (AD-15), extracts it to /.games-tmp/<id>/,
// converts its images to .bmp, replaces any /.games/<id>/ with the new folder, writes .pkg
// last as the commit marker, and deletes the inbox file. /.games-data/<id>/ is never touched.
// A package that is not valid is renamed <name>.cpgame.bad. All file access goes through
// Storage / HalFile. Runs on the loop task, and takes seconds for a package with images.
namespace GamePackageInstaller {

// Why one inbox file was not installed. SdCard and OutOfMemory are the card's or the device's
// fault, so the file stays in the inbox for the next try; every other reason makes the package
// invalid and renames it .bad. (Not named Storage: HalStorage.h defines that as a macro.)
enum class Error : uint8_t {
  None,
  SdCard,       // a read, write, rename, or delete on the SD card failed
  OutOfMemory,  // a buffer for the install could not be allocated
  NotAPackage,  // not a zip this device can read
  BadManifest,  // manifest.json missing, malformed, or failing Manifest::check (Invalid)
  BadMember,    // a member name off the whitelist, or the same name twice
  NoMain,       // main.lua missing
  TooManyMembers,
  BadImage,  // an image the converter refuses or fails on, a non-square icon, or images over their budget
};

// The most inbox files one installAll takes; the rest wait for the next call.
inline constexpr size_t MAX_PER_RUN = 32;

// What one run of installAll did.
struct Report {
  uint8_t installed = 0;
  uint8_t failed = 0;
  // The first failure, for the one-time notice. A package that installed but whose inbox file
  // would not delete, or an invalid one that would not rename to .bad, is a failure (SdCard).
  Error firstError = Error::None;
  char firstFile[GamePaths::INBOX_NAME_BYTES] = {};
};

// Whether /games holds at least one .cpgame file.
bool hasInbox();

// Installs every .cpgame in /games (at most MAX_PER_RUN, the rest on the next call) and
// removes a leftover /.games-tmp.
Report installAll();

// A short English phrase for logs.
const char* describe(Error error);

}  // namespace GamePackageInstaller
