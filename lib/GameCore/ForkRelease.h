#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

// Where builds with games look for firmware updates, what the release assets are
// called, and how a fork build number is read and compared.
//
// A fork version is <upstream X.Y.Z>-ch.<N>, for example 1.6.5-ch.7. Tags follow
//   ^(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)-ch[.]([1-9][0-9]{0,8})$
// and are at most MAX_TAG_LEN characters. N strictly increases across releases
// and never resets when the upstream base changes, so N alone decides whether a
// release is newer. A running build has N only when it starts with a tag followed
// by the end of the string, '-', or '+'; every other build (development builds
// included) has N = 0, so it is offered any release.
//
// test/game_core/fork_version_vectors.json fixes this behaviour. It is data only
// so that the fork release workflow can check its tags against the same file;
// change the two together.
//
// Pure on purpose: no device headers, so the host suite tests the code the
// firmware runs.
namespace ForkRelease {

inline constexpr char LATEST_RELEASE_URL[] =
    "https://api.github.com/repos/CarpeTelam/crosshatch-player/releases/latest";

// Also keeps "crosspoint-<tag>-sticky.bin" within the 48-byte asset-name buffers
// of the update path.
inline constexpr size_t MAX_TAG_LEN = 25;

namespace detail {

inline constexpr std::string_view BUILD_MARKER = "-ch.";
inline constexpr size_t MAX_BUILD_DIGITS = 9;

constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Length of the digit run starting at pos (0 when none).
constexpr size_t digitRun(std::string_view s, size_t pos) {
  size_t end = pos;
  while (end < s.size() && isDigit(s[end])) ++end;
  return end - pos;
}

// Length of an X, Y, or Z component at pos: "0" or digits without a leading
// zero. 0 when there is none; a leading zero followed by more digits is none.
constexpr size_t componentLen(std::string_view s, size_t pos) {
  const size_t run = digitRun(s, pos);
  if (run == 0 || (s[pos] == '0' && run > 1)) return 0;
  return run;
}

struct TagMatch {
  size_t length;  // 0 when no prefix of the string is a tag
  uint32_t buildNumber;
};

// Matches the tag grammar against the start of s. N's digit run is taken whole,
// so the character after a match is never a digit.
constexpr TagMatch matchTagPrefix(std::string_view s) {
  constexpr TagMatch none{0, 0};
  size_t pos = 0;
  for (int component = 0; component < 3; ++component) {
    const size_t len = componentLen(s, pos);
    if (len == 0) return none;
    pos += len;
    if (component < 2) {
      if (pos >= s.size() || s[pos] != '.') return none;
      ++pos;
    }
  }
  if (!s.substr(pos).starts_with(BUILD_MARKER)) return none;
  pos += BUILD_MARKER.size();

  const size_t digits = digitRun(s, pos);
  if (digits == 0 || digits > MAX_BUILD_DIGITS || s[pos] == '0') return none;
  uint32_t n = 0;
  for (size_t i = 0; i < digits; ++i) n = n * 10 + static_cast<uint32_t>(s[pos + i] - '0');
  pos += digits;

  if (pos > MAX_TAG_LEN) return none;
  return TagMatch{pos, n};
}

}  // namespace detail

// N of a release tag, or 0 when the whole string is not a fork tag.
constexpr uint32_t tagBuildNumber(std::string_view tag) {
  const detail::TagMatch match = detail::matchTagPrefix(tag);
  return match.length != 0 && match.length == tag.size() ? match.buildNumber : 0;
}

// N of the running firmware's version string, or 0 (development and upstream
// builds, and anything else that does not start with a fork tag).
constexpr uint32_t runningBuildNumber(std::string_view version) {
  const detail::TagMatch match = detail::matchTagPrefix(version);
  if (match.length == 0) return 0;
  if (match.length == version.size()) return match.buildNumber;
  const char next = version[match.length];
  return next == '-' || next == '+' ? match.buildNumber : 0;
}

// True when latestTag is a fork tag whose N is larger than the running build's.
constexpr bool isNewer(std::string_view latestTag, std::string_view runningVersion) {
  return tagBuildNumber(latestTag) > runningBuildNumber(runningVersion);
}

// Writes "crosspoint-<tag>-<board>.bin" and a NUL into out. Returns false and
// leaves out empty (when outSize > 0) if tag is not a fork tag, board is empty,
// or the name does not fit.
constexpr bool formatAssetName(char* out, size_t outSize, std::string_view tag, std::string_view board) {
  constexpr std::string_view prefix = "crosspoint-";
  constexpr std::string_view suffix = ".bin";
  if (outSize == 0) return false;
  out[0] = '\0';
  if (tagBuildNumber(tag) == 0 || board.empty()) return false;
  const size_t length = prefix.size() + tag.size() + 1 + board.size() + suffix.size();
  if (length >= outSize) return false;

  size_t pos = 0;
  for (const std::string_view part : {prefix, tag, std::string_view("-"), board, suffix}) {
    for (const char c : part) out[pos++] = c;
  }
  out[pos] = '\0';
  return true;
}

}  // namespace ForkRelease
