#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GameCore {

// Why a manifest.json did not parse. describe() gives a short English phrase for logs.
enum class ManifestError : uint8_t {
  None,
  OutOfMemory,
  Syntax,        // not one JSON object, truncated, or a key without a value
  DuplicateKey,  // a known key appears twice
  WrongType,     // a known key holds the wrong JSON type
  MissingKey,    // id, name, version, api, seats, or modes is absent
  BadId,
  BadName,
  BadVersion,
  BadApi,
  BadSeats,
  BadModes,
  BadIcon,
};

const char* describe(ManifestError error);

// One game's manifest.json (AD-15). Fixed-size fields so a list of games needs no
// per-string allocation; the text caps below are part of the manifest's rules.
struct Manifest {
  static constexpr size_t MAX_ID_BYTES = 32;       // ^[a-z0-9][a-z0-9-]{0,31}$
  static constexpr size_t MAX_NAME_BYTES = 64;     // bytes of UTF-8, at least 1
  static constexpr size_t MAX_VERSION_BYTES = 32;  // bytes, any text
  static constexpr size_t MAX_ICON_BYTES = 32;     // [a-z0-9_]{1,32}, a library icon name

  enum Mode : uint8_t { MODE_SOLO = 1 << 0, MODE_PASS = 1 << 1, MODE_NEARBY = 1 << 2 };

  char id[MAX_ID_BYTES + 1] = {};
  char name[MAX_NAME_BYTES + 1] = {};
  char version[MAX_VERSION_BYTES + 1] = {};
  char icon[MAX_ICON_BYTES + 1] = {};  // empty when the key is absent
  int32_t api = 0;
  int32_t seatsMin = 0;
  int32_t seatsMax = 0;
  uint8_t modes = 0;  // Mode bits, at least one
  bool hidden = false;

  bool hasMode(const Mode mode) const { return (modes & mode) != 0; }

  // The one manifest parser: fills `out` from a whole manifest.json. Unknown keys,
  // at any depth, are ignored. On failure `out` is unspecified.
  static ManifestError parse(std::string_view json, Manifest& out);
};

// Streaming form of Manifest::parse for callers that read the file in chunks:
// begin(), feed() any number of times, then finish(). Holds the JSON parser's
// token buffer and a Manifest (about 800 B), so allocate it on the heap.
class ManifestReader {
 public:
  ManifestReader();
  ManifestReader(const ManifestReader&) = delete;
  ManifestReader& operator=(const ManifestReader&) = delete;

  void begin();
  void feed(const char* data, size_t len);
  ManifestError finish(Manifest& out);

  // JSON callback targets; public so the parser's C-style callbacks can reach them.
  void onKey(std::string_view key);
  void onString(std::string_view value);
  void onNumber(std::string_view value);
  void onBool(bool value);
  void onNull();
  void onContainerStart(bool isObject);
  void onContainerEnd(bool isObject);

 private:
  enum class Key : uint8_t { None, Id, Name, Version, Api, Seats, Modes, Hidden, Icon, Unknown };
  enum class SeatKey : uint8_t { None, Min, Max, Unknown };

  void fail(ManifestError error);
  // False (and fails the parse) for an event outside the root object.
  bool acceptEvent();
  // Consume the pending top-level or seats key for a value; false when none is pending.
  bool takeTopLevelKey();
  bool takeSeatKey();
  // A boolean or null below the top level.
  void onOtherScalar();

  StreamingJsonParser parser;
  Manifest result;
  ManifestError error = ManifestError::None;
  int depth = 0;
  bool rootSeen = false;
  bool rootClosed = false;
  Key key = Key::None;  // top-level key awaiting or holding its value
  bool keyPending = false;
  SeatKey seatKey = SeatKey::None;
  bool seatKeyPending = false;
  uint16_t seen = 0;  // bit per Key
  bool seatsMinSeen = false;
  bool seatsMaxSeen = false;
  uint32_t objectBits = 0;  // bit d set when the container at depth d+1 is an object
};

}  // namespace GameCore
