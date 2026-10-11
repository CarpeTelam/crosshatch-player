#include "Manifest.h"

#include <Memory.h>

#include <cstring>

#include "ModeTable.h"

namespace GameCore {

namespace {

constexpr int MAX_INT_DIGITS = 9;  // keeps every accepted integer inside int32_t

// A JSON number that is a plain non-negative integer: digits only, no sign,
// fraction, exponent, or leading zero.
bool parseCount(const std::string_view text, int32_t& out) {
  if (text.empty() || text.size() > MAX_INT_DIGITS) return false;
  if (text.size() > 1 && text[0] == '0') return false;
  int32_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  out = value;
  return true;
}

bool isLowerDigit(const char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

bool validId(const std::string_view text) {
  if (text.empty() || text.size() > Manifest::MAX_ID_BYTES || !isLowerDigit(text[0])) return false;
  for (const char c : text) {
    if (!isLowerDigit(c) && c != '-') return false;
  }
  return true;
}

// A manifest's icon: the library's grammar, [a-z][a-z0-9]*(-[a-z0-9]+)* in at most MAX_ICON_BYTES
// (spine AD-15, AD-24): a letter first, and each '-' between lower-case letters or digits, so no "--"
// and no '-' at either end. '_' is refused. scripts/pack_game.py's ICON_NAME is the same rule. Whether
// the name is in the library is checked outside GameCore (the installer and the packer), because
// GameCore cannot depend on lib/GameIcons.
bool validIcon(const std::string_view text) {
  if (text.empty() || text.size() > Manifest::MAX_ICON_BYTES) return false;
  if (text[0] < 'a' || text[0] > 'z') return false;
  for (size_t i = 1; i < text.size(); ++i) {
    if (text[i] == '-') {
      if (i + 1 == text.size() || !isLowerDigit(text[i + 1])) return false;
    } else if (!isLowerDigit(text[i])) {
      return false;
    }
  }
  return true;
}

// icon_weight's two values, in Manifest::IconWeight order.
bool parseIconWeight(const std::string_view text, uint8_t& out) {
  if (text == "regular") {
    out = Manifest::ICON_REGULAR;
  } else if (text == "fill") {
    out = Manifest::ICON_FILL;
  } else {
    return false;
  }
  return true;
}

// default_mode's value as a Manifest::Mode bit: a mode's name (ModeTable.h).
bool parseMode(const std::string_view text, uint8_t& out) {
  const ModeRow* row = modeRowForName(text);
  if (!row) return false;
  out = row->bit;
  return true;
}

// A setting's id: [a-z][a-z0-9_]{0,15}, the API list's `name setting_id`. scripts/pack_game.py's SETTING_ID is the
// same rule.
bool validSettingId(const std::string_view text) {
  if (text.empty() || text.size() > ManifestSetting::MAX_ID_BYTES) return false;
  if (text[0] < 'a' || text[0] > 'z') return false;
  for (const char c : text) {
    if (!isLowerDigit(c) && c != '_') return false;
  }
  return true;
}

// Copies text into a fixed field; false when it does not fit.
template <size_t N>
bool copyField(char (&field)[N], const std::string_view text) {
  if (text.size() >= N) return false;
  std::memcpy(field, text.data(), text.size());
  field[text.size()] = '\0';
  return true;
}

// The first component of a dotted path ("seats" for "seats.min"), and the rest ("" for a top-level path).
std::string_view topOf(const std::string_view path) { return path.substr(0, path.find('.')); }

std::string_view innerOf(const std::string_view path) {
  const size_t dot = path.find('.');
  return dot == std::string_view::npos ? std::string_view() : path.substr(dot + 1);
}

// Every MANIFEST_KEYS entry is one onKey reads: a Seats entry has a seat and a dotted path; a
// Settings entry has a setting exactly when its path is dotted ("settings" itself has none); any
// other entry has neither.
constexpr bool manifestKeysAreReadable() {
  for (const ManifestKey& entry : MANIFEST_KEYS) {
    const bool seats = entry.key == ManifestReader::Key::Seats;
    const bool settings = entry.key == ManifestReader::Key::Settings;
    const bool hasSeat = entry.seat != ManifestReader::SeatKey::None;
    const bool hasSetting = entry.setting != ManifestReader::SettingKey::None;
    const bool dotted = entry.path.find('.') != std::string_view::npos;
    if (hasSeat != seats || (seats && !dotted)) return false;
    if (hasSetting != (settings && dotted)) return false;
    if (dotted && !seats && !settings) return false;
  }
  return true;
}
static_assert(
    manifestKeysAreReadable(),
    "MANIFEST_KEYS: a seat and a dotted path for Key::Seats entries, a setting for dotted Key::Settings ones");

// The top-level key MANIFEST_KEYS names `name`, or Unknown.
ManifestReader::Key topLevelKey(const std::string_view name) {
  for (const ManifestKey& entry : MANIFEST_KEYS) {
    if (topOf(entry.path) == name) return entry.key;
  }
  return ManifestReader::Key::Unknown;
}

// The seats key MANIFEST_KEYS names `name`, or Unknown.
ManifestReader::SeatKey seatKeyNamed(const std::string_view name) {
  for (const ManifestKey& entry : MANIFEST_KEYS) {
    if (entry.key == ManifestReader::Key::Seats && entry.seat != ManifestReader::SeatKey::None &&
        innerOf(entry.path) == name) {
      return entry.seat;
    }
  }
  return ManifestReader::SeatKey::Unknown;
}

// The settings key MANIFEST_KEYS names `name`, or Unknown.
ManifestReader::SettingKey settingKeyNamed(const std::string_view name) {
  for (const ManifestKey& entry : MANIFEST_KEYS) {
    if (entry.key == ManifestReader::Key::Settings && entry.setting != ManifestReader::SettingKey::None &&
        innerOf(entry.path) == name) {
      return entry.setting;
    }
  }
  return ManifestReader::SettingKey::Unknown;
}

ManifestReader* self(void* ctx) { return static_cast<ManifestReader*>(ctx); }

JsonCallbacks callbacksFor(ManifestReader* reader) {
  JsonCallbacks cb{};
  cb.ctx = reader;
  cb.onKey = [](void* ctx, const char* key, size_t len) { self(ctx)->onKey({key, len}); };
  cb.onString = [](void* ctx, const char* value, size_t len) { self(ctx)->onString({value, len}); };
  cb.onNumber = [](void* ctx, const char* value, size_t len) { self(ctx)->onNumber({value, len}); };
  cb.onBool = [](void* ctx, bool value) { self(ctx)->onBool(value); };
  cb.onNull = [](void* ctx) { self(ctx)->onNull(); };
  cb.onObjectStart = [](void* ctx) { self(ctx)->onContainerStart(true); };
  cb.onObjectEnd = [](void* ctx) { self(ctx)->onContainerEnd(true); };
  cb.onArrayStart = [](void* ctx) { self(ctx)->onContainerStart(false); };
  cb.onArrayEnd = [](void* ctx) { self(ctx)->onContainerEnd(false); };
  return cb;
}

}  // namespace

const char* describe(const ManifestError error) {
  switch (error) {
    case ManifestError::None:
      return "ok";
    case ManifestError::OutOfMemory:
      return "out of memory";
    case ManifestError::Syntax:
      return "not a JSON object";
    case ManifestError::DuplicateKey:
      return "duplicate key";
    case ManifestError::WrongType:
      return "key has the wrong type";
    case ManifestError::MissingKey:
      return "required key missing";
    case ManifestError::BadId:
      return "invalid id";
    case ManifestError::BadName:
      return "invalid name";
    case ManifestError::BadVersion:
      return "invalid version";
    case ManifestError::BadApi:
      return "invalid api";
    case ManifestError::BadSeats:
      return "invalid seats";
    case ManifestError::BadModes:
      return "invalid modes";
    case ManifestError::BadIcon:
      return "invalid icon";
    case ManifestError::BadIconWeight:
      return "invalid icon_weight";
    case ManifestError::BadDefaultMode:
      return "invalid default_mode";
    case ManifestError::BadSettings:
      return "invalid settings";
  }
  return "unknown error";
}

ManifestError Manifest::parse(const std::string_view json, Manifest& out) {
  auto reader = makeUniqueNoThrow<ManifestReader>();
  if (!reader) return ManifestError::OutOfMemory;
  reader->begin();
  reader->feed(json.data(), json.size());
  return reader->finish(out);
}

const char* describe(const CheckReason reason) {
  switch (reason) {
    case CheckReason::None:
      return "ok";
    case CheckReason::BadFields:
      return "invalid fields";
    case CheckReason::SoloNeedsOneSeat:
      return "solo needs seats.min 1";
    case CheckReason::NearbyNeedsTwoSeats:
      return "pass and nearby need seats.max 2 or more";
    case CheckReason::ApiTooOld:
      return "api older than this host supports";
    case CheckReason::ApiTooNew:
      return "api newer than this host supports";
    case CheckReason::TooManySeats:
      return "needs more seats than this host has";
    case CheckReason::NoHostMode:
      return "no mode this host can start";
  }
  return "unknown reason";
}

namespace {

// A fixed field's text; all N bytes when it has no terminator, which no rule accepts.
template <size_t N>
std::string_view fieldText(const char (&field)[N]) {
  const void* end = std::memchr(field, '\0', N);
  return std::string_view(field, end ? static_cast<size_t>(static_cast<const char*>(end) - field) : N);
}

// True for exactly one Mode bit.
bool oneMode(const uint8_t bits) { return modeRowForBit(bits) != nullptr; }

// The rules Manifest::parse enforces, for a Manifest that did not come from it.
bool fieldsValid(const Manifest& m) {
  const std::string_view name = fieldText(m.name);
  const std::string_view icon = fieldText(m.icon);
  return validId(fieldText(m.id)) && !name.empty() && name.size() <= Manifest::MAX_NAME_BYTES &&
         fieldText(m.version).size() <= Manifest::MAX_VERSION_BYTES && (icon.empty() || validIcon(icon)) &&
         m.iconWeight <= Manifest::ICON_FILL && m.api >= 1 && m.seatsMin >= 1 && m.seatsMax >= m.seatsMin &&
         m.modes != 0 && (m.modes & ~ALL_MODE_BITS) == 0 &&
         (m.defaultMode == 0 || (oneMode(m.defaultMode) && (m.defaultMode & m.modes) != 0)) &&
         m.settingsCount <= Manifest::MAX_SETTINGS;
}

CheckResult verdict(const CheckStatus status, const CheckReason reason) { return CheckResult{status, reason, 0}; }

}  // namespace

CheckResult Manifest::check(const HostCaps& host) const {
  if (!fieldsValid(*this)) return verdict(CheckStatus::Invalid, CheckReason::BadFields);
  if (hasMode(MODE_SOLO) && seatsMin != 1) return verdict(CheckStatus::Invalid, CheckReason::SoloNeedsOneSeat);
  // Epic pass-and-play R10 made this Invalid; a game installed before it that listed pass with one seat was inert
  // then, so with a solo mode it keeps running (e5-r7): the claim is dropped below and reported in the reason.
  const bool unseated = claimsUnseatedMode();
  if (unseated && !hasMode(MODE_SOLO)) return verdict(CheckStatus::Invalid, CheckReason::NearbyNeedsTwoSeats);

  if (api < host.minApi) return verdict(CheckStatus::Unavailable, CheckReason::ApiTooOld);
  if (api > host.api) return verdict(CheckStatus::Unavailable, CheckReason::ApiTooNew);
  if (seatsMin > host.maxSeats) return verdict(CheckStatus::Unavailable, CheckReason::TooManySeats);

  // Solo starts whenever the rules above hold; pass needs the host's pass capability (its seats
  // are passSeats' to fit when the match starts), and nearby needs the radio and a second seat.
  uint8_t startable = modes & MODE_SOLO;
  if (!unseated) {
    if (hasMode(MODE_PASS) && host.pass) startable |= MODE_PASS;
    if (hasMode(MODE_NEARBY) && host.nearby && host.maxSeats >= 2) startable |= MODE_NEARBY;
  }
  if (startable == 0) return verdict(CheckStatus::Unavailable, CheckReason::NoHostMode);
  return CheckResult{CheckStatus::Ok, unseated ? CheckReason::NearbyNeedsTwoSeats : CheckReason::None, startable};
}

uint8_t Manifest::startMode(const uint8_t remembered, const uint8_t hostModes) const {
  if (oneMode(remembered) && (remembered & hostModes) != 0) return remembered;
  if (oneMode(defaultMode) && (defaultMode & hostModes) != 0) return defaultMode;
  // The bits carry no list order, so "the first listed" is read in the table's order: solo, pass, nearby.
  for (const ModeRow& row : MODE_TABLE) {
    if ((hostModes & row.bit) != 0) return row.bit;
  }
  return 0;
}

int ManifestSetting::indexOf(const std::string_view value) const {
  for (uint8_t i = 0; i < count && i < MAX_VALUES; ++i) {
    if (fieldText(values[i]) == value) return i;
  }
  return -1;
}

int ManifestSettings::indexOf(const std::string_view id) const {
  for (uint8_t i = 0; i < count && i < MAX_SETTINGS; ++i) {
    if (fieldText(settings[i].id) == id) return i;
  }
  return -1;
}

SettingValues ManifestSettings::valuesAt(const uint8_t (&chosen)[MAX_SETTINGS]) const {
  SettingValues out;
  for (uint8_t i = 0; i < count && i < MAX_SETTINGS; ++i) {
    const ManifestSetting& setting = settings[i];
    const uint8_t index = chosen[i] < setting.count ? chosen[i] : setting.defaultIndex;
    copyField(out.entries[i].id, fieldText(setting.id));
    copyField(out.entries[i].value, fieldText(setting.values[index]));
    out.count = static_cast<uint8_t>(i + 1);
  }
  return out;
}

ManifestReader::ManifestReader() : parser(callbacksFor(this)) {}

void ManifestReader::begin() {
  parser.reset();
  result = Manifest{};
  error = ManifestError::None;
  depth = 0;
  rootSeen = false;
  rootClosed = false;
  key = Key::None;
  keyPending = false;
  seatKey = SeatKey::None;
  seatKeyPending = false;
  seen = 0;
  seatsMinSeen = false;
  seatsMaxSeen = false;
  objectBits = 0;
  parsedSettings = ManifestSettings{};
  settingKey = SettingKey::None;
  settingKeyPending = false;
  settingSeen = 0;
  settingDefault[0] = '\0';
}

void ManifestReader::feed(const char* data, const size_t len) {
  if (error != ManifestError::None) return;
  parser.feed(data, len);
}

void ManifestReader::fail(const ManifestError e) {
  if (error == ManifestError::None) error = e;
}

bool ManifestReader::acceptEvent() {
  if (error != ManifestError::None) return false;
  // Nothing may follow the root object, and nothing but an object may start it.
  if (rootClosed || depth == 0) {
    fail(ManifestError::Syntax);
    return false;
  }
  return true;
}

// The JSON parser drops a key longer than its token buffer without a callback, so
// its value arrives with no key pending. Only such a key can cause that (a string
// the parser reads where a key belongs is always reported as a key), and every
// known key is far shorter, so the value belongs to an unknown key and is ignored.
bool ManifestReader::takeTopLevelKey() {
  if (!keyPending) key = Key::Unknown;
  keyPending = false;
  return true;
}

bool ManifestReader::takeSeatKey() {
  if (!seatKeyPending) seatKey = SeatKey::Unknown;
  seatKeyPending = false;
  return true;
}

void ManifestReader::onKey(const std::string_view name) {
  if (!acceptEvent()) return;
  if (depth == 1) {
    // A pending unknown key had its value dropped by the JSON parser (a string
    // longer than its token buffer); it is ignored like any unknown key. A known
    // key without a value is malformed.
    if (keyPending && key != Key::Unknown) {
      fail(ManifestError::Syntax);
      return;
    }
    const Key k = topLevelKey(name);
    if (k != Key::Unknown) {
      const auto bit = static_cast<uint16_t>(1u << static_cast<unsigned>(k));
      if ((seen & bit) != 0) {
        fail(ManifestError::DuplicateKey);
        return;
      }
      seen |= bit;
    }
    key = k;
    keyPending = true;
    return;
  }
  if (depth == 2 && key == Key::Seats) {
    if (seatKeyPending && seatKey != SeatKey::Unknown) {
      fail(ManifestError::Syntax);
      return;
    }
    seatKey = SeatKey::Unknown;
    const SeatKey named = seatKeyNamed(name);
    if (named == SeatKey::Min) {
      if (seatsMinSeen) {
        fail(ManifestError::DuplicateKey);
        return;
      }
      seatsMinSeen = true;
      seatKey = SeatKey::Min;
    } else if (named == SeatKey::Max) {
      if (seatsMaxSeen) {
        fail(ManifestError::DuplicateKey);
        return;
      }
      seatsMaxSeen = true;
      seatKey = SeatKey::Max;
    }
    seatKeyPending = true;
    return;
  }
  if (depth == 3 && key == Key::Settings) onSettingKey(name);
  // Keys deeper than that belong to ignored values.
}

// A key inside one settings object: one of MANIFEST_KEYS' settings keys, each at most once. Unlike a
// top-level key, an unknown one makes the manifest invalid (AD-15: "anything else"), so a key the JSON
// parser dropped for length does too (its value arrives with no key pending).
void ManifestReader::onSettingKey(const std::string_view name) {
  if (settingKeyPending) {
    fail(ManifestError::Syntax);
    return;
  }
  const SettingKey named = settingKeyNamed(name);
  if (named == SettingKey::Unknown) {
    fail(ManifestError::BadSettings);
    return;
  }
  const auto bit = static_cast<uint8_t>(1u << static_cast<unsigned>(named));
  if ((settingSeen & bit) != 0) {
    fail(ManifestError::DuplicateKey);
    return;
  }
  settingSeen |= bit;
  settingKey = named;
  settingKeyPending = true;
}

// A string inside settings: a field of one setting (depth 3) or one of its values (depth 4). Any
// string elsewhere in settings is invalid.
void ManifestReader::onSettingString(const std::string_view value) {
  ManifestSetting& setting = parsedSettings.settings[parsedSettings.count - 1];
  if (depth == 4) {
    // Only a values array reaches depth 4 (onContainerStart).
    const bool fits = !value.empty() && value.size() <= ManifestSetting::MAX_VALUE_BYTES;
    if (!fits || setting.count == ManifestSetting::MAX_VALUES || setting.indexOf(value) >= 0 ||
        !copyField(setting.values[setting.count], value)) {
      fail(ManifestError::BadSettings);
      return;
    }
    ++setting.count;
    return;
  }
  if (depth != 3 || !settingKeyPending) {
    fail(ManifestError::BadSettings);
    return;
  }
  settingKeyPending = false;
  bool ok = false;
  switch (settingKey) {
    case SettingKey::Id:
      // Unique among the settings before it, which are complete.
      ok = validSettingId(value) && parsedSettings.indexOf(value) < 0 && copyField(setting.id, value);
      break;
    case SettingKey::Name:
      ok = !value.empty() && value.size() <= ManifestSetting::MAX_NAME_BYTES && copyField(setting.name, value);
      break;
    case SettingKey::Default:
      // Resolved against the values when the setting's object closes.
      ok = !value.empty() && copyField(settingDefault, value);
      break;
    default:  // values must be an array
      break;
  }
  settingKey = SettingKey::None;
  if (!ok) fail(ManifestError::BadSettings);
}

// One settings object has closed: it needs an id, a name, and 2 to 6 values, and its default (when
// given) is one of them.
void ManifestReader::finishSetting() {
  ManifestSetting& setting = parsedSettings.settings[parsedSettings.count - 1];
  const auto has = [this](const SettingKey k) { return (settingSeen & (1u << static_cast<unsigned>(k))) != 0; };
  if (!has(SettingKey::Id) || !has(SettingKey::Name) || !has(SettingKey::Values) ||
      setting.count < ManifestSetting::MIN_VALUES) {
    fail(ManifestError::BadSettings);
    return;
  }
  if (has(SettingKey::Default)) {
    const int index = setting.indexOf(fieldText(settingDefault));
    if (index < 0) {
      fail(ManifestError::BadSettings);
      return;
    }
    setting.defaultIndex = static_cast<uint8_t>(index);
  }
  settingKey = SettingKey::None;
  settingSeen = 0;
  settingDefault[0] = '\0';
}

void ManifestReader::onString(const std::string_view value) {
  if (!acceptEvent()) return;
  if (depth == 1) {
    if (!takeTopLevelKey()) return;
    switch (key) {
      case Key::Id:
        if (!validId(value) || !copyField(result.id, value)) fail(ManifestError::BadId);
        break;
      case Key::Name:
        if (value.empty() || !copyField(result.name, value)) fail(ManifestError::BadName);
        break;
      case Key::Version:
        if (!copyField(result.version, value)) fail(ManifestError::BadVersion);
        break;
      case Key::Icon:
        if (!validIcon(value) || !copyField(result.icon, value)) fail(ManifestError::BadIcon);
        break;
      case Key::IconWeight:
        if (!parseIconWeight(value, result.iconWeight)) fail(ManifestError::BadIconWeight);
        break;
      case Key::DefaultMode:
        // Checked against modes in finish(), so the two keys may come in either order.
        if (!parseMode(value, result.defaultMode)) fail(ManifestError::BadDefaultMode);
        break;
      case Key::Unknown:
        break;
      default:
        fail(ManifestError::WrongType);
        break;
    }
    key = Key::None;
    return;
  }
  if (depth == 2 && key == Key::Modes) {
    const ModeRow* row = modeRowForName(value);
    if (row) {
      result.modes |= row->bit;
    } else {
      fail(ManifestError::BadModes);
    }
    return;
  }
  if (depth == 2 && key == Key::Seats) {
    if (!takeSeatKey()) return;
    if (seatKey != SeatKey::Unknown) fail(ManifestError::WrongType);
    seatKey = SeatKey::None;
    return;
  }
  if (depth >= 2 && key == Key::Settings) {
    // A string straight in the settings array is no setting.
    if (depth == 2) {
      fail(ManifestError::BadSettings);
      return;
    }
    onSettingString(value);
  }
}

void ManifestReader::onNumber(const std::string_view value) {
  if (!acceptEvent()) return;
  if (depth == 1) {
    if (!takeTopLevelKey()) return;
    if (key == Key::Api) {
      if (!parseCount(value, result.api) || result.api < 1) fail(ManifestError::BadApi);
    } else if (key != Key::Unknown) {
      fail(ManifestError::WrongType);
    }
    key = Key::None;
    return;
  }
  if (depth == 2 && key == Key::Modes) {
    fail(ManifestError::BadModes);
    return;
  }
  if (depth == 2 && key == Key::Seats) {
    if (!takeSeatKey()) return;
    if (seatKey == SeatKey::Min && !parseCount(value, result.seatsMin)) fail(ManifestError::BadSeats);
    if (seatKey == SeatKey::Max && !parseCount(value, result.seatsMax)) fail(ManifestError::BadSeats);
    seatKey = SeatKey::None;
    return;
  }
  // Settings hold only objects, strings, and the values arrays.
  if (depth >= 2 && key == Key::Settings) fail(ManifestError::BadSettings);
}

void ManifestReader::onBool(const bool value) {
  if (!acceptEvent()) return;
  if (depth == 1) {
    if (!takeTopLevelKey()) return;
    if (key == Key::Hidden) {
      result.hidden = value;
    } else if (key != Key::Unknown) {
      fail(ManifestError::WrongType);
    }
    key = Key::None;
    return;
  }
  onOtherScalar();
}

void ManifestReader::onNull() {
  if (!acceptEvent()) return;
  if (depth == 1) {
    if (!takeTopLevelKey()) return;
    if (key != Key::Unknown) fail(ManifestError::WrongType);
    key = Key::None;
    return;
  }
  onOtherScalar();
}

void ManifestReader::onOtherScalar() {
  // A boolean or null below the top level.
  if (depth == 2 && key == Key::Modes) {
    fail(ManifestError::BadModes);
    return;
  }
  if (key == Key::Settings) {
    fail(ManifestError::BadSettings);
    return;
  }
  if (depth == 2 && key == Key::Seats) {
    if (!takeSeatKey()) return;
    if (seatKey != SeatKey::Unknown) fail(ManifestError::WrongType);
    seatKey = SeatKey::None;
  }
}

void ManifestReader::onContainerStart(const bool isObject) {
  if (error != ManifestError::None) return;
  if (depth == 0) {
    if (rootSeen || !isObject) {
      fail(ManifestError::Syntax);
      return;
    }
    rootSeen = true;
  } else if (depth == 1) {
    if (!takeTopLevelKey()) return;
    const bool accepted = key == Key::Unknown || (key == Key::Seats && isObject) ||
                          ((key == Key::Modes || key == Key::Settings) && !isObject);
    if (!accepted) {
      fail(ManifestError::WrongType);
      return;
    }
  } else if (depth == 2 && key == Key::Settings) {
    // One setting: an object, at most MAX_SETTINGS of them.
    if (!isObject || parsedSettings.count == ManifestSettings::MAX_SETTINGS) {
      fail(ManifestError::BadSettings);
      return;
    }
    ++parsedSettings.count;
    settingKey = SettingKey::None;
    settingKeyPending = false;
    settingSeen = 0;
    settingDefault[0] = '\0';
  } else if (depth == 3 && key == Key::Settings) {
    // Only values holds a container: an array of strings.
    if (!settingKeyPending || settingKey != SettingKey::Values || isObject) {
      fail(ManifestError::BadSettings);
      return;
    }
    settingKeyPending = false;
  } else if (depth == 4 && key == Key::Settings) {
    fail(ManifestError::BadSettings);
    return;
  } else if (depth == 2 && key == Key::Modes) {
    fail(ManifestError::BadModes);
    return;
  } else if (depth == 2 && key == Key::Seats) {
    if (!takeSeatKey()) return;
    if (seatKey != SeatKey::Unknown) {
      fail(ManifestError::WrongType);
      return;
    }
    // seatKey stays Unknown until this container closes.
  }
  if (depth >= 32) {
    fail(ManifestError::Syntax);
    return;
  }
  if (isObject) objectBits |= 1u << depth;
  ++depth;
}

void ManifestReader::onContainerEnd(const bool isObject) {
  if (error != ManifestError::None) return;
  if (depth == 0) {
    fail(ManifestError::Syntax);
    return;
  }
  --depth;
  const bool wasObject = (objectBits & (1u << depth)) != 0;
  objectBits &= ~(1u << depth);
  // A key waiting for its value when its object closes, or a bracket that does not
  // match its opener, is malformed.
  const bool knownKeyPending = depth == 0 && keyPending && key != Key::Unknown;
  const bool knownSeatKeyPending = depth == 1 && key == Key::Seats && seatKeyPending && seatKey != SeatKey::Unknown;
  const bool settingKeyLeftPending = depth == 2 && key == Key::Settings && settingKeyPending;
  if (wasObject != isObject || knownKeyPending || knownSeatKeyPending || settingKeyLeftPending) {
    fail(ManifestError::Syntax);
    return;
  }
  if (depth == 0) {
    rootClosed = true;
  } else if (depth == 1) {
    key = Key::None;  // the container value of a top-level key is complete
  } else if (depth == 2 && key == Key::Seats) {
    seatKey = SeatKey::None;
  } else if (depth == 2 && key == Key::Settings) {
    finishSetting();  // one setting's object
  } else if (depth == 3 && key == Key::Settings) {
    settingKey = SettingKey::None;  // its values array
  }
}

ManifestError ManifestReader::finish(Manifest& out) {
  if (error != ManifestError::None) return error;
  if (parser.hasError() || !rootClosed) return ManifestError::Syntax;
  const auto has = [this](const Key k) { return (seen & (1u << static_cast<unsigned>(k))) != 0; };
  if (!has(Key::Id) || !has(Key::Name) || !has(Key::Version) || !has(Key::Api) || !has(Key::Seats) ||
      !has(Key::Modes)) {
    return ManifestError::MissingKey;
  }
  if (!seatsMinSeen || !seatsMaxSeen || result.seatsMin < 1 || result.seatsMax < result.seatsMin) {
    return ManifestError::BadSeats;
  }
  if (result.modes == 0) return ManifestError::BadModes;
  if (has(Key::DefaultMode) && (result.defaultMode & result.modes) == 0) return ManifestError::BadDefaultMode;
  result.settingsCount = parsedSettings.count;
  out = result;
  return ManifestError::None;
}

}  // namespace GameCore
