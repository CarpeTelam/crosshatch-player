#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <iomanip>
#include <lua.hpp>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ApiLevel.h"
#include "ApiLevelList.h"
#include "ArenaAllocator.h"
#include "CallGuard.h"
#include "ChBindings.h"
#include "Codec.h"
#include "DisplayList.h"
#include "GameIcons.h"
#include "GameImages.h"
#include "GameTimer.h"
#include "LuaGameFixture.h"
#include "Manifest.h"
#include "Sandbox.h"
#include "Session.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");

// The level-1 surface test (spine AD-19): a real LuaGame, whose load() opens the
// device's sandbox and ch table, reports what a game can reach (fixtures/surface),
// and each entry kind is compared with the union of docs/crosshatch/api-level-<n>.txt
// in both directions, so a function, global, library member, enum value, event,
// ctx field, limit, or icon added on either side alone fails here. ApiLevelTest
// checks the list's grammar and manifest entries; IconsMatchTheList checks the icon
// names against the library's table; ch.d.lua and the catalog are checked by the
// epics that add them.

// A new EventKind, SwipeDir, or Mode enumerator must break the build until named() lists it.
#pragma GCC diagnostic error "-Wswitch"

using namespace GameScript;
using ApiLevelList::Entry;
using GameScriptTestSupport::DirectGame;
using GameScriptTestSupport::readFixture;

namespace {

using Names = std::set<std::string>;

// Fails with every name found on one side only.
void expectSameNames(const Names& live, const Names& listed, const std::string& what) {
  for (const std::string& name : live) {
    EXPECT_TRUE(listed.count(name)) << what << " \"" << name << "\" is live but not in api-level-" << API_LEVEL
                                    << ".txt";
  }
  for (const std::string& name : listed) {
    EXPECT_TRUE(live.count(name)) << what << " \"" << name << "\" is listed but not live";
  }
}

// The words after `tag ` on each log line that starts with it.
Names tagged(const std::vector<std::string>& lines, const std::string& tag) {
  Names found;
  for (const std::string& line : lines) {
    if (line.rfind(tag + " ", 0) == 0) found.insert(line.substr(tag.size() + 1));
  }
  return found;
}

std::string firstWord(const std::string& text) { return text.substr(0, text.find(' ')); }

// "swipe x y dir" -> "swipe dir x y": an event's fields compared as a set.
std::string sortedEvent(const std::string& body) {
  std::istringstream words(body);
  std::string kind;
  words >> kind;
  Names fields;
  for (std::string field; words >> field;) fields.insert(field);
  for (const std::string& field : fields) kind += " " + field;
  return kind;
}

class ApiSurfaceTest : public GameScriptTestSupport::LuaGameTest {
 protected:
  ApiSurfaceTest() : surface(ApiLevelList::loadSurface()), entries(surface.entries()) {}

  // The bodies (or names) of every listed entry of `kind`.
  Names listed(const std::string& kind, const bool namesOnly = false) const {
    Names found;
    for (const Entry& entry : entries) {
      if (entry.kind == kind) found.insert(namesOnly ? entry.name : entry.body);
    }
    return found;
  }

  // The values of `enum <set> ...`.
  Names listedEnum(const std::string& set) const {
    Names values;
    for (const std::string& body : listed("enum")) {
      if (firstWord(body) == set) values.insert(body.substr(set.size() + 1));
    }
    return values;
  }

  // Loads the surface fixture in a fresh game; its load-time report is in log.lines.
  void loadSurfaceGame(DirectGame& game) {
    useSource("main", readFixture("surface/main.lua"));
    log.lines.clear();
    ASSERT_EQ(game.load(), Outcome::Ok) << game.errorMessage();
  }

  // How many ch.timer timers can be pending: after the game's input arms two with
  // different delays, how many fire.
  int pendingTimerCapacity(DirectGame& game) {
    if (game.start() != Outcome::Ok || game.input(InputEvent{InputKind::Tap, 0, 0}) != Outcome::Ok) return -1;
    int fired = 0;
    uint32_t serial = 0;
    for (int second = 0; second < 5; ++second) {
      clock.advance(1000);
      if (game.timer().takeDue(clock.nowMs(), serial)) ++fired;
    }
    return fired;
  }

  ApiLevelList::Surface surface;
  std::vector<Entry> entries;
};

TEST_F(ApiSurfaceTest, ListLoadsAndMatchesItsCrc) {
  ASSERT_TRUE(surface.loaded) << "cannot read " << ApiLevelList::listPath(API_MIN_LEVEL);
  ASSERT_EQ(entries.size(), surface.lines.size()) << "a line breaks the grammar; see ApiLevelTest";
  EXPECT_EQ(surface.crc(), static_cast<uint32_t>(API_SURFACE_CRC))
      << "update API_SURFACE_CRC in lib/GameCore/ApiLevel.h to 0x" << std::hex << std::uppercase << std::setw(8)
      << std::setfill('0') << surface.crc();
}

TEST_F(ApiSurfaceTest, ChTableMatchesTheList) {
  DirectGame game(arena, frames, sources, ports, canvas);
  loadSurfaceGame(game);
  // fn entries under ch are functions; field entries carry their type.
  Names listedCh;
  for (const Entry& entry : entries) {
    if (entry.kind == "fn" && entry.name.rfind("ch.", 0) == 0) listedCh.insert(entry.name + " function");
    if (entry.kind == "field") listedCh.insert(entry.body);
  }
  expectSameNames(tagged(log.lines, "C"), listedCh, "ch entry");
  EXPECT_TRUE(tagged(log.lines, "MT").empty()) << "a global table or ch table has a metatable";
}

TEST_F(ApiSurfaceTest, GlobalsMatchTheList) {
  DirectGame game(arena, frames, sources, ports, canvas);
  loadSurfaceGame(game);
  Names live;
  for (const std::string& global : tagged(log.lines, "G")) live.insert(firstWord(global));
  // ch, the fn entries without a dot, and the lib entries without a dot (the list's header).
  Names expected = {"ch"};
  for (const Entry& entry : entries) {
    const bool global = entry.name.find('.') == std::string::npos;
    if ((entry.kind == "fn" || entry.kind == "lib") && global) expected.insert(entry.name);
  }
  expectSameNames(live, expected, "global");
  // The runtime's own globals are functions.
  const Names liveGlobals = tagged(log.lines, "G");
  for (const Entry& entry : entries) {
    if (entry.kind == "fn" && entry.name.find('.') == std::string::npos) {
      EXPECT_TRUE(liveGlobals.count(entry.name + " function")) << entry.name << " is not a function";
    }
  }
  // Removed from Lua's base library, or never opened (spine AD-6).
  for (const char* removed : {"load", "loadfile", "dofile", "io", "os", "debug", "coroutine", "package"}) {
    EXPECT_FALSE(live.count(removed)) << removed << " must not be reachable";
  }
}

TEST_F(ApiSurfaceTest, LibraryMembersMatchTheList) {
  DirectGame game(arena, frames, sources, ports, canvas);
  loadSurfaceGame(game);
  Names live;
  for (const std::string& member : tagged(log.lines, "M")) live.insert(firstWord(member));
  Names listedMembers;
  const Names libs = listed("lib", true);
  for (const std::string& name : libs) {
    const size_t dot = name.find('.');
    if (dot == std::string::npos) continue;
    listedMembers.insert(name);
    EXPECT_TRUE(libs.count(name.substr(0, dot))) << name << " is listed without its table";
  }
  expectSameNames(live, listedMembers, "library member");
  // The string metatable is sealed: a game sees false, and strings still work.
  EXPECT_EQ(tagged(log.lines, "S"), (Names{"getmetatable boolean false", "method AB arith 2"}));

  // Behind the seal it is lstrlib's own (stringmetamethods): string methods, and
  // arithmetic on numeric strings, plus the seal. Read raw, since a game cannot.
  lua_State* L = luaL_newstate();
  openSandbox(L, random);
  lua_pushliteral(L, "");
  Names stringMeta;
  if (lua_getmetatable(L, -1)) {
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {  // every key of lstrlib's metatable is a string
      lua_getglobal(L, "string");
      const char* type = lua_rawequal(L, -1, -2) ? "string" : luaL_typename(L, -2);
      stringMeta.insert(std::string(lua_tostring(L, -3)) + " " + type);
      lua_pop(L, 2);
    }
  }
  lua_close(L);
  const Names expected = {"__index string", "__add function",     "__sub function", "__mul function",
                          "__mod function", "__pow function",     "__div function", "__idiv function",
                          "__unm function", "__metatable boolean"};
  EXPECT_EQ(stringMeta, expected);
}

// Whether a value is a named enumerator. Each switch lists every enumerator, and
// -Wswitch (an error in this file) fails the build when one is missing, so
// enumerators() below always yields them all.
bool named(const GameCore::EventKind kind) {
  switch (kind) {
    case GameCore::EventKind::Tap:
    case GameCore::EventKind::Rejected:
    case GameCore::EventKind::Over:
    case GameCore::EventKind::Timer:
    case GameCore::EventKind::LongPress:
    case GameCore::EventKind::Swipe:
      return true;
  }
  return false;
}

bool named(const GameCore::SwipeDir dir) {
  switch (dir) {
    case GameCore::SwipeDir::None:
    case GameCore::SwipeDir::Left:
    case GameCore::SwipeDir::Right:
    case GameCore::SwipeDir::Up:
    case GameCore::SwipeDir::Down:
      return true;
  }
  return false;
}

bool named(const GameCore::Mode mode) {
  switch (mode) {
    case GameCore::Mode::Solo:
    case GameCore::Mode::Pass:
    case GameCore::Mode::Nearby:
      return true;
  }
  return false;
}

// Every named enumerator of a uint8_t-based enum, in value order.
template <typename E>
std::vector<E> enumerators() {
  static_assert(sizeof(E) == 1, "enumerates the 256 values of a uint8_t-based enum");
  std::vector<E> all;
  for (int raw = 0; raw <= 0xFF; ++raw) {
    if (named(static_cast<E>(raw))) all.push_back(static_cast<E>(raw));
  }
  return all;
}

// One event of each kind, and a swipe in each direction, as the runtime builds them.
std::vector<GameCore::GameEvent> everyEvent() {
  std::vector<GameCore::GameEvent> events;
  for (const GameCore::EventKind kind : enumerators<GameCore::EventKind>()) {
    GameCore::GameEvent event{kind, 10, 20};
    event.reason = "not your turn";  // read for Rejected only
    if (kind != GameCore::EventKind::Swipe) {
      events.push_back(event);
      continue;
    }
    for (const GameCore::SwipeDir dir : enumerators<GameCore::SwipeDir>()) {
      if (dir == GameCore::SwipeDir::None) continue;  // the classifier never posts it
      event.dir = dir;
      events.push_back(event);
    }
  }
  return events;
}

TEST_F(ApiSurfaceTest, CtxAndModesMatchTheList) {
  Names ctx;
  Names modes;
  for (const GameCore::Mode mode : enumerators<GameCore::Mode>()) {
    const uint8_t seats = mode == GameCore::Mode::Solo ? 1 : 2;
    DirectGame game(arena, frames, sources, ports, canvas);
    loadSurfaceGame(game);
    log.lines.clear();
    std::span<const uint8_t> state;
    ASSERT_EQ(game.setup(GameCore::GameContext{seats, mode, API_LEVEL}, state), Outcome::Ok) << game.errorMessage();
    for (const std::string& field : tagged(log.lines, "X")) ctx.insert(field);
    for (const std::string& value : tagged(log.lines, "mode")) modes.insert(value);
    EXPECT_TRUE(modes.count(GameCore::modeName(mode))) << GameCore::modeName(mode);
  }
  expectSameNames(ctx, listed("ctx"), "ctx field");
  expectSameNames(modes, listedEnum("mode"), "enum mode value");
}

TEST_F(ApiSurfaceTest, EventsAndDirectionsMatchTheList) {
  DirectGame game(arena, frames, sources, ports, canvas);
  loadSurfaceGame(game);
  std::span<const uint8_t> state;
  ASSERT_EQ(game.setup(GameCore::GameContext{}, state), Outcome::Ok) << game.errorMessage();
  const std::vector<uint8_t> snapshot(state.begin(), state.end());
  log.lines.clear();
  for (const GameCore::GameEvent& event : everyEvent()) {
    std::span<const uint8_t> move;
    ASSERT_EQ(game.input(snapshot, 1, event, move), Outcome::Ok) << game.errorMessage();
  }
  Names events;
  for (const std::string& event : tagged(log.lines, "E")) events.insert(sortedEvent(event));
  Names listedEvents;
  for (const std::string& event : listed("event")) listedEvents.insert(sortedEvent(event));
  expectSameNames(events, listedEvents, "event");
  expectSameNames(tagged(log.lines, "D"), listedEnum("dir"), "enum dir value");
}

TEST_F(ApiSurfaceTest, GfxOptionsMatchTheList) {
  struct Option {
    const char* set;
    const char* const* names;  // null-terminated, what the binding accepts
    const char* call;          // Lua calling ch.gfx with the value v
  };
  const Option options[] = {
      {"color", COLOR_NAMES, "ch.gfx.rect(0, 0, 1, 1, v, true)"},
      {"size", SIZE_NAMES, "ch.gfx.text(0, 0, 'a', v, 'black')"},
      {"align", ALIGN_NAMES, "ch.gfx.text(0, 0, 'a', 'small', 'black', v)"},
      {"refresh", REFRESH_NAMES, "ch.gfx.refresh(v)"},
      {"weight", WEIGHT_NAMES, "ch.gfx.icon('x', 0, 0, 'small', 'black', v)"},
  };
  constexpr const char* UNLISTED = "grey";
  std::string probes;
  for (const Option& option : options) {
    Names accepted;
    for (const char* const* name = option.names; *name; ++name) accepted.insert(*name);
    const Names values = listedEnum(option.set);
    expectSameNames(accepted, values, std::string("enum ") + option.set + " value");
    ASSERT_FALSE(values.count(UNLISTED));
    for (const std::string& value : values) probes += "probe('" + value + "', function(v) " + option.call + " end)\n";
    probes += std::string("probe('") + UNLISTED + "', function(v) " + option.call + " end)\n";
  }
  // Each value through the real binding inside draw: "P <value> <accepted>".
  useSource("main",
            "local probes = {}\n"
            "local function probe(value, call) probes[#probes + 1] = {value, call} end\n" +
                probes +
                "return { setup = function() return {} end, draw = function()\n"
                "  for _, p in ipairs(probes) do print('P ' .. p[1] .. ' ' .. tostring(pcall(p[2], p[1]))) end\n"
                "end }\n");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  log.lines.clear();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  const Names results = tagged(log.lines, "P");
  for (const Option& option : options) {
    for (const std::string& value : listedEnum(option.set)) {
      EXPECT_TRUE(results.count(value + " true")) << "ch.gfx refuses listed " << option.set << " " << value;
    }
  }
  EXPECT_EQ(results.count(std::string(UNLISTED) + " true"), 0u) << "ch.gfx accepts an unlisted value";
}

TEST_F(ApiSurfaceTest, IconsMatchTheList) {
  Names library;
  for (const GameIcons::Icon& icon : GameIcons::ICONS) library.insert(icon.name);
  const Names icons = listed("icon", true);
  expectSameNames(library, icons, "icon");
  ASSERT_FALSE(icons.empty());

  // Each listed name draws through ch.gfx.icon at every listed size in every
  // listed weight, as its own index.
  const Names sizes = listedEnum("size");
  const Names weights = listedEnum("weight");
  ASSERT_EQ(weights, (Names{"regular", "fill"}));
  std::string calls;
  Names every;  // "<size> <weight>"
  for (const std::string& size : sizes) {
    for (const std::string& weight : weights) every.insert(size + " " + weight);
  }
  for (const std::string& name : icons) {
    for (const std::string& size : sizes) {
      for (const std::string& weight : weights) {
        calls += "ch.gfx.icon('" + name + "', 0, 0, '" + size + "', 'black', '" + weight + "')\n";
      }
    }
  }
  useSource("main", "return { setup = function() return {} end, draw = function()\n" + calls + "end }\n");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  std::map<std::string, Names> drawn;  // name -> "<size> <weight>"
  for (const DrawCommand& c : frontCommands()) {
    ASSERT_EQ(c.op, Op::Icon);
    ASSERT_LT(c.icon, GameIcons::ICON_COUNT);
    drawn[GameIcons::ICONS[c.icon].name].insert(std::string(SIZE_NAMES[static_cast<size_t>(c.size)]) + " " +
                                                WEIGHT_NAMES[static_cast<size_t>(c.weight)]);
  }
  for (const std::string& name : icons) {
    EXPECT_EQ(drawn[name], every) << "icon " << name;
  }
}

TEST_F(ApiSurfaceTest, LimitsMatchTheCode) {
  useSource("main",
            "return { setup = function() return {} end, draw = function() end,\n"
            "  input = function() ch.timer.after(1000) ch.timer.after(3000) end }\n");
  DirectGame timerGame(arena, frames, sources, ports, canvas);
  const int pendingTimers = pendingTimerCapacity(timerGame);
  EXPECT_GE(pendingTimers, 0) << "the ch.timer probe did not run: " << timerGame.errorMessage();

  const std::map<std::string, long long> live = {
      {"manifest_id_bytes", GameCore::Manifest::MAX_ID_BYTES},
      {"manifest_name_bytes", GameCore::Manifest::MAX_NAME_BYTES},
      {"manifest_version_bytes", GameCore::Manifest::MAX_VERSION_BYTES},
      {"manifest_icon_bytes", GameCore::Manifest::MAX_ICON_BYTES},
      {"state_bytes", GameCore::SNAPSHOT_BYTES},
      {"move_bytes", GameCore::MOVE_BYTES},
      {"reject_reason_bytes", GameCore::REJECT_REASON_BYTES},
      {"store_bytes", Codec::STORE_LIMIT},
      {"codec_depth_count", Codec::MAX_DEPTH},
      {"frame_commands_count", MAX_COMMANDS},
      {"frame_bytes", MAX_BYTES},
      {"lua_heap_bytes", LUA_HEAP_BYTES},
      {"call_instructions_count", CallGuard::INSTRUCTION_BUDGET},
      {"table_elements_count", TABLE_ELEMENTS_LIMIT},
      {"timer_min_ms", TIMER_MIN_MS},
      {"timers_pending_count", pendingTimers},
      {"images_bytes", GameCore::IMAGES_BYTES},
      {"images_count", GameCore::MAX_IMAGES},
      // lib/lua/library.json's defines, as the Lua build (device and host) compiles them.
      {"c_stack_levels_count", LUA_BUILD_LUAI_MAXCCALLS},
      {"pattern_depth_count", LUA_BUILD_MAXCCALLS},
  };
  const std::map<std::string, std::string> listedLimits = surface.limits();
  Names liveNames;
  Names listedNames;
  for (const auto& [name, value] : live) liveNames.insert(name);
  for (const auto& [name, value] : listedLimits) listedNames.insert(name);
  expectSameNames(liveNames, listedNames, "limit");
  for (const auto& [name, value] : live) {
    const auto entry = listedLimits.find(name);
    if (entry != listedLimits.end()) {
      EXPECT_EQ(entry->second, std::to_string(value)) << "limit " << name;
    }
  }
  // The codec, the Session, and the bindings enforce the same state and move limits.
  EXPECT_EQ(Codec::SNAPSHOT_LIMIT, GameCore::SNAPSHOT_BYTES);
  EXPECT_EQ(Codec::MOVE_LIMIT, GameCore::MOVE_BYTES);
}

}  // namespace
