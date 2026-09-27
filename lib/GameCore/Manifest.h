#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "HostCaps.h"

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

// Manifest::check's verdict (AD-15). Invalid: the manifest breaks its own rules,
// so its package is rejected. Unavailable: well-formed, but this host cannot
// start it. Ok: at least one mode can start here.
enum class CheckStatus : uint8_t { Ok, Invalid, Unavailable };

enum class CheckReason : uint8_t {
  None,
  BadFields,            // Invalid: a field breaks a parse rule (not from a successful parse)
  SoloNeedsOneSeat,     // Invalid: solo with seats.min other than 1
  NearbyNeedsTwoSeats,  // Invalid: nearby with seats.max below 2
  ApiTooOld,            // Unavailable: api below the host's minApi
  ApiTooNew,            // Unavailable: api above the host's api
  TooManySeats,         // Unavailable: seats.min above the host's maxSeats
  NoHostMode,           // Unavailable: no declared mode can start on this host
};

const char* describe(CheckReason reason);

struct CheckResult {
  CheckStatus status = CheckStatus::Invalid;
  CheckReason reason = CheckReason::BadFields;
  uint8_t modes = 0;  // Manifest::Mode bits this host can start; 0 unless Ok

  bool ok() const { return status == CheckStatus::Ok; }
};

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

  // Whether this host can start the game, reading only the manifest and `host`.
  CheckResult check(const HostCaps& host) const;
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
  // Consume the pending top-level or seats key for a value; with none pending (a key
  // the JSON parser dropped for length) the value is taken as an unknown key's.
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
