#include "GameCheck.h"

#include <GameAssets.h>
#include <GameHostCaps.h>
#include <GamePackageInstaller.h>
#include <GameRegistry.h>
#include <HalMemoryStub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MatchStore.h>
#include <Memory.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <lua.hpp>
#include <memory>

#include "RoundFile.h"
#include "RoundPlayer.h"
#include "ScriptVm.h"

namespace fs = std::filesystem;

namespace games_check {

namespace {

constexpr uint32_t CHECKS_SEED = 1;  // C2: checks.lua's math.random is seeded 1, so a check seeds it itself

bool readFile(const fs::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in.good()) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

std::string trimmed(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  return text;
}

std::string hexOf(const uint8_t* bytes, const size_t count) {
  static const char DIGITS[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < count; ++i) {
    out += DIGITS[bytes[i] >> 4];
    out += DIGITS[bytes[i] & 15];
  }
  return out;
}

const char* loadResultName(const GameAssets::LoadResult result) {
  switch (result) {
    case GameAssets::LoadResult::Ok:
      return "Ok";
    case GameAssets::LoadResult::FolderMissing:
      return "the game's folder is missing";
    case GameAssets::LoadResult::NoSources:
      return "the game has no sources";
    case GameAssets::LoadResult::BadSourceName:
      return "no .lua file has a module name";
    case GameAssets::LoadResult::TooLarge:
      return "the sources are too large";
    case GameAssets::LoadResult::OutOfMemory:
      return "out of memory";
    case GameAssets::LoadResult::CannotRead:
      return "a file cannot be read";
    case GameAssets::LoadResult::BadImage:
      return "an image is not usable";
  }
  return "unknown";
}

// The game as the installer wrote it to the fake card and the match would load it.
struct Installed {
  GameRegistry::Entry entry;
  GameCore::CheckResult check;
  std::unique_ptr<GameCore::ManifestReader> reader;
  std::unique_ptr<MatchStore> store;
  std::unique_ptr<GameAssets> assets;
  std::string packerHash;
};

// Packs nothing: reads what pack_games.py wrote for `id`, installs it on an empty fake card, and loads it. False, with
// a failure in `report`, at the first thing that goes wrong.
bool installAndLoad(const Roots& roots, const std::string& id, Installed& out, Report& report) {
  const fs::path packed = roots.packed;
  std::string text;
  if (readFile(packed / (id + ".packerror"), text)) {
    report.fail("scripts/pack_game.py refused games/" + id + "/:\n" + trimmed(text));
    return false;
  }
  std::string package;
  if (!readFile(packed / (id + ".chgame"), package)) {
    report.fail("no package " + id + ".chgame in " + roots.packed + " (pack_games.py did not run for this game)");
    return false;
  }
  if (!readFile(packed / (id + ".hash"), text)) {
    report.fail("no " + id + ".hash in " + roots.packed + ": the packer printed no hash");
    return false;
  }
  out.packerHash = trimmed(text);

  fakesd::reset();
  fakelog::lines.clear();
  fakepsram::reset();
  fakesd::addFile("/games/" + id + ".chgame", package);
  const GamePackageInstaller::Report installed = GamePackageInstaller::installAll();
  if (installed.installed != 1 || installed.failed != 0) {
    std::string detail;
    for (const std::string& line : fakelog::lines) detail += "\n  " + line;
    report.fail("the installer installed " + std::to_string(installed.installed) + " games and rejected " +
                std::to_string(installed.failed) + " (" + GamePackageInstaller::describe(installed.firstError) + ")" +
                detail);
    return false;
  }

  out.reader = makeUniqueNoThrow<GameCore::ManifestReader>();
  out.store = makeUniqueNoThrow<MatchStore>();
  out.assets = makeUniqueNoThrow<GameAssets>();
  if (!out.reader || !out.store || !out.assets) {
    report.fail("out of memory");
    return false;
  }
  if (!GameRegistry::readGame(id.c_str(), *out.reader, out.entry)) {
    report.fail("the installed " + id +
                " is not a game the registry lists (no .pkg, or a manifest that does not parse)");
    return false;
  }
  out.check = out.entry.manifest.check(gameHostCaps());
  if (!out.store->allocate(id.c_str(), 0)) {
    report.fail("out of memory");
    return false;
  }
  const GameAssets::LoadResult loaded = out.assets->load(id.c_str(), out.store->saves(), out.store->slot());
  if (loaded != GameAssets::LoadResult::Ok) {
    report.fail(std::string("GameAssets could not load the installed game: ") + loadResultName(loaded));
    return false;
  }
  return true;
}

// The companion folder's top-level `*.lua` files, the modules a round file or checks.lua may `require`.
bool readModules(const Roots& roots, const std::string& id, std::vector<ModuleText>& out, Report& report) {
  const fs::path folder = fs::path(roots.companion) / id;
  std::error_code error;
  if (!fs::is_directory(folder, error)) return true;
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(folder, error)) {
    if (entry.is_regular_file() && entry.path().extension() == ".lua") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& file : files) {
    ModuleText module;
    module.name = file.stem().string();
    if (!readFile(file, module.text)) {
      report.fail(file.string() + " cannot be read");
      return false;
    }
    out.push_back(std::move(module));
  }
  return true;
}

// What reading checks.lua's list found, and the names of its checks.
struct CheckList {
  std::vector<std::string> names;
  std::string error;
};

// Non-raising reads of the table `checks` returned (ScriptVm::inspect): a list of tables holding a string `name` and a
// function `run`.
bool readCheckList(lua_State* L, CheckList& list) {
  if (!lua_checkstack(L, 12)) {
    list.error = "the Lua stack is exhausted";
    return false;
  }
  const int table = lua_gettop(L);
  if (lua_type(L, table) != LUA_TTABLE) {
    list.error = std::string("checks.lua must return a list of {name, run}, not a ") + luaL_typename(L, table);
    return false;
  }
  const lua_Unsigned count = lua_rawlen(L, table);
  if (count == 0) {
    list.error = "checks.lua must return a non-empty list of {name, run}, and this list is empty";
    return false;
  }
  for (lua_Unsigned i = 1; i <= count; ++i) {
    const std::string at = "check #" + std::to_string(i);
    lua_rawgeti(L, table, static_cast<lua_Integer>(i));
    if (lua_type(L, -1) != LUA_TTABLE) {
      list.error = at + " must be a table {name, run}, not a " + luaL_typename(L, -1);
      lua_pop(L, 1);
      return false;
    }
    std::string name;
    bool hasRun = false;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
      if (lua_type(L, -2) == LUA_TSTRING) {
        const std::string key = lua_tostring(L, -2);
        if (key == "name" && lua_type(L, -1) == LUA_TSTRING) name = lua_tostring(L, -1);
        if (key == "run" && lua_type(L, -1) == LUA_TFUNCTION) hasRun = true;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    if (name.empty() || !hasRun) {
      list.error = at + " must be a table with a non-empty string `name` and a function `run`";
      return false;
    }
    list.names.push_back(name);
  }
  return true;
}

struct CallCheck {
  int list;
  lua_Integer index;
};

}  // namespace

std::string Report::text() const {
  std::string out;
  for (const std::string& line : failures) out += "FAIL " + line + "\n";
  for (const std::string& line : notes) out += "note " + line + "\n";
  return out;
}

std::string testNameOf(const std::string& id, const size_t index) {
  std::string name;
  for (const char c : id) {
    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    name += plain ? c : '_';
  }
  return name + "_" + std::to_string(index);
}

Report checkPackage(const Roots& roots, const std::string& id) {
  Report report;
  // 256 bytes: on the heap, with the rest of the large objects (AGENTS.md).
  auto installed = makeUniqueNoThrow<Installed>();
  if (!installed) {
    report.fail("out of memory");
    return report;
  }
  if (!installAndLoad(roots, id, *installed, report)) return report;
  // Listed through the registry, whole, and with the hash the packer printed: the two implementations of R4 agree.
  GameRegistry::Listing listing;
  if (!GameRegistry::load(listing)) {
    report.fail("GameRegistry::load ran out of memory");
    return report;
  }
  if (listing.count != 1 || std::string(listing.entries[0].manifest.id) != id) {
    report.fail("the registry lists " + std::to_string(listing.count) + " games after installing " + id);
    return report;
  }
  if (!listing.entries[0].check.ok()) {
    report.fail(std::string("the registry lists ") + id +
                " as unavailable on this host: " + GameCore::describe(listing.entries[0].check.reason));
  }
  const std::string installedHash = hexOf(listing.entries[0].pkgHash, sizeof(listing.entries[0].pkgHash));
  if (installedHash != installed->packerHash) {
    report.fail("the installer's package hash " + installedHash + " is not the packer's " + installed->packerHash);
  }
  // A module that loads inside another module's load needs the C stack twice over, and the device refuses a third
  // level; this host sets no stack headroom and would take any depth, so main's own load is probed (ScriptVm.h). A
  // load that raises or faults is no nesting finding: the rounds name it.
  std::vector<ModuleText> none;
  std::string error;
  // The probe runs main.lua's own load, which is the game's: the device's limits.
  auto probe = OwnedVm::create(installed->assets->sources(), none, installed->assets->images(), CHECKS_SEED, error,
                               roots.canvas, VmLimits::device());
  if (!probe) {
    report.fail("the module-loading probe could not start: " + error);
  } else {
    const LoadNesting nesting = probeLoadNesting(*probe);
    if (!nesting.error.empty()) {
      report.fail("the module-loading probe failed: " + nesting.error);
    } else if (nesting.depth > MAX_LOAD_NESTING) {
      // The chain is "main > m1 > m2 > ... > mk": requiring mk, ..., m2 from main.lua first (deepest first), then m1,
      // leaves every later require a cache hit.
      std::vector<std::string> names;
      for (size_t from = 0, at; from <= nesting.chain.size(); from = at + 3) {
        at = nesting.chain.find(" > ", from);
        if (at == std::string::npos) at = nesting.chain.size();
        names.push_back(nesting.chain.substr(from, at - from));
      }
      std::string first;
      for (size_t i = names.size() - 1; i >= 2; --i) first += (first.empty() ? "'" : ", then '") + names[i] + "'";
      report.fail(
          "modules load inside one another while main.lua loads: " + nesting.chain + " (" +
          std::to_string(nesting.depth) + " deep, at most " + std::to_string(MAX_LOAD_NESTING) +
          "). The device refuses a module's load that starts with too little of the VM stack left, the first one at "
          "the third "
          "level (\"require '" +
          names[2] +
          "': script recursion too deep to load a module\"; the fault is final, so no deeper module "
          "is reached). Require " +
          first + " from main.lua before '" + names[1] +
          "' (deepest first, so each later require finds its module loaded), or from a function body (setup, draw, "
          "input), so that no module's own load requires one that is not loaded yet.");
    }
  }
  return report;
}

Report companionHasGame(const Roots& roots, const std::string& id) {
  Report report;
  std::error_code error;
  if (!fs::is_directory(fs::path(roots.games) / id, error)) {
    report.fail("the companion folder " + (fs::path(roots.companion) / id).string() +
                " has no game: " + (fs::path(roots.games) / id).string() + " is missing");
  }
  return report;
}

Report runGameChecks(const Roots& roots, const std::string& id) {
  Report report;
  // 256 bytes: on the heap, with the rest of the large objects (AGENTS.md).
  auto installed = makeUniqueNoThrow<Installed>();
  if (!installed) {
    report.fail("out of memory");
    return report;
  }
  if (!installAndLoad(roots, id, *installed, report)) return report;
  std::vector<ModuleText> modules;
  if (!readModules(roots, id, modules, report)) return report;
  const bool hasChecks =
      std::any_of(modules.begin(), modules.end(), [](const ModuleText& m) { return m.name == "checks"; });
  if (!hasChecks) {
    // The early return stays: with no checks.lua there is no VM to build and nothing to run.
    report.fail((fs::path(roots.companion) / id / "checks.lua").string() +
                " is missing: every first-party game has its own checks (the interface is in "
                "test/game_script/first_party/README.md), so a game with none has no check of its own rules");
    return report;
  }
  std::string error;
  auto owned = OwnedVm::create(installed->assets->sources(), modules, installed->assets->images(), CHECKS_SEED, error,
                               roots.canvas, VmLimits::check());
  if (!owned) {
    report.fail("checks.lua: " + error);
    return report;
  }
  ScriptVm& vm = owned->vm();
  int list = ScriptVm::NO_REF;
  const VmResult loaded = vm.requireModule("checks", list);
  if (!loaded.ok()) {
    report.fail("checks.lua: " + loaded.message);
    return report;
  }
  CheckList checks;
  bool listed = false;
  struct ReadCall {
    CheckList* checks;
    bool* ok;
  } read{&checks, &listed};
  vm.inspect(
      list,
      [](lua_State* L, void* context) {
        auto& read = *static_cast<ReadCall*>(context);
        *read.ok = readCheckList(L, *read.checks);
      },
      &read);
  if (!listed) {
    report.fail("checks.lua: " + checks.error);
    return report;
  }
  size_t ran = 0;
  for (size_t i = 0; i < checks.names.size(); ++i) {
    CallCheck call{list, static_cast<lua_Integer>(i + 1)};
    const VmResult result = vm.protect(
        [](lua_State* L, void* context) {
          auto& call = *static_cast<CallCheck*>(context);
          lua_rawgeti(L, LUA_REGISTRYINDEX, call.list);
          lua_rawgeti(L, -1, call.index);
          lua_getfield(L, -1, "run");
          lua_call(L, 0, 0);
        },
        &call);
    if (result.ok()) {
      ++ran;
      continue;
    }
    if (!result.fault()) {
      report.fail("check '" + checks.names[i] + "' failed: " + result.message);
      continue;
    }
    // A guard fault is what ends a game: it fails this check and stops the rest, which are counted.
    const size_t remaining = checks.names.size() - i - 1;
    report.fail("check '" + checks.names[i] + "' faulted: " + result.message + "; the remaining " +
                std::to_string(remaining) + " of the game's " + std::to_string(checks.names.size()) +
                " checks were not run");
    break;
  }
  report.note(id + ": " + std::to_string(ran) + " of " + std::to_string(checks.names.size()) + " checks passed");
  return report;
}

Report playRounds(const Roots& roots, const std::string& id, RoundDetails* details, const PlayOptions& options) {
  Report report;
  // 256 bytes: on the heap, with the rest of the large objects (AGENTS.md).
  auto installed = makeUniqueNoThrow<Installed>();
  if (!installed) {
    report.fail("out of memory");
    return report;
  }
  if (!installAndLoad(roots, id, *installed, report)) return report;
  const GameCore::Manifest& manifest = installed->entry.manifest;
  const struct {
    GameCore::Manifest::Mode bit;
    const char* name;
  } modes[] = {{GameCore::Manifest::MODE_SOLO, "solo"},
               {GameCore::Manifest::MODE_PASS, "pass"},
               {GameCore::Manifest::MODE_NEARBY, "nearby"}};
  for (const auto& mode : modes) {
    if (manifest.hasMode(mode.bit) && (installed->check.modes & mode.bit) == 0) {
      report.note(id + ": declared mode " + mode.name + " skipped: this host cannot start it");
    }
  }

  const fs::path folder = fs::path(roots.companion) / id / "rounds";
  std::vector<fs::path> files;
  std::vector<std::string> misnamed;
  std::error_code listing;
  for (const auto& entry : fs::directory_iterator(folder, listing)) {
    const std::string file = entry.path().filename().string();
    // A round is a regular file named <name>.lua: not `x.LUA`, `x.lua.txt`, `.lua`, a directory, or anything else,
    // which a skipped entry would leave unplayed and unnoticed beside the valid rounds.
    const bool named = file.size() > 4 && file.compare(file.size() - 4, 4, ".lua") == 0;
    if (entry.is_regular_file() && named) {
      files.push_back(entry.path());
    } else {
      misnamed.push_back(file + (entry.is_directory() ? "/" : ""));
    }
  }
  std::sort(files.begin(), files.end());
  std::sort(misnamed.begin(), misnamed.end());
  for (const std::string& entry : misnamed) {
    report.fail("rounds/" + entry + " is not a round: an entry of " + folder.string() +
                " must be a regular file whose name ends exactly `.lua`, so it is never silently skipped");
  }
  if (files.empty()) {
    report.fail("no rounds: " + folder.string() + " is missing or holds no .lua file");
    return report;
  }
  std::vector<ModuleText> modules;
  if (!readModules(roots, id, modules, report)) return report;

  GameUnderCheck under(roots.canvas);
  under.sources = &installed->assets->sources();
  under.images = &installed->assets->images();
  under.facts.manifest = &manifest;
  under.facts.settings = &installed->reader->settings();
  under.facts.hostMaxSeats = gameHostCaps().maxSeats;
  under.facts.hostModes = installed->check.modes;

  // A name clash between a companion module and the game's own is the folder's fault, not a round's: said once.
  {
    std::string clash;
    if (!OwnedVm::create(*under.sources, modules, *under.images, 1, clash, roots.canvas, VmLimits::check())) {
      report.fail(id + ": " + clash);
      return report;
    }
  }
  for (const fs::path& file : files) {
    std::string text;
    const std::string name = file.stem().string();
    if (!readFile(file, text)) {
      report.fail("round '" + name + "': " + file.string() + " cannot be read");
      continue;
    }
    std::string error;
    auto vm = OwnedVm::create(*under.sources, modules, *under.images, 1, error, roots.canvas, VmLimits::check());
    if (!vm) {
      report.fail("round '" + name + "': " + error);
      continue;
    }
    Round round;
    if (!loadRound(std::move(vm), name, text, under.facts, round, error)) {
      report.fail(error);
      continue;
    }
    RoundReport played = playRound(round, under, options);
    for (const std::string& failure : played.failures) report.fail(failure);
    if (played.ok()) {
      report.note("round '" + name + "' (" + GameCore::modeName(round.mode) + ") played to its end");
    }
    if (details) details->emplace_back(name, std::move(played));
  }
  return report;
}

}  // namespace games_check
