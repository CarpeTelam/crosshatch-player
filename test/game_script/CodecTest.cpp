#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <lua.hpp>
#include <map>
#include <string>
#include <vector>

#include "ArenaAllocator.h"
#include "BlobHeader.h"
#include "Codec.h"
#include "StreamingJsonParser.h"

// The expected bytes live in codec_vectors.json, which scripts/game_codec_test.py
// reads too; this suite loads them and runs them through the C codec.

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");

using namespace GameScript;

namespace {

using Record = std::map<std::string, std::string>;

struct Vectors {
  bool loaded = false;
  std::map<std::string, std::string> scalars;
  std::map<std::string, std::vector<Record>> sections;

  const std::vector<Record>& section(const std::string& name) const {
    static const std::vector<Record> empty;
    const auto found = sections.find(name);
    return found == sections.end() ? empty : found->second;
  }
};

// Top-level scalars, and top-level arrays of flat objects kept as text.
class VectorCollector {
 public:
  explicit VectorCollector(Vectors& out) : out(out) {}

  bool complete() const { return started && depth == 0; }

  JsonCallbacks callbacks() {
    return JsonCallbacks{this,   onKey,         onString,    onNumber,     onBool,
                         onNull, onObjectStart, onObjectEnd, onArrayStart, onArrayEnd};
  }

 private:
  static VectorCollector& self(void* ctx) { return *static_cast<VectorCollector*>(ctx); }

  static void onKey(void* ctx, const char* key, size_t len) {
    auto& c = self(ctx);
    if (c.depth == 1) c.section.assign(key, len);
    if (c.depth == 3) c.field.assign(key, len);
  }
  static void onString(void* ctx, const char* value, size_t len) { self(ctx).value(std::string(value, len)); }
  static void onNumber(void* ctx, const char* value, size_t len) { self(ctx).value(std::string(value, len)); }
  static void onBool(void* ctx, bool value) { self(ctx).value(value ? "true" : "false"); }
  static void onNull(void* ctx) { self(ctx).value("null"); }
  static void onObjectStart(void* ctx) {
    auto& c = self(ctx);
    c.started = true;
    if (c.depth == 2) c.out.sections[c.section].emplace_back();
    ++c.depth;
  }
  static void onObjectEnd(void* ctx) { --self(ctx).depth; }
  static void onArrayStart(void* ctx) {
    auto& c = self(ctx);
    if (c.depth == 1) c.out.sections[c.section];
    ++c.depth;
  }
  static void onArrayEnd(void* ctx) { --self(ctx).depth; }

  void value(const std::string& text) {
    if (depth == 1) out.scalars[section] = text;
    if (depth == 3) out.sections[section].back()[field] = text;
  }

  Vectors& out;
  int depth = 0;
  bool started = false;
  std::string section;
  std::string field;
};

const Vectors& vectors() {
  static const Vectors loaded = [] {
    Vectors v;
    std::ifstream file(CODEC_VECTORS_PATH, std::ios::binary);
    if (!file) return v;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    VectorCollector collector(v);
    StreamingJsonParser parser(collector.callbacks());
    parser.feed(text.data(), text.size());
    v.loaded = !parser.hasError() && collector.complete();
    return v;
  }();
  return loaded;
}

std::string field(const Record& r, const char* name) {
  const auto found = r.find(name);
  return found == r.end() ? std::string() : found->second;
}

// The vector hex notation: space-separated hex groups, each optionally followed by *N.
std::vector<uint8_t> parseHex(const std::string& text) {
  std::vector<uint8_t> out;
  size_t at = 0;
  while (at < text.size()) {
    if (text[at] == ' ') {
      ++at;
      continue;
    }
    const size_t end = std::min(text.find(' ', at), text.size());
    const std::string group = text.substr(at, end - at);
    const size_t star = group.find('*');
    const std::string digits = group.substr(0, star);
    const size_t count = star == std::string::npos ? 1 : std::stoul(group.substr(star + 1));
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < digits.size(); i += 2) {
      bytes.push_back(static_cast<uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
    }
    for (size_t i = 0; i < count; ++i) out.insert(out.end(), bytes.begin(), bytes.end());
    at = end;
  }
  return out;
}

std::string toHex(const uint8_t* data, size_t length) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < length; ++i) {
    if (i) out += ' ';
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0xF];
  }
  return out;
}

size_t limitOf(const Record& r) {
  const std::string name = field(r, "limit");
  if (name == "snapshot") return Codec::SNAPSHOT_LIMIT;
  if (name == "move") return Codec::MOVE_LIMIT;
  EXPECT_TRUE(name.empty() || name == "store") << "unknown limit " << name;
  return Codec::STORE_LIMIT;
}

// The helpers the vector notation may call, and a strict equality: same type and
// subtype, NaN equal to NaN, and the sign of zero counted.
constexpr const char* PRELUDE = R"lua(
local function cycle(t, ...)
  local keys = {...}
  local holder = t
  for i = 1, #keys - 1 do holder = holder[keys[i]] end
  holder[keys[#keys]] = t
  return t
end
local function nest(n, v)
  for _ = 1, n do v = {v} end
  return v
end
local function same(a, b)
  if type(a) ~= type(b) then return false end
  if type(a) == "number" then
    if math.type(a) ~= math.type(b) then return false end
    if a ~= a then return b ~= b end
    return a == b and (a ~= 0 or 1 / a == 1 / b)
  end
  if type(a) ~= "table" then return a == b end
  for k, v in next, a do
    if not same(v, rawget(b, k)) then return false end
  end
  for k in next, b do
    if rawget(a, k) == nil then return false end
  end
  return true
end
local env = {
  setmetatable = setmetatable,
  string = {rep = string.rep},
  math = {huge = math.huge, maxinteger = math.maxinteger, mininteger = math.mininteger},
  cycle = cycle,
  fn = function() return function() end end,
  nest = nest,
  shared = function(t) return {t, t} end,
}
return env, same
)lua";

// A state whose heap is an arena, as on the device, with the codec's scratch taken
// from the same arena (entry 8's arrangement).
class CodecTest : public ::testing::Test {
 protected:
  void SetUp() override {
    block.resize(ARENA_BYTES);
    arena.reset(block.data(), block.size());
    L = lua_newstate(&ArenaAllocator::luaAlloc, &arena, 1234);
    ASSERT_NE(L, nullptr);
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_settop(L, 0);
    ASSERT_EQ(luaL_loadstring(L, PRELUDE), LUA_OK);
    ASSERT_EQ(lua_pcall(L, 0, 2, 0), LUA_OK) << lua_tostring(L, -1);
    sameRef = luaL_ref(L, LUA_REGISTRYINDEX);
    envRef = luaL_ref(L, LUA_REGISTRYINDEX);
    scratchSize = Codec::scratchBytes(Codec::STORE_LIMIT);
    scratch = arena.allocate(scratchSize);
    ASSERT_NE(scratch, nullptr);
  }

  void TearDown() override {
    if (L) lua_close(L);
  }

  // Pushes the value a vector's notation describes.
  bool push(const std::string& notation) {
    const std::string chunk = "return " + notation;
    const int loaded = luaL_loadbufferx(L, chunk.data(), chunk.size(), "=vector", "t");
    if (loaded == LUA_OK) {
      lua_rawgeti(L, LUA_REGISTRYINDEX, envRef);
      lua_setupvalue(L, -2, 1);  // _ENV
    }
    if (loaded != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
      ADD_FAILURE() << notation << ": " << lua_tostring(L, -1);
      lua_pop(L, 1);
      return false;
    }
    return true;
  }

  // Compares the two values on top of the stack and pops them.
  bool sameOnTop() {
    lua_rawgeti(L, LUA_REGISTRYINDEX, sameRef);
    lua_insert(L, -3);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
      ADD_FAILURE() << lua_tostring(L, -1);
      lua_pop(L, 1);
      return false;
    }
    const bool same = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return same;
  }

  Codec::Encoded encodeTop(size_t limit) { return Codec::encode(L, -1, limit, scratch, scratchSize); }

  std::vector<uint8_t> block;
  ArenaAllocator arena;
  lua_State* L = nullptr;
  int envRef = LUA_NOREF;
  int sameRef = LUA_NOREF;
  void* scratch = nullptr;
  size_t scratchSize = 0;
};

TEST(CodecVectorsTest, FileLoads) {
  const Vectors& v = vectors();
  ASSERT_TRUE(v.loaded) << "cannot read " << CODEC_VECTORS_PATH;
  for (const char* name : {"encode", "encode_errors", "decode_errors", "headers"}) {
    EXPECT_FALSE(v.section(name).empty()) << name;
  }
}

// Each limit is one named constant, mirrored in the vector file (spine, Units and limits).
TEST(CodecVectorsTest, ConstantsMatchVectors) {
  const Vectors& v = vectors();
  ASSERT_TRUE(v.loaded);
  const std::map<std::string, size_t> constants = {
      {"codec_version", Codec::VERSION}, {"snapshot_limit", Codec::SNAPSHOT_LIMIT},
      {"move_limit", Codec::MOVE_LIMIT}, {"store_limit", Codec::STORE_LIMIT},
      {"max_depth", Codec::MAX_DEPTH},   {"blob_header_bytes", BLOB_HEADER_BYTES},
  };
  for (const auto& [name, constant] : constants) {
    ASSERT_EQ(v.scalars.count(name), 1u) << name;
    EXPECT_EQ(std::stoul(v.scalars.at(name)), constant) << name;
  }
}

// docs/crosshatch/formats.md quotes these sizes.
TEST(CodecVectorsTest, ScratchSizes) {
  EXPECT_EQ(Codec::scratchBytes(Codec::SNAPSHOT_LIMIT), 4732u);
  EXPECT_EQ(Codec::scratchBytes(Codec::MOVE_LIMIT), 920u);
  EXPECT_EQ(Codec::scratchBytes(Codec::STORE_LIMIT), 13720u);
}

TEST_F(CodecTest, EncodeVectors) {
  for (const Record& r : vectors().section("encode")) {
    const std::string name = field(r, "name");
    const std::vector<uint8_t> expected = parseHex(field(r, "hex"));
    const size_t limit = limitOf(r);
    ASSERT_TRUE(push(field(r, "value"))) << name;
    const int top = lua_gettop(L);
    const Codec::Encoded encoded = encodeTop(limit);
    EXPECT_EQ(lua_gettop(L), top) << name;
    ASSERT_EQ(Codec::errorName(encoded.error), std::string("none")) << name;
    EXPECT_EQ(toHex(encoded.data, encoded.length), toHex(expected.data(), expected.size())) << name;

    ASSERT_EQ(Codec::errorName(Codec::decode(L, expected.data(), expected.size(), limit)), std::string("none")) << name;
    EXPECT_EQ(lua_gettop(L), top + 1) << name;
    const Codec::Encoded again = encodeTop(limit);
    ASSERT_EQ(again.error, Codec::Error::None) << name;
    EXPECT_EQ(toHex(again.data, again.length), toHex(expected.data(), expected.size())) << name;
    EXPECT_TRUE(sameOnTop()) << name << ": decoded value differs";
    lua_settop(L, 0);
  }
}

TEST_F(CodecTest, EncodeErrorVectors) {
  for (const Record& r : vectors().section("encode_errors")) {
    const std::string name = field(r, "name");
    ASSERT_TRUE(push(field(r, "value"))) << name;
    const int top = lua_gettop(L);
    const Codec::Encoded encoded = encodeTop(limitOf(r));
    EXPECT_EQ(Codec::errorName(encoded.error), field(r, "error")) << name;
    EXPECT_EQ(encoded.data, nullptr) << name;
    EXPECT_EQ(lua_gettop(L), top) << name;
    lua_settop(L, 0);
  }
}

TEST_F(CodecTest, DecodeErrorVectors) {
  for (const Record& r : vectors().section("decode_errors")) {
    const std::string name = field(r, "name");
    const std::vector<uint8_t> bytes = parseHex(field(r, "hex"));
    lua_pushliteral(L, "sentinel");
    const Codec::Error error = Codec::decode(L, bytes.data(), bytes.size(), limitOf(r));
    EXPECT_EQ(Codec::errorName(error), field(r, "error")) << name;
    EXPECT_EQ(lua_gettop(L), 1) << name << ": the stack changed";
    lua_settop(L, 0);
  }
}

TEST(CodecVectorsTest, HeaderVectors) {
  for (const Record& r : vectors().section("headers")) {
    const std::string name = field(r, "name");
    const std::string magic = field(r, "magic");
    ASSERT_EQ(magic.size(), BLOB_MAGIC_BYTES) << name;
    const auto fileVersion = static_cast<uint8_t>(std::stoul(field(r, "file_version")));
    const std::vector<uint8_t> bytes = parseHex(field(r, "hex"));
    const BlobHeaderStatus status = checkBlobHeader(bytes.data(), bytes.size(), magic.c_str(), fileVersion);
    EXPECT_EQ(blobHeaderStatusName(status), field(r, "status")) << name;
    if (field(r, "status") != "ok") continue;
    uint8_t written[BLOB_HEADER_BYTES];
    writeBlobHeader(written, magic.c_str(), fileVersion);
    EXPECT_EQ(toHex(written, sizeof(written)), toHex(bytes.data(), BLOB_HEADER_BYTES)) << name;
  }
}

// Values Lua can hold but the vector notation cannot build.
TEST_F(CodecTest, LightUserdataAndThreadAreBadType) {
  int anchor = 0;
  lua_pushlightuserdata(L, &anchor);
  EXPECT_EQ(encodeTop(Codec::STORE_LIMIT).error, Codec::Error::BadType);
  lua_newthread(L);
  EXPECT_EQ(encodeTop(Codec::STORE_LIMIT).error, Codec::Error::BadType);
  lua_createtable(L, 0, 1);
  lua_pushlightuserdata(L, &anchor);
  lua_setfield(L, -2, "p");
  EXPECT_EQ(encodeTop(Codec::STORE_LIMIT).error, Codec::Error::BadType);
  EXPECT_EQ(lua_gettop(L), 3);
}

TEST_F(CodecTest, ScratchMustFitTheLimit) {
  lua_pushinteger(L, 1);
  auto* bytes = static_cast<uint8_t*>(scratch);
  const size_t snapshot = Codec::scratchBytes(Codec::SNAPSHOT_LIMIT);
  EXPECT_EQ(Codec::encode(L, -1, Codec::SNAPSHOT_LIMIT, scratch, snapshot - 1).error, Codec::Error::Scratch);
  EXPECT_EQ(Codec::encode(L, -1, Codec::SNAPSHOT_LIMIT, nullptr, snapshot).error, Codec::Error::Scratch);
  EXPECT_EQ(Codec::encode(L, -1, Codec::SNAPSHOT_LIMIT, bytes + 1, snapshot).error, Codec::Error::Scratch);
  EXPECT_EQ(Codec::encode(L, -1, Codec::MAX_LIMIT + 1, scratch, scratchSize).error, Codec::Error::Scratch);
  const Codec::Encoded fits = Codec::encode(L, -1, Codec::SNAPSHOT_LIMIT, scratch, snapshot);
  EXPECT_EQ(fits.error, Codec::Error::None);
  EXPECT_EQ(fits.length, 2u);
}

// Hundreds of record pairs in nested tables: every level is sorted in place, and the
// strict decoder accepts only sorted keys.
TEST_F(CodecTest, ManyNestedRecordsRoundTrip) {
  ASSERT_EQ(luaL_dostring(L, R"lua(
    local t = {}
    for i = 1, 60 do
      t["k" .. (i * 7919 % 101)] = {x = i, [-i] = "v", [i * 1000] = {i, i + 1}}
    end
    return t
  )lua"),
            LUA_OK)
      << lua_tostring(L, -1);
  const Codec::Encoded encoded = encodeTop(Codec::STORE_LIMIT);
  ASSERT_EQ(encoded.error, Codec::Error::None);
  const std::vector<uint8_t> bytes(encoded.data, encoded.data + encoded.length);
  ASSERT_EQ(Codec::decode(L, bytes.data(), bytes.size(), Codec::STORE_LIMIT), Codec::Error::None);
  const Codec::Encoded again = encodeTop(Codec::STORE_LIMIT);
  ASSERT_EQ(again.error, Codec::Error::None);
  EXPECT_EQ(std::vector<uint8_t>(again.data, again.data + again.length), bytes);
  EXPECT_TRUE(sameOnTop());
}

// decode() allocates through the state, so a full arena is a Lua memory error that
// the protected call around it catches (entry 8 decodes inside the trampoline).
TEST_F(CodecTest, DecodeInAFullArenaRaisesAMemoryError) {
  struct Call {
    std::vector<uint8_t> bytes;
    bool returned = false;
  } call{parseHex("05 fd 1f 61*4093")};
  lua_gc(L, LUA_GCCOLLECT);
  std::vector<void*> held;
  for (const size_t size : {1024u, 16u}) {
    while (void* block = arena.allocate(size)) held.push_back(block);
  }
  lua_pushcfunction(L, [](lua_State* state) -> int {
    auto* c = static_cast<Call*>(lua_touserdata(state, 1));
    Codec::decode(state, c->bytes.data(), c->bytes.size(), Codec::STORE_LIMIT);
    c->returned = true;
    return 0;
  });
  lua_pushlightuserdata(L, &call);
  const int status = lua_pcall(L, 1, 0, 0);
  for (void* block : held) arena.release(block);
  EXPECT_EQ(status, LUA_ERRMEM);
  EXPECT_FALSE(call.returned);
}

// Encoding never touches the heap: the arena's use is the same before and after.
TEST_F(CodecTest, EncodeAllocatesNothing) {
  ASSERT_TRUE(push("{a = {1, 2, 3}, b = 'text', [10] = {c = 1.5}}"));
  const size_t before = arena.bytesInUse();
  EXPECT_EQ(encodeTop(Codec::STORE_LIMIT).error, Codec::Error::None);
  EXPECT_EQ(arena.bytesInUse(), before);
}

}  // namespace
