#include "ChBindings.h"

#include <ApiLevel.h>
#include <GameIcons.h>
#include <GameImages.h>
#include <IClock.h>
#include <IGameLog.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <lua.hpp>

#include "CallGuard.h"
#include "Codec.h"
#include "DisplayList.h"
#include "GameTimer.h"
#include "StoreSlot.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");
static_assert(LUA_EXTRASPACE >= sizeof(void*), "the binding context pointer lives in the state's extra space");
static_assert(GameIcons::ICON_COUNT <= UINT16_MAX, "a display list stores an icon's index in 16 bits");
static_assert(GameCore::MAX_IMAGES <= UINT16_MAX, "a display list stores an image's index in 16 bits");

namespace GameScript {

namespace {

// The values of the header's *_NAMES, index for index. Fills take all four colors;
// lines, outlines, and text only white or black (AD-7).
constexpr Color COLOR_VALUES[] = {Color::White, Color::Light, Color::Dark, Color::Black};
constexpr TextSize SIZE_VALUES[] = {TextSize::Small, TextSize::Medium, TextSize::Large};
constexpr Align ALIGN_VALUES[] = {Align::Left, Align::Center, Align::Right};
constexpr Refresh REFRESH_VALUES[] = {Refresh::Fast, Refresh::Half, Refresh::Full};
constexpr IconWeight WEIGHT_VALUES[] = {IconWeight::Regular, IconWeight::Fill};
static_assert(std::size(COLOR_NAMES) == std::size(COLOR_VALUES) + 1, "one value per color name");
static_assert(std::size(SIZE_NAMES) == std::size(SIZE_VALUES) + 1, "one value per size name");
static_assert(std::size(ALIGN_NAMES) == std::size(ALIGN_VALUES) + 1, "one value per align name");
static_assert(std::size(REFRESH_NAMES) == std::size(REFRESH_VALUES) + 1, "one value per refresh name");
static_assert(std::size(WEIGHT_NAMES) == std::size(WEIGHT_VALUES) + 1, "one value per weight name");
static_assert(std::size(GameIcons::DRAWN_PIXELS) == std::size(SIZE_VALUES), "one drawn icon size per size name");
static_assert(GameIcons::DRAWN_PIXELS[static_cast<size_t>(TextSize::Small)] == GameIcons::SMALL_PIXELS,
              "DRAWN_PIXELS is indexed by TextSize");
static_assert(GameIcons::DRAWN_PIXELS[static_cast<size_t>(TextSize::Medium)] == GameIcons::MEDIUM_PIXELS,
              "DRAWN_PIXELS is indexed by TextSize");
static_assert(GameIcons::DRAWN_PIXELS[static_cast<size_t>(TextSize::Large)] == 2 * GameIcons::MEDIUM_PIXELS,
              "DRAWN_PIXELS is indexed by TextSize");

// The gfx faults (ch.gfx outside draw, a full frame, an unknown icon or image name,
// a frame's icons and images over their pixel budget) stop the game (the contract's
// Errors), so they go through the guard: a script's own pcall cannot catch them and
// publish a cut frame.

// The frame being drawn; raises unless draw is running.
DisplayList& drawTarget(lua_State* L, const char* function) {
  const BindingContext& context = *bindingContext(L);
  if (!context.drawTarget) {
    char message[48];
    snprintf(message, sizeof(message), "ch.gfx.%s called outside draw", function);
    context.guard->raise(L, message);
  }
  return *context.drawTarget;
}

int frameFull(lua_State* L) {
  char message[72];
  snprintf(message, sizeof(message), "frame is full (at most %d drawing calls or %d bytes)",
           static_cast<int>(MAX_COMMANDS), static_cast<int>(MAX_BYTES));
  return bindingContext(L)->guard->raise(L, message);
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

// The byte length of the well-formed UTF-8 sequence at text[at], with `end` bytes of text (the ranges of Unicode
// Table 3-7): 0 when the byte there stands alone, which is a continuation byte with no lead, a lead the encoding never
// uses (C0, C1, F5 to FF), a sequence cut short by `end`, or one that is overlong, a surrogate, or past U+10FFFF (the
// second byte's range depends on E0, ED, F0, and F4).
size_t utf8SequenceLength(const char* text, const size_t at, const size_t end) {
  const auto lead = static_cast<uint8_t>(text[at]);
  size_t length = 0;
  uint8_t secondLow = 0x80;
  uint8_t secondHigh = 0xBF;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    if (lead == 0xE0) secondLow = 0xA0;   // else overlong
    if (lead == 0xED) secondHigh = 0x9F;  // else a surrogate
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    if (lead == 0xF0) secondLow = 0x90;   // else overlong
    if (lead == 0xF4) secondHigh = 0x8F;  // else past U+10FFFF
  }
  if (length == 0 || at + length > end) return 0;
  const auto second = static_cast<uint8_t>(text[at + 1]);
  if (second < secondLow || second > secondHigh) return 0;
  for (size_t i = 2; i < length; ++i) {
    if ((static_cast<uint8_t>(text[at + i]) & 0xC0) != 0x80) return 0;
  }
  return length;
}

// An unknown icon or image name stops the game through the guard, as a full frame
// does, so a script's pcall cannot carry on drawing without it. The message shows
// at most NAME_SHOWN_BYTES of the name (cut at a UTF-8 boundary), with each control
// byte, '"', and each byte that is not part of a well-formed UTF-8 sequence (a game's
// name is any bytes) as '?', so it stays one readable line and valid UTF-8: "ch.gfx.<kind>:
// unknown <kind> \"<name>\"" (the function is named for what it draws).
constexpr size_t NAME_SHOWN_BYTES = 32;

int unknownName(lua_State* L, const char* kind, const char* name, const size_t length) {
  char shown[NAME_SHOWN_BYTES + 1];
  const size_t kept = utf8Cut(name, length, NAME_SHOWN_BYTES);
  size_t out = 0;
  for (size_t i = 0; i < kept;) {
    const auto byte = static_cast<uint8_t>(name[i]);
    if (byte < 0x80) {
      shown[out++] = (byte < 0x20 || byte == 0x7F || byte == '"') ? '?' : name[i];
      ++i;
      continue;
    }
    const size_t sequence = utf8SequenceLength(name, i, kept);
    if (sequence == 0) {
      shown[out++] = '?';
      ++i;
      continue;
    }
    std::memcpy(shown + out, name + i, sequence);
    out += sequence;
    i += sequence;
  }
  shown[out] = '\0';
  char message[72];
  snprintf(message, sizeof(message), "ch.gfx.%s: unknown %s \"%s\"", kind, kind, shown);
  return bindingContext(L)->guard->raise(L, message);
}

// A frame's icons and images past MAX_BLIT_PIXELS canvas pixels stop the game
// through the guard, as a full frame does: "ch.gfx.<kind>: the frame's icons and
// images cover over <limit> pixels".
int blitBudgetFull(lua_State* L, const char* kind) {
  char message[96];
  snprintf(message, sizeof(message), "ch.gfx.%s: the frame's icons and images cover over %lu pixels", kind,
           static_cast<unsigned long>(MAX_BLIT_PIXELS));
  return bindingContext(L)->guard->raise(L, message);
}

// ch.gfx.icon(name, x, y, size, color, weight?): a library icon in its regular
// (the default) or fill weight with its top-left at x, y; only its ink pixels are
// drawn.
int gfxIcon(lua_State* L) {
  DisplayList& list = drawTarget(L, "icon");
  size_t length = 0;
  const char* name = luaL_checklstring(L, 1, &length);
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const TextSize size = checkSize(L, 4);
  const Color color = checkInkColor(L, 5);
  const IconWeight weight = WEIGHT_VALUES[luaL_checkoption(L, 6, "regular", WEIGHT_NAMES)];
  const int icon = GameIcons::find(name, length);
  if (icon < 0) return unknownName(L, "icon", name, length);
  const auto side = static_cast<uint32_t>(GameIcons::DRAWN_PIXELS[static_cast<size_t>(size)]);
  const Canvas& canvas = *bindingContext(L)->canvas;
  if (!list.chargeBlit(x, y, side, side, canvas.width, canvas.height)) return blitBudgetFull(L, "icon");
  if (!list.appendIcon(x, y, static_cast<uint16_t>(icon), size, color, weight)) return frameFull(L);
  return 0;
}

// ch.gfx.image(name, x, y, color): one of the game's own images with its top-left
// at x, y, at its own size and opaque; white swaps its black and white.
int gfxImage(lua_State* L) {
  DisplayList& list = drawTarget(L, "image");
  size_t length = 0;
  const char* name = luaL_checklstring(L, 1, &length);
  const lua_Integer x = luaL_checkinteger(L, 2);
  const lua_Integer y = luaL_checkinteger(L, 3);
  const Color color = checkInkColor(L, 4);
  const GameCore::GameImages* images = bindingContext(L)->images;
  const int image = images ? images->find(name, length) : -1;
  if (image < 0) return unknownName(L, "image", name, length);
  const GameCore::ImageSpan& span = images->spans[image];
  const Canvas& canvas = *bindingContext(L)->canvas;
  if (!list.chargeBlit(x, y, span.width, span.height, canvas.width, canvas.height)) {
    return blitBudgetFull(L, "image");
  }
  if (!list.appendImage(x, y, static_cast<uint16_t>(image), color)) return frameFull(L);
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

// Raises `literal` unless the stack has room for a binding that runs deep C code
// (the codec, the logger); see CallGuard::BINDING_HEADROOM_BYTES. Through the guard,
// so a script's pcall cannot catch it; raiseStatic, with no lua_getinfo or
// formatting, since under 4 KiB of stack is left here.
void requireHeadroom(lua_State* L, const char* literal) {
  CallGuard* guard = bindingContext(L)->guard;
  if (guard && !guard->hasHeadroom(CallGuard::BINDING_HEADROOM_BYTES)) guard->raiseStatic(L, literal);
}

// ch.timer.after(ms): replaces the pending timer (AD-23).
int timerAfter(lua_State* L) {
  const lua_Integer ms = luaL_checkinteger(L, 1);
  if (ms < static_cast<lua_Integer>(TIMER_MIN_MS)) {
    return luaL_argerror(L, 1, lua_pushfstring(L, "at least %d ms", static_cast<int>(TIMER_MIN_MS)));
  }
  const BindingContext& context = *bindingContext(L);
  const uint64_t now = context.clock->nowMs();
  enterLockedSection(L);
  context.timer->arm(now, static_cast<uint64_t>(ms));
  leaveLockedSection(L);
  return 0;
}

// ch.timer.cancel()
int timerCancel(lua_State* L) {
  enterLockedSection(L);
  bindingContext(L)->timer->cancel();
  leaveLockedSection(L);
  return 0;
}

// ch.time.ms(): milliseconds since the game loaded, for display only.
int timeMs(lua_State* L) {
  const BindingContext& context = *bindingContext(L);
  lua_pushinteger(L, static_cast<lua_Integer>(context.clock->nowMs() - context.startMs));
  return 1;
}

// ch.store.set(t): encoded at once under the store limit (AD-10, AD-17) and posted
// to the slot, which marks itself dirty only when the bytes changed.
int storeSet(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  requireHeadroom(L, "ch.store.set: script recursion too deep to call it");
  const BindingContext& context = *bindingContext(L);
  const Codec::Encoded encoded = Codec::encode(L, 1, Codec::STORE_LIMIT, context.scratch, context.scratchBytes);
  if (encoded.error != Codec::Error::None) {
    char message[ENCODE_ERROR_BYTES];
    encodeErrorMessage(message, sizeof(message), "ch.store.set", "the store", encoded.error, Codec::STORE_LIMIT);
    return context.guard->raise(L, message);
  }
  enterLockedSection(L);
  context.store->post({encoded.data, encoded.length});
  leaveLockedSection(L);
  return 0;
}

// ch.store.get(): a fresh copy of the saved table, or an empty table. The slot is
// copied into the scratch under its lock and decoded outside it, since decoding
// allocates and may raise.
int storeGet(lua_State* L) {
  requireHeadroom(L, "ch.store.get: script recursion too deep to call it");
  const BindingContext& context = *bindingContext(L);
  auto* scratch = static_cast<uint8_t*>(context.scratch);
  enterLockedSection(L);
  const size_t length = context.store->read({scratch, context.scratchBytes});
  leaveLockedSection(L);
  if (length == 0) {
    lua_createtable(L, 0, 0);
    return 1;
  }
  const Codec::Error error = Codec::decode(L, scratch, length, Codec::STORE_LIMIT);
  if (error != Codec::Error::None) {
    return luaL_error(L, "ch.store.get: the saved store cannot be decoded (%s)", Codec::errorName(error));
  }
  return 1;
}

constexpr luaL_Reg GFX_FUNCTIONS[] = {{"clear", gfxClear},   {"rect", gfxRect},       {"line", gfxLine},
                                      {"circle", gfxCircle}, {"text", gfxText},       {"icon", gfxIcon},
                                      {"image", gfxImage},   {"refresh", gfxRefresh}, {nullptr, nullptr}};
constexpr luaL_Reg TIMER_FUNCTIONS[] = {{"after", timerAfter}, {"cancel", timerCancel}, {nullptr, nullptr}};
constexpr luaL_Reg STORE_FUNCTIONS[] = {{"get", storeGet}, {"set", storeSet}, {nullptr, nullptr}};
constexpr luaL_Reg TIME_FUNCTIONS[] = {{"ms", timeMs}, {nullptr, nullptr}};

}  // namespace

size_t utf8Cut(const char* text, const size_t length, const size_t room) {
  if (length <= room) return length;
  size_t kept = room;
  while (kept > 0 && (static_cast<uint8_t>(text[kept]) & 0xC0) == 0x80) --kept;
  return kept;
}

void encodeErrorMessage(char* out, const size_t capacity, const char* function, const char* what,
                        const Codec::Error error, const size_t limit) {
  if (error == Codec::Error::TooLarge) {
    snprintf(out, capacity, "%s: %s is too large (over %d bytes)", function, what, static_cast<int>(limit));
  } else {
    snprintf(out, capacity, "%s: %s cannot be encoded (%s)", function, what, Codec::errorName(error));
  }
}

int chLog(lua_State* L) {
  requireHeadroom(L, "ch.log: script recursion too deep to call it");
  char line[LOG_LINE_BYTES + 1];
  size_t used = 0;
  const int count = lua_gettop(L);
  for (int i = 1; i <= count && used < LOG_LINE_BYTES; ++i) {
    if (i > 1) line[used++] = '\t';
    size_t length = 0;
    // May call __tostring, so it runs before the locked section.
    const char* text = luaL_tolstring(L, i, &length);
    const size_t kept = utf8Cut(text, length, LOG_LINE_BYTES - used);
    std::memcpy(line + used, text, kept);
    used += kept;
    lua_pop(L, 1);
  }
  for (size_t i = 0; i < used; ++i) {
    const auto byte = static_cast<uint8_t>(line[i]);
    // One line per call: newlines and other control bytes become spaces; the tab
    // separator stays.
    if ((byte < 0x20 && byte != '\t') || byte == 0x7F) line[i] = ' ';
  }
  line[used] = '\0';
  enterLockedSection(L);
  bindingContext(L)->log->write(line);
  leaveLockedSection(L);
  return 0;
}

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
  lua_createtable(L, 0, 8);  // ch
  lua_pushinteger(L, API_LEVEL);
  lua_setfield(L, -2, "api");
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
  luaL_newlib(L, TIMER_FUNCTIONS);
  lua_setfield(L, -2, "timer");
  luaL_newlib(L, STORE_FUNCTIONS);
  lua_setfield(L, -2, "store");
  luaL_newlib(L, TIME_FUNCTIONS);
  lua_setfield(L, -2, "time");
  lua_pushcfunction(L, chLog);
  lua_setfield(L, -2, "log");
  lua_setglobal(L, "ch");
}

}  // namespace GameScript
