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
  BadIconWeight,
  BadDefaultMode,  // default_mode is not solo, pass, or nearby, or not one of the manifest's modes
  BadSettings,     // settings breaks one of its rules (Manifest::MAX_SETTINGS and ManifestSetting's limits)
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
  NearbyNeedsTwoSeats,  // Invalid: pass or nearby with seats.max below 2
  ApiTooOld,            // Unavailable: api below the host's minApi
  ApiTooNew,            // Unavailable: api above the host's api
  TooManySeats,         // Unavailable: seats.min above the host's maxSeats
  NoHostMode,           // Unavailable: no declared mode can start on this host (pass needs HostCaps::pass)
};

const char* describe(CheckReason reason);

struct CheckResult {
  CheckStatus status = CheckStatus::Invalid;
  CheckReason reason = CheckReason::BadFields;
  uint8_t modes = 0;  // Manifest::Mode bits this host can start; 0 unless Ok

  bool ok() const { return status == CheckStatus::Ok; }
};

// One setting a manifest declares (AD-15, as amended 2026-10-02): an id, the name the Options
// screen shows, two to six values, and the index of the default one. Fixed-size fields, like
// Manifest's; the caps below are part of the manifest's rules.
struct ManifestSetting {
  static constexpr size_t MAX_ID_BYTES = 16;     // ^[a-z][a-z0-9_]{0,15}$
  static constexpr size_t MAX_NAME_BYTES = 24;   // bytes of UTF-8, at least 1
  static constexpr size_t MAX_VALUE_BYTES = 16;  // bytes of UTF-8, at least 1
  static constexpr size_t MIN_VALUES = 2;
  static constexpr size_t MAX_VALUES = 6;

  char id[MAX_ID_BYTES + 1] = {};
  char name[MAX_NAME_BYTES + 1] = {};
  char values[MAX_VALUES][MAX_VALUE_BYTES + 1] = {};
  uint8_t count = 0;         // values, MIN_VALUES to MAX_VALUES
  uint8_t defaultIndex = 0;  // the default's index in values; 0 when the key is absent

  // The index of `value` in values, or -1.
  int indexOf(std::string_view value) const;
};

// The chosen value of each declared setting, by id: what ctx.settings gives a game (AD-8) and
// prefs.bin remembers (AD-17). Strings, as the manifest spells them.
struct SettingValues {
  static constexpr size_t MAX_SETTINGS = 4;  // Manifest::MAX_SETTINGS

  struct Entry {
    char id[ManifestSetting::MAX_ID_BYTES + 1] = {};
    char value[ManifestSetting::MAX_VALUE_BYTES + 1] = {};
  };
  Entry entries[MAX_SETTINGS] = {};
  uint8_t count = 0;
};

// The settings a manifest declares, in its order; ManifestReader holds them, since the registry
// keeps only their count (Manifest::settingsCount).
struct ManifestSettings {
  static constexpr size_t MAX_SETTINGS = SettingValues::MAX_SETTINGS;

  ManifestSetting settings[MAX_SETTINGS] = {};
  uint8_t count = 0;

  // The index of the setting with `id`, or -1.
  int indexOf(std::string_view id) const;
  // Each setting's id and the value at `chosen[i]` (its index in setting i's values; one out of
  // range takes the default), in manifest order.
  SettingValues valuesAt(const uint8_t (&chosen)[MAX_SETTINGS]) const;
};

// One game's manifest.json (AD-15). Fixed-size fields so a list of games needs no
// per-string allocation; the text caps below are part of the manifest's rules.
struct Manifest {
  static constexpr size_t MAX_ID_BYTES = 32;       // ^[a-z0-9][a-z0-9-]{0,31}$
  static constexpr size_t MAX_NAME_BYTES = 64;     // bytes of UTF-8, at least 1
  static constexpr size_t MAX_VERSION_BYTES = 32;  // bytes, any text
  // [a-z][a-z0-9]*(-[a-z0-9]+)* in at most 32 bytes: the library's own grammar (spine AD-15, AD-24)
  static constexpr size_t MAX_ICON_BYTES = 32;
  // The most settings a manifest declares (AD-15, as amended 2026-10-02).
  static constexpr size_t MAX_SETTINGS = ManifestSettings::MAX_SETTINGS;

  enum Mode : uint8_t { MODE_SOLO = 1 << 0, MODE_PASS = 1 << 1, MODE_NEARBY = 1 << 2 };
  // The manifest's icon_weight, in the order of ch.gfx.icon's weight names.
  enum IconWeight : uint8_t { ICON_REGULAR, ICON_FILL };

  char id[MAX_ID_BYTES + 1] = {};
  char name[MAX_NAME_BYTES + 1] = {};
  char version[MAX_VERSION_BYTES + 1] = {};
  char icon[MAX_ICON_BYTES + 1] = {};  // empty when the key is absent
  uint8_t iconWeight = ICON_REGULAR;   // an IconWeight; regular when the key is absent
  int32_t api = 0;
  int32_t seatsMin = 0;
  int32_t seatsMax = 0;
  uint8_t modes = 0;  // Mode bits, at least one
  bool hidden = false;
  uint8_t defaultMode = 0;    // the Mode bit default_mode names, one of modes; 0 when the key is absent
  uint8_t settingsCount = 0;  // the settings the manifest declares, at most MAX_SETTINGS (ManifestReader::settings)

  bool hasMode(const Mode mode) const { return (modes & mode) != 0; }

  // The mode New game starts (AD-15, AD-22, as amended 2026-10-02): `remembered` (a Mode bit,
  // 0 for none) when `hostModes` (CheckResult::modes) holds it, else defaultMode when it does,
  // else the first of solo, pass, and nearby it holds; 0 when it holds none. Pure.
  uint8_t startMode(uint8_t remembered, uint8_t hostModes) const;

  // The one manifest parser: fills `out` from a whole manifest.json. Unknown keys,
  // at any depth, are ignored. On failure `out` is unspecified.
  static ManifestError parse(std::string_view json, Manifest& out);

  // Whether this host can start the game, reading only the manifest and `host`.
  CheckResult check(const HostCaps& host) const;
};

// Streaming form of Manifest::parse for callers that read the file in chunks:
// begin(), feed() any number of times, then finish(). Holds the JSON parser's
// token buffer, a Manifest, and the settings (about 1.4 KB), so allocate it on the heap.
class ManifestReader {
 public:
  // The top-level keys the parser reads, the keys inside seats, and the keys inside each
  // settings object. MANIFEST_KEYS names each one; the Unknown values stand for any other key.
  enum class Key : uint8_t {
    None,
    Id,
    Name,
    Version,
    Api,
    Seats,
    Modes,
    Hidden,
    Icon,
    IconWeight,
    DefaultMode,
    Settings,
    Unknown
  };
  enum class SeatKey : uint8_t { None, Min, Max, Unknown };
  enum class SettingKey : uint8_t { None, Id, Name, Values, Default, Unknown };

  ManifestReader();
  ManifestReader(const ManifestReader&) = delete;
  ManifestReader& operator=(const ManifestReader&) = delete;

  void begin();
  void feed(const char* data, size_t len);
  ManifestError finish(Manifest& out);
  // The settings of the manifest finish() last accepted (Manifest::settingsCount of them);
  // unspecified after a failed parse.
  const ManifestSettings& settings() const { return parsedSettings; }

  // JSON callback targets; public so the parser's C-style callbacks can reach them.
  void onKey(std::string_view key);
  void onString(std::string_view value);
  void onNumber(std::string_view value);
  void onBool(bool value);
  void onNull();
  void onContainerStart(bool isObject);
  void onContainerEnd(bool isObject);

 private:
  void fail(ManifestError error);
  // False (and fails the parse) for an event outside the root object.
  bool acceptEvent();
  // Consume the pending top-level or seats key for a value; with none pending (a key
  // the JSON parser dropped for length) the value is taken as an unknown key's.
  bool takeTopLevelKey();
  bool takeSeatKey();
  // A boolean or null below the top level.
  void onOtherScalar();
  // Inside settings (depth 2 and below): a key, a string, and the end of one setting's object.
  void onSettingKey(std::string_view name);
  void onSettingString(std::string_view value);
  void finishSetting();

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
  // The settings read so far, and the one being read: its pending key, the keys it has had (bit
  // per SettingKey), and its default until its values are known (key order is free).
  ManifestSettings parsedSettings;
  SettingKey settingKey = SettingKey::None;
  bool settingKeyPending = false;
  uint8_t settingSeen = 0;
  char settingDefault[ManifestSetting::MAX_VALUE_BYTES + 1] = {};
};

// One manifest.json key the parser reads, by its dotted path as the API level list
// spells it (docs/crosshatch/api-level-<n>.txt `manifest` entries): `key` is its
// top-level key, `seat` its key inside seats and `setting` its key inside each settings
// object (None for a top-level value).
struct ManifestKey {
  std::string_view path;
  ManifestReader::Key key;
  ManifestReader::SeatKey seat;
  ManifestReader::SettingKey setting = ManifestReader::SettingKey::None;
};

// Every key the parser reads; ManifestReader::onKey looks names up here, and
// ApiLevelTest compares it with the list both ways.
inline constexpr ManifestKey MANIFEST_KEYS[] = {
    {"id", ManifestReader::Key::Id, ManifestReader::SeatKey::None},
    {"name", ManifestReader::Key::Name, ManifestReader::SeatKey::None},
    {"version", ManifestReader::Key::Version, ManifestReader::SeatKey::None},
    {"api", ManifestReader::Key::Api, ManifestReader::SeatKey::None},
    {"seats.min", ManifestReader::Key::Seats, ManifestReader::SeatKey::Min},
    {"seats.max", ManifestReader::Key::Seats, ManifestReader::SeatKey::Max},
    {"modes", ManifestReader::Key::Modes, ManifestReader::SeatKey::None},
    {"hidden", ManifestReader::Key::Hidden, ManifestReader::SeatKey::None},
    {"icon", ManifestReader::Key::Icon, ManifestReader::SeatKey::None},
    {"icon_weight", ManifestReader::Key::IconWeight, ManifestReader::SeatKey::None},
    {"default_mode", ManifestReader::Key::DefaultMode, ManifestReader::SeatKey::None},
    {"settings", ManifestReader::Key::Settings, ManifestReader::SeatKey::None},
    {"settings.id", ManifestReader::Key::Settings, ManifestReader::SeatKey::None, ManifestReader::SettingKey::Id},
    {"settings.name", ManifestReader::Key::Settings, ManifestReader::SeatKey::None, ManifestReader::SettingKey::Name},
    {"settings.values", ManifestReader::Key::Settings, ManifestReader::SeatKey::None,
     ManifestReader::SettingKey::Values},
    {"settings.default", ManifestReader::Key::Settings, ManifestReader::SeatKey::None,
     ManifestReader::SettingKey::Default},
};

}  // namespace GameCore
