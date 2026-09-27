#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ArenaAllocator.h"
#include "CallGuard.h"
#include "DisplayList.h"
#include "FrameBuffers.h"
#include "GameInput.h"
#include "GameSources.h"
#include "IClock.h"
#include "IGameLog.h"
#include "IRandom.h"
#include "LuaGame.h"
#include "Session.h"
#include "StoreSlot.h"

namespace GameScriptTestSupport {

class FixedRandom : public GameCore::IRandom {
 public:
  explicit FixedRandom(uint32_t value = 0x12345678u) : value(value) {}
  uint32_t next32() override { return value; }

 private:
  uint32_t value;
};

// A clock the test moves by hand.
class FakeClock : public GameCore::IClock {
 public:
  uint64_t nowMs() const override { return now; }
  void advance(uint64_t ms) { now += ms; }

  uint64_t now = 1000000;  // any start; ch.time.ms counts from load()
};

// Keeps every ch.log and print line, and whether `watched` (when set) was inside a
// locked binding as it wrote.
class CapturingLog : public GameCore::IGameLog {
 public:
  void write(const char* line) override {
    lines.emplace_back(line);
    if (watched) lockedAtWrite.push_back(watched->inLockedBinding());
  }

  std::vector<std::string> lines;
  const GameScript::LuaGame* watched = nullptr;
  std::vector<bool> lockedAtWrite;
};

inline std::string readFixture(const std::string& relative) {
  std::ifstream in(std::string(GAME_SCRIPT_FIXTURES_DIR) + "/" + relative, std::ios::binary);
  EXPECT_TRUE(in.good()) << relative;
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// A LuaGame driven directly, without a Session, for tests of the VM itself (sandbox,
// guard, errors): start() loads main.lua and runs setup, keeping its state's bytes;
// draw() and input() pass them with seat 1. A move input returns is kept, unused.
// When setup fails the snapshot is an empty table, so a test can still draw what
// the script left in its globals.
class DirectGame : public GameScript::LuaGame {
 public:
  using LuaGame::draw;
  using LuaGame::input;
  using LuaGame::LuaGame;

  GameScript::Outcome start() {
    const GameScript::Outcome loaded = load();
    if (loaded != GameScript::Outcome::Ok) return loaded;
    std::span<const uint8_t> state;
    const GameScript::Outcome outcome = setup(GameCore::GameContext{}, state);
    snapshot.assign(state.begin(), state.end());
    if (snapshot.empty()) snapshot = {0x06, 0x00, 0x00};  // {} in codec v1
    return outcome;
  }
  GameScript::Outcome draw() { return LuaGame::draw(snapshot, 1); }
  GameScript::Outcome input(const GameScript::InputEvent& event) {
    std::span<const uint8_t> move;
    const GameScript::Outcome outcome = LuaGame::input(snapshot, 1, event, move);
    lastMove.assign(move.begin(), move.end());
    return outcome;
  }

  std::vector<uint8_t> snapshot;
  std::vector<uint8_t> lastMove;
};

// Owns everything a LuaGame borrows: a malloc'd arena block (the device uses PSRAM),
// two frame lists, and a source table.
class LuaGameTest : public ::testing::Test {
 protected:
  using Module = std::pair<std::string, std::string>;  // name, text

  LuaGameTest()
      : arenaBlock(GameScript::ARENA_BYTES),
        front(GameScript::MAX_BYTES),
        back(GameScript::MAX_BYTES),
        frames(front.data(), back.data(), GameScript::MAX_BYTES) {
    arena.split(arenaBlock.data(), GameScript::LUA_REGION_BYTES, GameScript::SCRATCH_RESERVE_BYTES);
  }

  void useSources(const std::vector<Module>& modules) {
    text.clear();
    spans.assign(modules.size(), GameScript::SourceSpan{});
    for (size_t i = 0; i < modules.size(); ++i) {
      std::strncpy(spans[i].name, modules[i].first.c_str(), GameScript::SourceSpan::MAX_NAME_BYTES);
      spans[i].offset = static_cast<uint32_t>(text.size());
      spans[i].length = static_cast<uint32_t>(modules[i].second.size());
      text += modules[i].second;
    }
    sources.spans = spans.data();
    sources.count = spans.size();
    sources.text = text.data();
  }

  void useSource(const std::string& moduleName, const std::string& source) { useSources({{moduleName, source}}); }

  // test/game_script/fixtures/faults/<name>.lua as main.lua.
  void useFault(const std::string& name) { useSource("main", readFixture("faults/" + name + ".lua")); }

  // The GameVM task's composition (GameVM::run): a solo Session in the arena, taken
  // before the Lua state, over a LuaGame; each step is followed by a draw.
  struct SessionGame {
    SessionGame(LuaGameTest& test)
        : arena(test.arena), game(test.arena, test.frames, test.sources, test.ports, test.canvas) {
      session = arena.create<GameCore::Session>(GameCore::Roster::solo(), game);
    }
    ~SessionGame() {
      game.close();
      arena.destroy(session);
    }
    GameScript::Outcome start() {
      if (!session) return GameScript::Outcome::ScriptError;
      GameScript::Outcome outcome = game.load();
      if (outcome == GameScript::Outcome::Ok) outcome = session->start();
      if (outcome == GameScript::Outcome::Ok) outcome = session->draw();
      return outcome;
    }
    // One event as GameVM handles it: input, the pending move, then a draw.
    GameScript::Outcome step(const GameScript::InputEvent& event) {
      GameScript::Outcome outcome = session->handle(event);
      if (outcome == GameScript::Outcome::Ok) outcome = session->applyPending();
      if (outcome == GameScript::Outcome::Ok) outcome = session->draw();
      return outcome;
    }
    GameScript::Outcome tap(int16_t x, int16_t y) {
      return step(GameScript::InputEvent{GameScript::InputKind::Tap, x, y});
    }
    const char* errorMessage() const { return game.errorMessage(); }

    GameScript::ArenaAllocator& arena;
    GameScript::LuaGame game;
    GameCore::Session* session = nullptr;
  };

  // The GameVM task has VM_STACK_BYTES; model it from the caller's frame down.
  __attribute__((noinline)) static void modelTaskStack(GameScript::LuaGame& game) {
    game.setStackFloor(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) - GameScript::VM_STACK_BYTES);
  }

  // The front frame's commands after the last publish.
  std::vector<GameScript::DrawCommand> frontCommands() {
    std::vector<GameScript::DrawCommand> commands;
    frames.readFront([&](const GameScript::DisplayList& list) {
      auto reader = list.reader();
      GameScript::DrawCommand c;
      while (reader.next(c)) commands.push_back(c);
    });
    return commands;
  }

  // Every text command of the front frame, joined by newlines.
  std::string frontText() {
    std::string joined;
    for (const auto& c : frontCommands()) {
      if (c.op != GameScript::Op::Text) continue;
      if (!joined.empty()) joined += '\n';
      joined += c.text;
    }
    return joined;
  }

  static bool hasText(const std::vector<GameScript::DrawCommand>& commands, const std::string& expected) {
    for (const auto& c : commands) {
      if (c.op == GameScript::Op::Text && expected == c.text) return true;
    }
    return false;
  }

  static bool contains(const char* haystack, const std::string& needle) {
    return std::string(haystack).find(needle) != std::string::npos;
  }

  std::vector<uint8_t> arenaBlock;
  GameScript::ArenaAllocator arena;
  std::vector<uint8_t> front;
  std::vector<uint8_t> back;
  GameScript::FrameBuffers frames;
  FixedRandom random;
  FakeClock clock;
  CapturingLog log;
  std::vector<uint8_t> storeBytes = std::vector<uint8_t>(GameScript::Codec::STORE_LIMIT);
  GameScript::StoreSlot store{storeBytes.data(), storeBytes.size()};
  GameScript::HostPorts ports{random, clock, log, store};
  // The X4 Pro's portrait canvas size, with the stand-in text metrics.
  GameScript::Canvas canvas{480, 800, GameScript::TextMetrics::standIn()};
  std::string text;
  std::vector<GameScript::SourceSpan> spans;
  GameScript::GameSources sources;
};

}  // namespace GameScriptTestSupport
