#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

// Codec v1 (AD-10): the one encoding of game state, moves, and ch.store. Lua values
// go to canonical bytes and back; docs/crosshatch/formats.md records the bytes, and
// scripts/game_codec.py is the reference that passes the same golden vectors
// (test/game_script/codec_vectors.json).
namespace GameScript::Codec {

// Written into every persisted blob's header and part of the link protocol version.
inline constexpr uint8_t VERSION = 1;

// Limits on codec output, in bytes, enforced in every mode (AD-10). Mirrored in the
// vector file, which has cases at each limit and one over.
inline constexpr size_t SNAPSHOT_LIMIT = 1400;
inline constexpr size_t MOVE_LIMIT = 256;
inline constexpr size_t STORE_LIMIT = 4 * 1024;
// Pair offsets in the encoder's scratch are 16-bit, so no limit may pass this.
inline constexpr size_t MAX_LIMIT = 0xFFFF;
// Tables nested at most this deep; the outermost table is depth 1.
inline constexpr int MAX_DEPTH = 16;

// Why an encode or decode failed. errorName() gives the snake_case name the vector
// file and game_codec.py use.
enum class Error : uint8_t {
  None,
  // Encode and decode.
  TooLarge,  // output (encode) or input (decode) longer than the limit
  TooDeep,   // more than MAX_DEPTH nested tables
  BadKey,    // a key that is neither an integer nor a string
  Overflow,  // a varint over 64 bits; game_codec.py also for an integer outside int64
  // Encode only.
  Cycle,      // a table that contains itself
  Metatable,  // a table with a metatable
  BadType,    // a function, userdata, or thread
  FloatKey,   // a float key that is not integral or outside int64
  NanKey,     // a NaN key (unreachable from Lua, which refuses NaN keys)
  // Decode only.
  Truncated,     // the input ends inside a value
  BadTag,        // an unknown tag byte
  Trailing,      // bytes after the value
  NonCanonical,  // well formed, but not the bytes the encoder would write
  // Caller errors, never produced by the vectors.
  Scratch,   // scratch too small or misaligned, or a limit over MAX_LIMIT
  NoMemory,  // the Lua stack could not grow
};

const char* errorName(Error error);

// One record pair's place in the output while its table is being sorted.
struct PairSpan {
  uint16_t start;
  uint16_t length;
};

// Record pairs that can be open at once for `limit` bytes of output: each pair
// written takes at least 3 bytes, plus one unfinished pair per nesting level.
constexpr size_t maxPairs(const size_t limit) { return limit / 3 + MAX_DEPTH + 1; }

// Scratch encode() needs for `limit`: the pair spans, the output, and an equal area
// used to reorder record pairs. Take it from the VM arena; it must be aligned for
// uint16_t, as every arena and malloc block is.
constexpr size_t scratchBytes(const size_t limit) { return maxPairs(limit) * sizeof(PairSpan) + 2 * limit; }

struct Encoded {
  Error error = Error::None;
  // Into the scratch; valid until the scratch is reused. Null on error.
  const uint8_t* data = nullptr;
  size_t length = 0;
};

// Encodes the value at `index` as at most `limit` bytes. Never raises, so it may run
// outside a protected call, and leaves the stack unchanged. It allocates nothing
// itself; it may only grow the Lua stack through the state's allocator (NoMemory
// when that fails).
Encoded encode(lua_State* L, int index, size_t limit, void* scratch, size_t scratchSize);

// Decodes `length` bytes (at most `limit`) and pushes the value. Accepts only the
// canonical bytes encode() would write. On error pushes nothing. Tables and strings
// come from the state's allocator, so a full heap raises a Lua memory error: call it
// inside a protected call.
Error decode(lua_State* L, const uint8_t* data, size_t length, size_t limit);

}  // namespace GameScript::Codec
