#include "Manifest.h"

#include <Memory.h>

#include <cstring>

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

// A manifest's icon: lower case, digits, '_', and '-'. '-' is accepted because the
// library's names are Phosphor's own, hyphenated (game-controller); '_' stays
// accepted so no manifest that parsed before fails now. Whether the name is in the
// library is not checked here: drawGameIcon refuses an unknown name when a screen
// draws it.
bool validIcon(const std::string_view text) {
  if (text.empty() || text.size() > Manifest::MAX_ICON_BYTES) return false;
  for (const char c : text) {
    if (!isLowerDigit(c) && c != '_' && c != '-') return false;
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

// Every MANIFEST_KEYS entry is one onKey reads: a Seats entry has a seat and a
// dotted path, any other entry neither.
constexpr bool manifestKeysAreReadable() {
  for (const ManifestKey& entry : MANIFEST_KEYS) {
    const bool seats = entry.key == ManifestReader::Key::Seats;
    const bool hasSeat = entry.seat != ManifestReader::SeatKey::None;
    const bool dotted = entry.path.find('.') != std::string_view::npos;
    if (hasSeat != seats || dotted != seats) return false;
  }
  return true;
}
static_assert(manifestKeysAreReadable(), "MANIFEST_KEYS: a seat and a dotted path exactly for Key::Seats entries");

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
      return "nearby needs seats.max 2 or more";
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

constexpr uint8_t ALL_MODES = Manifest::MODE_SOLO | Manifest::MODE_PASS | Manifest::MODE_NEARBY;

// A fixed field's text; all N bytes when it has no terminator, which no rule accepts.
template <size_t N>
std::string_view fieldText(const char (&field)[N]) {
  const void* end = std::memchr(field, '\0', N);
  return std::string_view(field, end ? static_cast<size_t>(static_cast<const char*>(end) - field) : N);
}

// The rules Manifest::parse enforces, for a Manifest that did not come from it.
bool fieldsValid(const Manifest& m) {
  const std::string_view name = fieldText(m.name);
  const std::string_view icon = fieldText(m.icon);
  return validId(fieldText(m.id)) && !name.empty() && name.size() <= Manifest::MAX_NAME_BYTES &&
         fieldText(m.version).size() <= Manifest::MAX_VERSION_BYTES && (icon.empty() || validIcon(icon)) &&
         m.api >= 1 && m.seatsMin >= 1 && m.seatsMax >= m.seatsMin && m.modes != 0 && (m.modes & ~ALL_MODES) == 0;
}

CheckResult verdict(const CheckStatus status, const CheckReason reason) { return CheckResult{status, reason, 0}; }

}  // namespace

CheckResult Manifest::check(const HostCaps& host) const {
  if (!fieldsValid(*this)) return verdict(CheckStatus::Invalid, CheckReason::BadFields);
  if (hasMode(MODE_SOLO) && seatsMin != 1) return verdict(CheckStatus::Invalid, CheckReason::SoloNeedsOneSeat);
  if (hasMode(MODE_NEARBY) && seatsMax < 2) return verdict(CheckStatus::Invalid, CheckReason::NearbyNeedsTwoSeats);

  if (api < host.minApi) return verdict(CheckStatus::Unavailable, CheckReason::ApiTooOld);
  if (api > host.api) return verdict(CheckStatus::Unavailable, CheckReason::ApiTooNew);
  if (seatsMin > host.maxSeats) return verdict(CheckStatus::Unavailable, CheckReason::TooManySeats);

  // Solo and pass start whenever the rules above hold; nearby also needs the radio
  // and a second seat on this host.
  uint8_t startable = modes & (MODE_SOLO | MODE_PASS);
  if (hasMode(MODE_NEARBY) && host.nearby && host.maxSeats >= 2) startable |= MODE_NEARBY;
  if (startable == 0) return verdict(CheckStatus::Unavailable, CheckReason::NoHostMode);
  return CheckResult{CheckStatus::Ok, CheckReason::None, startable};
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
  }
  // Keys deeper than that belong to ignored values.
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
    if (value == "solo") {
      result.modes |= Manifest::MODE_SOLO;
    } else if (value == "pass") {
      result.modes |= Manifest::MODE_PASS;
    } else if (value == "nearby") {
      result.modes |= Manifest::MODE_NEARBY;
    } else {
      fail(ManifestError::BadModes);
    }
    return;
  }
  if (depth == 2 && key == Key::Seats) {
    if (!takeSeatKey()) return;
    if (seatKey != SeatKey::Unknown) fail(ManifestError::WrongType);
    seatKey = SeatKey::None;
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
  }
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
    const bool accepted = key == Key::Unknown || (key == Key::Seats && isObject) || (key == Key::Modes && !isObject);
    if (!accepted) {
      fail(ManifestError::WrongType);
      return;
    }
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
  if (wasObject != isObject || knownKeyPending || knownSeatKeyPending) {
    fail(ManifestError::Syntax);
    return;
  }
  if (depth == 0) {
    rootClosed = true;
  } else if (depth == 1) {
    key = Key::None;  // the container value of a top-level key is complete
  } else if (depth == 2 && key == Key::Seats) {
    seatKey = SeatKey::None;
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
  out = result;
  return ManifestError::None;
}

}  // namespace GameCore
