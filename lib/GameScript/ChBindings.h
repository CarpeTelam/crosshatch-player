#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Codec.h"
#include "TextMetrics.h"

struct lua_State;

namespace GameCore {
class IClock;
class IGameLog;
class PauseClock;
struct GameImages;
}  // namespace GameCore

namespace GameScript {

class CallGuard;
class DisplayList;
class GameTimer;
class StoreSlot;
struct GameSources;

// One ch.log or print line is at most this many bytes (cut at a UTF-8 boundary),
// so it fits logPrintf's 256 B entry after its timestamp, level, and game id.
inline constexpr size_t LOG_LINE_BYTES = 160;

// Where `length` bytes of UTF-8 at `text` may be cut to keep at most `room` bytes
// without splitting a code point (ch.log lines, apply's rejection reason).
size_t utf8Cut(const char* text, size_t length, size_t room);

// Room for encodeErrorMessage's text with the longest `function` and `what` it is
// given (ch.store.set's "the store").
inline constexpr size_t ENCODE_ERROR_BYTES = 96;
// The message for a value that `function` could not encode as `what` under `limit`
// bytes: "<function>: <what> is too large (over <limit> bytes)", or "<function>:
// <what> cannot be encoded (<error name>)". LuaGame's results and ch.store.set
// share it.
void encodeErrorMessage(char* out, size_t capacity, const char* function, const char* what, Codec::Error error,
                        size_t limit);

// The strings each ch.gfx option accepts (api-level-1.txt's `enum color`, `size`,
// `align`, `refresh`, and `weight`), null-terminated for luaL_checkoption; ChBindings.cpp maps
// them, index for index, to the DisplayList values.
inline constexpr const char* const COLOR_NAMES[] = {"white", "light", "dark", "black", nullptr};
inline constexpr const char* const SIZE_NAMES[] = {"small", "medium", "large", nullptr};
inline constexpr const char* const ALIGN_NAMES[] = {"left", "center", "right", nullptr};
inline constexpr const char* const REFRESH_NAMES[] = {"fast", "half", "full", nullptr};
inline constexpr const char* const WEIGHT_NAMES[] = {"regular", "fill", nullptr};

// The host's screen as a game sees it, fixed at VM start (AD-7): the canvas size
// (GameViewport's), exposed as ch.screen, and the text metrics behind
// ch.text_width (FrameReplay's font tables).
struct Canvas {
  int16_t width = 0;
  int16_t height = 0;
  TextMetrics text;
};

// Everything a ch.* C function may reach, owned by LuaGame and found through the
// state's extra space. Bindings never see LuaGame itself, so a new binding adds a
// field here instead of a dependency.
struct BindingContext {
  // The back display list while draw runs; null otherwise, which makes ch.gfx an error.
  DisplayList* drawTarget = nullptr;
  // ch.screen and ch.text_width (LuaGame's copy).
  const Canvas* canvas = nullptr;
  // The game's modules, which require() resolves against.
  const GameSources* sources = nullptr;
  // The game's own images, which ch.gfx.image draws by name.
  const GameCore::GameImages* images = nullptr;
  // The hook's limits for the current call (LuaGame's).
  CallGuard* guard = nullptr;
  // Bindings inside a locked section right now (LuaGame's); see enterLockedSection.
  std::atomic<uint32_t>* lockedSections = nullptr;
  // ch.time.ms reports play time: clock milliseconds since startMs (the game's load), less the time
  // the match spent in Paused when `paused` is set (null: none, as for a host that never pauses).
  // ch.timer keeps the raw clock.
  const GameCore::IClock* clock = nullptr;
  const GameCore::PauseClock* paused = nullptr;
  uint64_t startMs = 0;
  // ch.timer's one pending timer (LuaGame's).
  GameTimer* timer = nullptr;
  // ch.store's latest-wins slot (the match's), and the codec scratch ch.store.set
  // encodes into and ch.store.get copies through (LuaGame's, from the arena reserve).
  StoreSlot* store = nullptr;
  void* scratch = nullptr;
  size_t scratchBytes = 0;
  // Where ch.log and print lines go.
  GameCore::IGameLog* log = nullptr;
};

void setBindingContext(lua_State* L, BindingContext* context);
BindingContext* bindingContext(lua_State* L);

// A binding that takes a lock (a mutex, a newlib call that locks) brackets the
// locked part with these, so GameVM::abandon never deletes the task while it holds
// the lock. Nothing between them may raise a Lua error or call into Lua.
void enterLockedSection(lua_State* L);
void leaveLockedSection(lua_State* L);

// ch.log(...), which print also is: Lua print's text (tostring of each value,
// tab-separated) as one line to the context's log, at most LOG_LINE_BYTES, with
// control bytes other than those tabs shown as spaces.
int chLog(lua_State* L);

// Creates the global `ch` table and its sub-tables; ch.screen comes from the
// context's canvas, and every other context field must be set too. May raise a
// Lua error (out of memory), so call it only inside a protected call.
void openChLibrary(lua_State* L);

}  // namespace GameScript
