#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

struct lua_State;
struct lua_Debug;

namespace GameScript {

// The GameVM task's stack (AD-5): 16 KiB of internal RAM.
inline constexpr size_t VM_STACK_BYTES = 16 * 1024;

// Why the guard stopped a call. Binding: a binding hit a fault the game contract
// says stops the game (a store over AD-10's limit, a full frame, ch.gfx outside
// draw, a table call over its element limit, too little stack for a binding),
// which ends the call as surely as a state over its limit. Memory: the heap cap
// ended a call a script's pcall or xpcall had caught (raiseMemory).
enum class Fault : uint8_t { None, Budget, Cancelled, Stack, Binding, Memory };

// The limits on one call into a game, enforced from a single Lua hook (AD-6, and
// the owner's stack decision of 2026-09-27): the instruction budget, the cancel
// flag, and the C stack headroom. LuaGame arms it before each lua_pcall.
//
// The hook fires every HOOK_INTERVAL instructions and on every function call. A
// Lua-to-Lua call does not grow the C stack, but each pcall, metamethod, or other
// C-to-Lua call does, and each of those is a call event, so the headroom check
// sees C recursion one level at a time.
//
// A fault is sticky: once raised, the hook raises it again at the next
// instruction, so a script's own pcall catches it at most once per level, and
// LuaGame reads fault() after the call whatever the call returned.
class CallGuard {
 public:
  static constexpr uint32_t INSTRUCTION_BUDGET = 2000000;
  static constexpr int HOOK_INTERVAL = 1000;
  static constexpr size_t STACK_HEADROOM_BYTES = 2048;
  static constexpr size_t MESSAGE_CAPACITY = 128;
  // require() refuses to compile a module with less stack than this left: the
  // parser runs no hook, and LUAI_MAXCCALLS (30, lib/lua/library.json) levels of
  // it cost up to 30 x 320 B = 9.6 KB on the ESP32-S3.
  static constexpr size_t PARSE_HEADROOM_BYTES = 10 * 1024;
  // ch.log, print, and ch.store refuse to run with less stack than this left: a
  // binding call is only sure of STACK_HEADROOM_BYTES, while the codec at depth 16
  // needs about 1.8 KB on the ESP32-S3 plus Lua's allocations, and logPrintf a 256 B
  // buffer plus vsnprintf and the serial write (about 1.5 KB).
  static constexpr size_t BINDING_HEADROOM_BYTES = 4 * 1024;

  CallGuard() = default;
  CallGuard(const CallGuard&) = delete;
  CallGuard& operator=(const CallGuard&) = delete;

  // Installs the hook. The state's BindingContext must point at this guard.
  void install(lua_State* L);
  // Starts a call: nothing spent, no fault. A requested cancel stays requested.
  void arm(lua_State* L);

  // Any task. Every later hook event raises Cancelled.
  void requestCancel() { cancel.store(true, std::memory_order_release); }
  bool cancelRequested() const { return cancel.load(std::memory_order_acquire); }

  // The lowest address the calling task's stack may use (its start on the
  // device). 0, the default, turns the headroom check off.
  void setStackFloor(uintptr_t lowest) { floor = lowest; }
  uintptr_t stackFloor() const { return floor; }
  // The deepest stack address seen at a hook event (UINTPTR_MAX before any).
  uintptr_t deepestAddress() const { return deepest; }
  // True when at least `bytes` of stack are free below the caller (or no floor is set).
  bool hasHeadroom(size_t bytes) const;

  Fault fault() const { return tripped; }
  // The fault's message, with the script's chunk and line for a budget fault.
  const char* message() const { return shown; }

  static void hook(lua_State* L, lua_Debug* ar);

  // From a binding: records a Binding fault with `message`, prefixed with the calling
  // script's chunk and line, and raises it; like every fault it is sticky, so the
  // script's own pcall cannot keep the call going. Does not return.
  int raise(lua_State* L, const char* message);
  // raise() without the chunk and line: records a Binding fault whose message is
  // `literal` itself (kept until the next arm(), so a string literal; raise()
  // passes the guard's own text), with no lua_getinfo or formatting, for a binding
  // that runs short of stack. Does not return.
  int raiseStatic(lua_State* L, const char* literal);
  // From the sandbox's pcall, xpcall, and require, when a call they protect ended
  // in a memory error (the heap cap, AD-6): records a Memory fault, unless one is
  // already recorded, and raises the error object on top of the stack again.
  // Allocation-free, since the heap is full. Sticky like every fault. Does not return.
  int raiseMemory(lua_State* L);

 private:
  void trip(lua_State* L, lua_Debug* ar, Fault fault);

  uint32_t spent = 0;
  Fault tripped = Fault::None;
  std::atomic<bool> cancel{false};
  uintptr_t floor = 0;
  uintptr_t deepest = UINTPTR_MAX;
  char text[MESSAGE_CAPACITY] = {};
  // text, or a static literal for the stack, cancel, and memory faults and raiseStatic.
  const char* shown = text;
};

}  // namespace GameScript
