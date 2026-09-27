#include "ChBindings.h"

#include <climits>
#include <cstring>
#include <lua.hpp>

#include "DisplayList.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");
static_assert(LUA_EXTRASPACE >= sizeof(void*), "the binding context pointer lives in the state's extra space");

namespace GameScript {

namespace {

// Lines and text take only white or black; fills gain light and dark later (AD-7).
constexpr const char* const COLOR_NAMES[] = {"white", "black", nullptr};
constexpr Color COLOR_VALUES[] = {Color::White, Color::Black};
constexpr const char* const SIZE_NAMES[] = {"small", "medium", "large", nullptr};
constexpr TextSize SIZE_VALUES[] = {TextSize::Small, TextSize::Medium, TextSize::Large};

int outsideDraw(lua_State* L, const char* function) { return luaL_error(L, "ch.gfx.%s called outside draw", function); }

int frameFull(lua_State* L) {
  return luaL_error(L, "frame is full (at most %d drawing calls or %d bytes)", static_cast<int>(MAX_COMMANDS),
                    static_cast<int>(MAX_BYTES));
}

Color checkColor(lua_State* L, const int arg) { return COLOR_VALUES[luaL_checkoption(L, arg, nullptr, COLOR_NAMES)]; }

// ch.gfx.clear(color)
int gfxClear(lua_State* L) {
  DisplayList* list = bindingContext(L)->drawTarget;
  if (!list) return outsideDraw(L, "clear");
  const Color color = checkColor(L, 1);
  if (!list->appendClear(color)) return frameFull(L);
  return 0;
}

// ch.gfx.rect(x, y, w, h, color, filled)
int gfxRect(lua_State* L) {
  DisplayList* list = bindingContext(L)->drawTarget;
  if (!list) return outsideDraw(L, "rect");
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  const lua_Integer w = luaL_checkinteger(L, 3);
  const lua_Integer h = luaL_checkinteger(L, 4);
  const Color color = checkColor(L, 5);
  const bool filled = lua_toboolean(L, 6) != 0;
  if (!list->appendRect(x, y, w, h, color, filled)) return frameFull(L);
  return 0;
}

// ch.gfx.text(x, y, str, size, color)
int gfxText(lua_State* L) {
  DisplayList* list = bindingContext(L)->drawTarget;
  if (!list) return outsideDraw(L, "text");
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  size_t length = 0;
  const char* text = luaL_checklstring(L, 3, &length);
  const TextSize size = SIZE_VALUES[luaL_checkoption(L, 4, nullptr, SIZE_NAMES)];
  const Color color = checkColor(L, 5);
  if (!list->appendText(x, y, text, length, size, color)) return frameFull(L);
  return 0;
}

constexpr luaL_Reg GFX_FUNCTIONS[] = {{"clear", gfxClear}, {"rect", gfxRect}, {"text", gfxText}, {nullptr, nullptr}};

}  // namespace

void setBindingContext(lua_State* L, BindingContext* context) {
  std::memcpy(lua_getextraspace(L), &context, sizeof(context));
}

BindingContext* bindingContext(lua_State* L) {
  BindingContext* context = nullptr;
  std::memcpy(&context, lua_getextraspace(L), sizeof(context));
  return context;
}

void openChLibrary(lua_State* L) {
  lua_createtable(L, 0, 1);  // ch
  luaL_newlib(L, GFX_FUNCTIONS);
  lua_setfield(L, -2, "gfx");
  lua_setglobal(L, "ch");
}

}  // namespace GameScript
