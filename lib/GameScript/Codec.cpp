#include "Codec.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <lua.hpp>

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");
static_assert(sizeof(lua_Number) == 8, "codec floats are IEEE-754 binary64");
static_assert(GameScript::Codec::STORE_LIMIT <= GameScript::Codec::MAX_LIMIT, "pair offsets are 16-bit");
static_assert(GameScript::Codec::SNAPSHOT_LIMIT <= GameScript::Codec::MAX_LIMIT, "pair offsets are 16-bit");

namespace GameScript::Codec {

namespace {

// One byte before every value (docs/crosshatch/formats.md).
enum class Tag : uint8_t { Nil = 0, False = 1, True = 2, Int = 3, Float = 4, String = 5, Table = 6 };
constexpr uint8_t TAG_COUNT = 7;

// The one NaN the codec writes, so equal states give equal bytes on every platform.
constexpr uint64_t CANONICAL_NAN = 0x7FF8000000000000ull;
// A 64-bit varint takes at most ten bytes; the tenth holds only bit 63.
constexpr size_t MAX_VARINT_BYTES = 10;
// The smallest record pair: a one-byte integer or empty string key and a one-byte value.
constexpr size_t MIN_PAIR_BYTES = 3;
constexpr double TWO_POW_63 = 9223372036854775808.0;

uint64_t zigzag(const int64_t value) {
  return (static_cast<uint64_t>(value) << 1) ^ static_cast<uint64_t>(value >> 63);
}

int64_t unzigzag(const uint64_t value) { return static_cast<int64_t>((value >> 1) ^ (0 - (value & 1))); }

// A table key as the codec orders it: integers ascending, then strings bytewise.
struct Key {
  bool isString = false;
  int64_t integer = 0;
  const char* text = nullptr;
  size_t length = 0;
};

bool keyLess(const Key& a, const Key& b) {
  if (a.isString != b.isString) return !a.isString;
  if (!a.isString) return a.integer < b.integer;
  const int order = std::memcmp(a.text, b.text, std::min(a.length, b.length));
  return order != 0 ? order < 0 : a.length < b.length;
}

// Integral floats inside int64 become integer keys (AD-10); Lua already does this for
// its own tables, so from Lua only non-integral and out-of-range floats arrive here.
Error readKey(lua_State* L, const int index, Key& key) {
  switch (lua_type(L, index)) {
    case LUA_TNUMBER: {
      if (lua_isinteger(L, index)) {
        key.integer = lua_tointeger(L, index);
        return Error::None;
      }
      const lua_Number number = lua_tonumber(L, index);
      if (std::isnan(number)) return Error::NanKey;
      if (!(number >= -TWO_POW_63 && number < TWO_POW_63) || std::floor(number) != number) return Error::FloatKey;
      key.integer = static_cast<int64_t>(number);
      return Error::None;
    }
    case LUA_TSTRING:
      key.isString = true;
      key.text = lua_tolstring(L, index, &key.length);
      return Error::None;
    default:  // boolean, table, function, userdata, thread
      return Error::BadKey;
  }
}

bool inArrayPart(const Key& key, const lua_Integer narr) {
  return !key.isString && key.integer >= 1 && key.integer <= narr;
}

// Reads a varint from bytes the encoder itself wrote.
uint64_t readOwnVarint(const uint8_t*& at) {
  uint64_t value = 0;
  for (int shift = 0;; shift += 7) {
    const uint8_t byte = *at++;
    value |= static_cast<uint64_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) return value;
  }
}

// The key at the start of a record pair the encoder wrote.
Key keyAt(const uint8_t* at) {
  Key key;
  key.isString = *at++ == static_cast<uint8_t>(Tag::String);
  const uint64_t value = readOwnVarint(at);
  if (key.isString) {
    key.text = reinterpret_cast<const char*>(at);
    key.length = static_cast<size_t>(value);
  } else {
    key.integer = unzigzag(value);
  }
  return key;
}

// Depth-first writer. The first failure sticks: later writes do nothing, and each
// loop stops once ok() is false. Every Lua push is popped on every path.
class Encoder {
 public:
  Encoder(lua_State* L, PairSpan* pairs, const size_t pairCapacity, uint8_t* out, uint8_t* sortArea, const size_t limit)
      : L(L), pairs(pairs), pairCapacity(pairCapacity), out(out), sortArea(sortArea), limit(limit) {}

  // `depth` is the number of tables already open around this value.
  void value(int index, int depth);

  Error error() const { return failure; }
  size_t length() const { return used; }

 private:
  bool ok() const { return failure == Error::None; }
  void fail(const Error error) {
    if (ok()) failure = error;
  }
  void put(const void* bytes, size_t count);
  void putTag(const Tag tag) {
    const auto byte = static_cast<uint8_t>(tag);
    put(&byte, 1);
  }
  void putVarint(uint64_t value);
  void putKey(const Key& key);
  void table(int index, int depth);
  // Counts the record keys (those outside 1..narr), failing on the first bad key.
  size_t countRecords(int index, lua_Integer narr);
  void writeRecords(int index, lua_Integer narr, int depth);
  // Reorders the record pairs pushed since `firstPair`, which start at `recordStart`.
  void sortRecords(size_t firstPair, size_t recordStart);

  lua_State* L;
  PairSpan* pairs;
  size_t pairCapacity;
  size_t pairTop = 0;
  uint8_t* out;
  uint8_t* sortArea;
  size_t limit;
  size_t used = 0;
  Error failure = Error::None;
  // The tables open around the current value, for the cycle check.
  const void* path[MAX_DEPTH] = {};
};

void Encoder::put(const void* bytes, const size_t count) {
  if (!ok()) return;
  if (count > limit - used) {
    fail(Error::TooLarge);
    return;
  }
  std::memcpy(out + used, bytes, count);
  used += count;
}

void Encoder::putVarint(uint64_t value) {
  uint8_t bytes[MAX_VARINT_BYTES];
  size_t count = 0;
  do {
    const auto low = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    bytes[count++] = value != 0 ? static_cast<uint8_t>(low | 0x80) : low;
  } while (value != 0);
  put(bytes, count);
}

void Encoder::putKey(const Key& key) {
  if (key.isString) {
    putTag(Tag::String);
    putVarint(key.length);
    put(key.text, key.length);
    return;
  }
  putTag(Tag::Int);
  putVarint(zigzag(key.integer));
}

void Encoder::value(const int index, const int depth) {
  switch (lua_type(L, index)) {
    case LUA_TNIL:
      putTag(Tag::Nil);
      return;
    case LUA_TBOOLEAN:
      putTag(lua_toboolean(L, index) ? Tag::True : Tag::False);
      return;
    case LUA_TNUMBER: {
      if (lua_isinteger(L, index)) {
        putTag(Tag::Int);
        putVarint(zigzag(lua_tointeger(L, index)));
        return;
      }
      const lua_Number number = lua_tonumber(L, index);
      uint64_t bits = CANONICAL_NAN;
      if (!std::isnan(number)) std::memcpy(&bits, &number, sizeof(bits));
      uint8_t bytes[8];
      for (uint8_t& byte : bytes) {
        byte = static_cast<uint8_t>(bits);
        bits >>= 8;
      }
      putTag(Tag::Float);
      put(bytes, sizeof(bytes));
      return;
    }
    case LUA_TSTRING: {
      size_t length = 0;
      const char* text = lua_tolstring(L, index, &length);
      putTag(Tag::String);
      putVarint(length);
      put(text, length);
      return;
    }
    case LUA_TTABLE:
      table(index, depth);
      return;
    default:  // function, userdata, light userdata, thread
      fail(Error::BadType);
      return;
  }
}

void Encoder::table(const int index, const int depth) {
  const void* self = lua_topointer(L, index);
  for (int i = 0; i < depth; ++i) {
    if (path[i] == self) return fail(Error::Cycle);
  }
  if (depth == MAX_DEPTH) return fail(Error::TooDeep);
  if (lua_getmetatable(L, index)) {
    lua_pop(L, 1);
    return fail(Error::Metatable);
  }
  // A key and a value per level, plus one probe.
  if (!lua_checkstack(L, 3)) return fail(Error::NoMemory);
  path[depth] = self;

  // The array part is the longest non-nil run from 1. Each value takes at least a
  // byte, so a run longer than the limit can stop early.
  lua_Integer narr = 0;
  while (lua_rawgeti(L, index, narr + 1) != LUA_TNIL) {
    lua_pop(L, 1);
    if (static_cast<size_t>(narr) >= limit) return fail(Error::TooLarge);
    ++narr;
  }
  lua_pop(L, 1);

  const size_t nrec = countRecords(index, narr);
  putTag(Tag::Table);
  putVarint(static_cast<uint64_t>(narr));
  for (lua_Integer i = 1; i <= narr && ok(); ++i) {
    lua_rawgeti(L, index, i);
    value(lua_gettop(L), depth + 1);
    lua_pop(L, 1);
  }
  putVarint(nrec);
  if (!ok()) return;
  if (nrec > (limit - used) / MIN_PAIR_BYTES) return fail(Error::TooLarge);
  writeRecords(index, narr, depth);
}

size_t Encoder::countRecords(const int index, const lua_Integer narr) {
  size_t count = 0;
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    Key key;
    const Error error = readKey(L, -2, key);
    lua_pop(L, 1);
    if (error != Error::None) {
      lua_pop(L, 1);
      fail(error);
      return 0;
    }
    if (!inArrayPart(key, narr)) ++count;
  }
  return count;
}

void Encoder::writeRecords(const int index, const lua_Integer narr, const int depth) {
  const size_t firstPair = pairTop;
  const size_t recordStart = used;
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    Key key;
    readKey(L, -2, key);  // countRecords() accepted every key
    if (inArrayPart(key, narr)) {
      lua_pop(L, 1);
      continue;
    }
    // Unreachable while maxPairs() holds; kept so the spans can never overrun.
    if (pairTop == pairCapacity) {
      lua_pop(L, 2);
      return fail(Error::TooLarge);
    }
    const size_t start = used;
    putKey(key);
    value(lua_gettop(L), depth + 1);
    lua_pop(L, 1);
    if (!ok()) {
      lua_pop(L, 1);
      return;
    }
    pairs[pairTop++] = PairSpan{static_cast<uint16_t>(start), static_cast<uint16_t>(used - start)};
  }
  sortRecords(firstPair, recordStart);
  pairTop = firstPair;
}

void Encoder::sortRecords(const size_t firstPair, const size_t recordStart) {
  PairSpan* first = pairs + firstPair;
  PairSpan* last = pairs + pairTop;
  if (last - first < 2) return;
  std::sort(first, last, [this](const PairSpan& a, const PairSpan& b) {
    return keyLess(keyAt(out + a.start), keyAt(out + b.start));
  });
  size_t moved = 0;
  for (const PairSpan* pair = first; pair != last; ++pair) {
    std::memcpy(sortArea + moved, out + pair->start, pair->length);
    moved += pair->length;
  }
  std::memcpy(out + recordStart, sortArea, moved);
}

// Builds each decoded value on the Lua stack: decode().
class LuaSink {
 public:
  explicit LuaSink(lua_State* L) : L(L) {}

  bool reserve() { return lua_checkstack(L, 3) != 0; }
  void nil() { lua_pushnil(L); }
  void boolean(const bool value) { lua_pushboolean(L, value); }
  void integer(const int64_t value) { lua_pushinteger(L, value); }
  void number(const double value) { lua_pushnumber(L, value); }
  void string(const Key& text) { lua_pushlstring(L, text.text, text.length); }
  void table(const uint64_t narr) { lua_createtable(L, static_cast<int>(narr), 0); }
  // Stores the value on top at array index i of the table below it.
  void arrayValue(const uint64_t i) { lua_rawseti(L, -2, static_cast<lua_Integer>(i)); }
  void key(const Key& key) {
    if (key.isString) {
      lua_pushlstring(L, key.text, key.length);
    } else {
      lua_pushinteger(L, key.integer);
    }
  }
  // Stores the key and value on top into the table below them.
  void recordValue() { lua_rawset(L, -3); }

 private:
  lua_State* L;
};

// Builds nothing, so the bytes are checked by the same rules without a Lua state: check().
struct CheckSink {
  static bool reserve() { return true; }
  static void nil() {}
  static void boolean(bool) {}
  static void integer(int64_t) {}
  static void number(double) {}
  static void string(const Key&) {}
  static void table(uint64_t) {}
  static void arrayValue(uint64_t) {}
  static void key(const Key&) {}
  static void recordValue() {}
};

// Reads bytes strictly in order, so the first fault met is the one reported (the
// same order game_codec.py follows). Hands each value to the sink; with LuaSink,
// pushes one value per value() that succeeds, and decode() drops anything left on
// error.
template <typename Sink>
class Decoder {
 public:
  Decoder(Sink& sink, const uint8_t* data, const size_t length) : sink(sink), at(data), end(data + length) {}

  // `tag` is the decoded value's tag, so callers can refuse a nil without the sink.
  Error value(int depth, Tag& tag);
  size_t remaining() const { return static_cast<size_t>(end - at); }

 private:
  Error byte(uint8_t& out);
  Error varint(uint64_t& out);
  Error string(Key& key);
  Error table(int depth);
  Error records(uint64_t narr, int depth);

  Sink& sink;
  const uint8_t* at;
  const uint8_t* end;
};

template <typename Sink>
Error Decoder<Sink>::byte(uint8_t& out) {
  if (at == end) return Error::Truncated;
  out = *at++;
  return Error::None;
}

template <typename Sink>
Error Decoder<Sink>::varint(uint64_t& out) {
  uint64_t value = 0;
  for (size_t i = 0; i < MAX_VARINT_BYTES; ++i) {
    uint8_t b = 0;
    if (byte(b) != Error::None) return Error::Truncated;
    if (i == MAX_VARINT_BYTES - 1 && b > 1) return Error::Overflow;
    value |= static_cast<uint64_t>(b & 0x7F) << (7 * i);
    if ((b & 0x80) == 0) {
      if (i > 0 && b == 0) return Error::NonCanonical;  // overlong
      out = value;
      return Error::None;
    }
  }
  return Error::Overflow;  // not reached: the tenth byte either ends or overflows
}

// A string's length and bytes; the key points into the input.
template <typename Sink>
Error Decoder<Sink>::string(Key& key) {
  uint64_t length = 0;
  const Error error = varint(length);
  if (error != Error::None) return error;
  if (length > remaining()) return Error::Truncated;
  key.isString = true;
  key.text = reinterpret_cast<const char*>(at);
  key.length = static_cast<size_t>(length);
  at += key.length;
  return Error::None;
}

template <typename Sink>
Error Decoder<Sink>::value(const int depth, Tag& tag) {
  uint8_t tagByte = 0;
  if (byte(tagByte) != Error::None) return Error::Truncated;
  if (tagByte >= TAG_COUNT) return Error::BadTag;
  tag = static_cast<Tag>(tagByte);
  switch (tag) {
    case Tag::Nil:
      sink.nil();
      return Error::None;
    case Tag::False:
    case Tag::True:
      sink.boolean(tag == Tag::True);
      return Error::None;
    case Tag::Int: {
      uint64_t value = 0;
      const Error error = varint(value);
      if (error != Error::None) return error;
      sink.integer(unzigzag(value));
      return Error::None;
    }
    case Tag::Float: {
      if (remaining() < 8) return Error::Truncated;
      uint64_t bits = 0;
      for (int i = 7; i >= 0; --i) bits = (bits << 8) | at[i];
      at += 8;
      double number = 0;
      std::memcpy(&number, &bits, sizeof(number));
      if (std::isnan(number) && bits != CANONICAL_NAN) return Error::NonCanonical;
      sink.number(number);
      return Error::None;
    }
    case Tag::String: {
      Key text;
      const Error error = string(text);
      if (error != Error::None) return error;
      sink.string(text);
      return Error::None;
    }
    case Tag::Table:
      return table(depth);
  }
  return Error::BadTag;  // not reached: every tag below TAG_COUNT is handled
}

template <typename Sink>
Error Decoder<Sink>::table(const int depth) {
  if (depth == MAX_DEPTH) return Error::TooDeep;
  if (!sink.reserve()) return Error::NoMemory;
  uint64_t narr = 0;
  Error error = varint(narr);
  if (error != Error::None) return error;
  // Each array value takes at least a byte; checked before the table is sized.
  if (narr > remaining()) return Error::Truncated;
  sink.table(narr);
  for (uint64_t i = 1; i <= narr; ++i) {
    Tag tag = Tag::Nil;
    error = value(depth + 1, tag);
    if (error != Error::None) return error;
    if (tag == Tag::Nil) return Error::NonCanonical;  // the run holds no nil
    sink.arrayValue(i);
  }
  return records(narr, depth);
}

template <typename Sink>
Error Decoder<Sink>::records(const uint64_t narr, const int depth) {
  uint64_t nrec = 0;
  Error error = varint(nrec);
  if (error != Error::None) return error;
  if (nrec > remaining() / MIN_PAIR_BYTES) return Error::Truncated;
  Key previous;
  for (uint64_t i = 0; i < nrec; ++i) {
    uint8_t tagByte = 0;
    if (byte(tagByte) != Error::None) return Error::Truncated;
    Key key;
    if (tagByte == static_cast<uint8_t>(Tag::Int)) {
      uint64_t value = 0;
      error = varint(value);
      if (error != Error::None) return error;
      key.integer = unzigzag(value);
      // 1..narr is the array part, and narr + 1 would have extended the run.
      if (key.integer >= 1 && static_cast<uint64_t>(key.integer) <= narr + 1) return Error::NonCanonical;
    } else if (tagByte == static_cast<uint8_t>(Tag::String)) {
      error = string(key);
      if (error != Error::None) return error;
    } else {
      return Error::BadKey;
    }
    if (i > 0 && !keyLess(previous, key)) return Error::NonCanonical;  // unsorted or duplicate
    sink.key(key);
    Tag tag = Tag::Nil;
    error = value(depth + 1, tag);
    if (error != Error::None) return error;
    if (tag == Tag::Nil) return Error::NonCanonical;  // a table cannot hold nil
    sink.recordValue();
    previous = key;
  }
  return Error::None;
}

}  // namespace

const char* errorName(const Error error) {
  switch (error) {
    case Error::None:
      return "none";
    case Error::TooLarge:
      return "too_large";
    case Error::TooDeep:
      return "too_deep";
    case Error::BadKey:
      return "bad_key";
    case Error::Overflow:
      return "overflow";
    case Error::Cycle:
      return "cycle";
    case Error::Metatable:
      return "metatable";
    case Error::BadType:
      return "bad_type";
    case Error::FloatKey:
      return "float_key";
    case Error::NanKey:
      return "nan_key";
    case Error::Truncated:
      return "truncated";
    case Error::BadTag:
      return "bad_tag";
    case Error::Trailing:
      return "trailing";
    case Error::NonCanonical:
      return "non_canonical";
    case Error::Scratch:
      return "scratch";
    case Error::NoMemory:
      return "no_memory";
  }
  return "unknown";
}

Encoded encode(lua_State* L, const int index, const size_t limit, void* scratch, const size_t scratchSize) {
  Encoded result;
  if (limit > MAX_LIMIT || scratch == nullptr || scratchSize < scratchBytes(limit) ||
      reinterpret_cast<uintptr_t>(scratch) % alignof(PairSpan) != 0) {
    result.error = Error::Scratch;
    return result;
  }
  auto* pairs = static_cast<PairSpan*>(scratch);
  uint8_t* out = static_cast<uint8_t*>(scratch) + maxPairs(limit) * sizeof(PairSpan);
  Encoder encoder(L, pairs, maxPairs(limit), out, out + limit, limit);
  encoder.value(lua_absindex(L, index), 0);
  result.error = encoder.error();
  if (result.error != Error::None) return result;
  result.data = out;
  result.length = encoder.length();
  return result;
}

Error decode(lua_State* L, const uint8_t* data, const size_t length, const size_t limit) {
  if (length > limit) return Error::TooLarge;
  if (!lua_checkstack(L, 1)) return Error::NoMemory;
  const int base = lua_gettop(L);
  LuaSink sink(L);
  Decoder<LuaSink> decoder(sink, data, length);
  Tag tag = Tag::Nil;
  Error error = decoder.value(0, tag);
  if (error == Error::None && decoder.remaining() != 0) error = Error::Trailing;
  if (error != Error::None) lua_settop(L, base);
  return error;
}

Error check(const uint8_t* data, const size_t length, const size_t limit, bool& isTable) {
  isTable = false;
  if (length > limit) return Error::TooLarge;
  CheckSink sink;
  Decoder<CheckSink> decoder(sink, data, length);
  Tag tag = Tag::Nil;
  Error error = decoder.value(0, tag);
  if (error == Error::None && decoder.remaining() != 0) error = Error::Trailing;
  if (error == Error::None) isTable = tag == Tag::Table;
  return error;
}

}  // namespace GameScript::Codec
