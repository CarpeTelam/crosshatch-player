#include "ChBindings.h"

#include <climits>
#include <cstring>
#include <lua.hpp>

#include "DisplayList.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");
static_assert(LUA_EXTRASPACE >= sizeof(void*), "the binding context pointer lives in the state's extra space");

namespace GameScript {

namespace {

// Fills take all four colors; lines, outlines, and text only white or black (AD-7).
constexpr const char* const COLOR_NAMES[] = {"white", "light", "dark", "black", nullptr};
constexpr Color COLOR_VALUES[] = {Color::White, Color::Light, Color::Dark, Color::Black};
constexpr const char* const SIZE_NAMES[] = {"small", "medium", "large", nullptr};
constexpr TextSize SIZE_VALUES[] = {TextSize::Small, TextSize::Medium, TextSize::Large};
constexpr const char* const ALIGN_NAMES[] = {"left", "center", "right", nullptr};
constexpr Align ALIGN_VALUES[] = {Align::Left, Align::Center, Align::Right};
constexpr const char* const REFRESH_NAMES[] = {"fast", "half", "full", nullptr};
constexpr Refresh REFRESH_VALUES[] = {Refresh::Fast, Refresh::Half, Refresh::Full};

// The frame being drawn; raises unless draw is running.
DisplayList& drawTarget(lua_State* L, const char* function) {
  DisplayList* list = bindingContext(L)->drawTarget;
  if (!list) luaL_error(L, "ch.gfx.%s called outside draw", function);
  return *list;
}

int frameFull(lua_State* L) {
  return luaL_error(L, "frame is full (at most %d drawing calls or %d bytes)", static_cast<int>(MAX_COMMANDS),
                    static_cast<int>(MAX_BYTES));
}

Color checkFillColor(lua_State* L, const int arg) {
  return COLOR_VALUES[luaL_checkoption(L, arg, nullptr, COLOR_NAMES)];
}

Color checkInkColor(lua_State* L, const int arg) {
  const Color color = checkFillColor(L, arg);
  if (color == Color::Light || color == Color::Dark) luaL_argerror(L, arg, "\"light\" and \"dark\" are only for fills");
  return color;
}

Color checkColor(lua_State* L, const int arg, const bool filled) {
  return filled ? checkFillColor(L, arg) : checkInkColor(L, arg);
}

TextSize checkSize(lua_State* L, const int arg) { return SIZE_VALUES[luaL_checkoption(L, arg, nullptr, SIZE_NAMES)]; }

// ch.gfx.clear(color)
int gfxClear(lua_State* L) {
  DisplayList& list = drawTarget(L, "clear");
  const Color color = checkFillColor(L, 1);
  if (!list.appendClear(color)) return frameFull(L);
  return 0;
}

// ch.gfx.rect(x, y, w, h, color, filled?)
int gfxRect(lua_State* L) {
  DisplayList& list = drawTarget(L, "rect");
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  const lua_Integer w = luaL_checkinteger(L, 3);
  const lua_Integer h = luaL_checkinteger(L, 4);
  const bool filled = lua_toboolean(L, 6) != 0;
  const Color color = checkColor(L, 5, filled);
  if (!list.appendRect(x, y, w, h, color, filled)) return frameFull(L);
  return 0;
}

// ch.gfx.line(x1, y1, x2, y2, color)
int gfxLine(lua_State* L) {
  DisplayList& list = drawTarget(L, "line");
  const lua_Integer x1 = luaL_checkinteger(L, 1);
  const lua_Integer y1 = luaL_checkinteger(L, 2);
  const lua_Integer x2 = luaL_checkinteger(L, 3);
  const lua_Integer y2 = luaL_checkinteger(L, 4);
  const Color color = checkInkColor(L, 5);
  if (!list.appendLine(x1, y1, x2, y2, color)) return frameFull(L);
  return 0;
}

// ch.gfx.circle(x, y, r, color, filled?)
int gfxCircle(lua_State* L) {
  DisplayList& list = drawTarget(L, "circle");
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  const lua_Integer r = luaL_checkinteger(L, 3);
  const bool filled = lua_toboolean(L, 5) != 0;
  const Color color = checkColor(L, 4, filled);
  if (!list.appendCircle(x, y, r, color, filled)) return frameFull(L);
  return 0;
}

// ch.gfx.text(x, y, str, size, color, align?)
int gfxText(lua_State* L) {
  DisplayList& list = drawTarget(L, "text");
  const lua_Integer x = luaL_checkinteger(L, 1);
  const lua_Integer y = luaL_checkinteger(L, 2);
  size_t length = 0;
  const char* text = luaL_checklstring(L, 3, &length);
  const TextSize size = checkSize(L, 4);
  const Color color = checkInkColor(L, 5);
  const Align align = ALIGN_VALUES[luaL_checkoption(L, 6, "left", ALIGN_NAMES)];
  if (!list.appendText(x, y, text, length, size, color, align)) return frameFull(L);
  return 0;
}

// ch.gfx.refresh(mode?): the frame's refresh request; the largest of a frame's wins.
int gfxRefresh(lua_State* L) {
  DisplayList& list = drawTarget(L, "refresh");
  list.requestRefresh(REFRESH_VALUES[luaL_checkoption(L, 1, "fast", REFRESH_NAMES)]);
  return 0;
}

// ch.text_width(str, size): works in every callback, since it only reads tables.
int textWidth(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  const TextSize size = checkSize(L, 2);
  lua_pushinteger(L, bindingContext(L)->canvas->text.width(text, size));
  return 1;
}

constexpr luaL_Reg GFX_FUNCTIONS[] = {{"clear", gfxClear},   {"rect", gfxRect}, {"line", gfxLine},
                                      {"circle", gfxCircle}, {"text", gfxText}, {"refresh", gfxRefresh},
                                      {nullptr, nullptr}};

}  // namespace

void setBindingContext(lua_State* L, BindingContext* context) {
  std::memcpy(lua_getextraspace(L), &context, sizeof(context));
}

BindingContext* bindingContext(lua_State* L) {
  BindingContext* context = nullptr;
  std::memcpy(&context, lua_getextraspace(L), sizeof(context));
  return context;
}

void enterLockedSection(lua_State* L) {
  if (auto* sections = bindingContext(L)->lockedSections) sections->fetch_add(1, std::memory_order_acq_rel);
}

void leaveLockedSection(lua_State* L) {
  if (auto* sections = bindingContext(L)->lockedSections) sections->fetch_sub(1, std::memory_order_acq_rel);
}

void openChLibrary(lua_State* L) {
  const Canvas& canvas = *bindingContext(L)->canvas;
  lua_createtable(L, 0, 3);  // ch
  lua_createtable(L, 0, 2);  // ch.screen
  lua_pushinteger(L, canvas.width);
  lua_setfield(L, -2, "w");
  lua_pushinteger(L, canvas.height);
  lua_setfield(L, -2, "h");
  lua_setfield(L, -2, "screen");
  luaL_newlib(L, GFX_FUNCTIONS);
  lua_setfield(L, -2, "gfx");
  lua_pushcfunction(L, textWidth);
  lua_setfield(L, -2, "text_width");
  lua_setglobal(L, "ch");
}

}  // namespace GameScript
