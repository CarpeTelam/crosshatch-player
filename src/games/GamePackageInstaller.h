#pragma once

#include <cstddef>
#include <cstdint>

#include "GamePaths.h"

// The one installer (spine AD-16): it installs every /games/*.cpgame when Games opens.
// For each file it validates the package (AD-15), extracts it to /.games-tmp/<id>/,
// converts its images to .bmp, replaces any /.games/<id>/ with the new folder, writes .pkg
// last as the commit marker, and deletes the inbox file. Validation comes first and touches no card
// folder: the file's size, then the zip's directory (ZipDirectory), then each member as it streams
// (its declared size as a cap, no bytecode, its CRC). /.games-data/<id>/ is never touched.
// A package that is not valid is renamed <name>.cpgame.bad. An installed package whose inbox file will not delete is
// renamed <name>.cpgame.installed instead, so it does not install again on every visit. All file access goes through
// Storage / HalFile. Runs on the loop task, and takes seconds for a package with images.
namespace GamePackageInstaller {

// Why one inbox file was not installed. SdCard and OutOfMemory are the card's or the device's
// fault, so the file stays in the inbox for the next try; every other reason makes the package
// invalid and renames it .bad (TooManyGames is neither: the file waits). (Not named Storage: HalStorage.h defines that
// as a macro.)
enum class Error : uint8_t {
  None,
  SdCard,       // a read, write, rename, or delete on the SD card failed
  OutOfMemory,  // a buffer for the install could not be allocated
  NotAPackage,  // not a zip this device can read
  BadManifest,  // manifest.json missing, malformed, or failing Manifest::check (Invalid)
  BadMember,    // a member name off the whitelist, or the same name twice
  NoMain,       // main.lua missing
  TooManyMembers,
  BadImage,  // an image the converter refuses or fails on, or a non-square icon
  // The hardening rejections (AD-15), each with its own reason: every one is the package's fault.
  PackageTooBig,  // the file is over PACKAGE_BYTES
  MemberTooBig,   // a member declares more than MEMBER_BYTES uncompressed
  ImagesTooBig,   // the converted images pass IMAGES_BYTES or MAX_IMAGES
  BadSize,        // a member streams more, fewer, or other bytes than the directory declares
  BadCrc,         // a member's bytes do not match its CRC-32
  BinaryLua,      // a .lua member starts with Lua's bytecode signature
  Unsupported,    // ZIP64, encryption, or a compression method other than stored and deflate
  BadDirectory,   // the EOCD's entry count differs from the directory's, or two members share bytes of the file
  SourcesTooBig,  // the .lua members together pass LUA_SOURCES_BYTES, more than GameAssets::load accepts
  UnknownIcon,    // the manifest's icon is well formed but no name in the game icon library (R9)
  // Not the package's fault, so the file stays in the inbox (like SdCard): GameRegistry lists MAX_GAMES games, and
  // this install would make one more. Replacing an installed game of the same id is allowed.
  TooManyGames,
};

// The most inbox files one installAll judges; the rest wait for the next call. A package that waits for room
// (TooManyGames) does not count, so it cannot keep the files behind it from being reached.
inline constexpr size_t MAX_PER_RUN = 32;

// What one run of installAll did.
struct Report {
  uint8_t installed = 0;
  uint8_t failed = 0;  // stops at 255
  // The first failure, for the one-time notice. A package that installed but whose inbox file would neither delete
  // nor rename aside, or an invalid one that would not rename to .bad, is a failure (SdCard).
  Error firstError = Error::None;
  char firstFile[GamePaths::INBOX_NAME_BYTES] = {};
};

// Whether /games holds at least one .cpgame file.
bool hasInbox();

// Installs every .cpgame in /games (at most MAX_PER_RUN, the rest on the next call) and
// removes a leftover /.games-tmp.
Report installAll();

// Removes an installed game: deletes /.games/<id>/. The .pkg goes first, because removeDir deletes in
// directory order and a stop partway would otherwise leave a listed game with files missing; a
// folder without a .pkg is not a game (GameRegistry). Results:
//   None       the folder is gone, or /.games can be opened and holds no such folder;
//   SdCard     a delete failed (the game is then listed whole, or not listed at all, never half listed);
//              /.games cannot be opened; or the folder has no .pkg and may share clusters with
//              /.games-tmp/<id> (an interrupted folder move, found by the installer's probe), in which
//              case nothing is deleted;
//   BadManifest  an id no manifest could carry (nothing is touched).
// Never touches /.games-data/<id>/ (the saved data outlives the game, and a reinstall finds it) or
// /.games-tmp, except for the probe's own file there.
Error remove(const char* id);

// A short English phrase for logs.
const char* describe(Error error);

}  // namespace GamePackageInstaller
