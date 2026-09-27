#include "CallGuard.h"

#include <climits>
#include <cstdio>
#include <lua.hpp>

#include "ChBindings.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");

namespace GameScript {

namespace {

constexpr int HOOK_MASK = LUA_MASKCOUNT | LUA_MASKCALL;

}  // namespace

void CallGuard::install(lua_State* L) { lua_sethook(L, &CallGuard::hook, HOOK_MASK, HOOK_INTERVAL); }

void CallGuard::arm(lua_State* L) {
  spent = 0;
  tripped = Fault::None;
  text[0] = '\0';
  // Also restores the interval after a sticky fault shortened it.
  lua_sethook(L, &CallGuard::hook, HOOK_MASK, HOOK_INTERVAL);
}

void CallGuard::trip(lua_State* L, lua_Debug* ar, const Fault fault) {
  tripped = fault;
  switch (fault) {
    case Fault::Stack:
      // Near the end of the stack: no lua_getinfo, no formatting.
      snprintf(text, sizeof(text), "%s", "script recursion too deep (C stack nearly full)");
      break;
    case Fault::Cancelled:
      snprintf(text, sizeof(text), "%s", "cancelled");
      break;
    case Fault::Budget:
      if (lua_getinfo(L, "Sl", ar) && ar->currentline > 0) {
        snprintf(text, sizeof(text), "%s:%d: instruction budget exceeded", ar->short_src, ar->currentline);
      } else {
        snprintf(text, sizeof(text), "%s", "instruction budget exceeded");
      }
      break;
    case Fault::None:
      break;
  }
}

void CallGuard::hook(lua_State* L, lua_Debug* ar) {
  CallGuard& guard = *bindingContext(L)->guard;
  const auto sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  if (sp < guard.deepest) guard.deepest = sp;
  if (guard.tripped == Fault::None) {
    if (guard.floor != 0 && sp < guard.floor + STACK_HEADROOM_BYTES) {
      guard.trip(L, ar, Fault::Stack);
    } else if (guard.cancelRequested()) {
      guard.trip(L, ar, Fault::Cancelled);
    } else if (ar->event == LUA_HOOKCOUNT && (guard.spent += HOOK_INTERVAL) >= INSTRUCTION_BUDGET) {
      guard.trip(L, ar, Fault::Budget);
    } else {
      return;
    }
  }
  // Raise (again): the next instruction re-raises, whatever pcall caught this one.
  lua_sethook(L, &CallGuard::hook, HOOK_MASK, 1);
  lua_pushstring(L, guard.text);
  lua_error(L);
}

}  // namespace GameScript
