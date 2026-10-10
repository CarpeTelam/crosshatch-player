#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
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
#include "GameImages.h"
#include "GameInput.h"
#include "GameSources.h"
#include "IClock.h"
#include "IGameLog.h"
#include "IRandom.h"
#include "LuaGame.h"
#include "MatchRounds.h"
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

  uint64_t now =
      1000000;  // any start; ch.time.ms reports play time from load() (these ports have no pause ledger, so all of it)
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
// two frame lists, a source table, and an image table.
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

  // Every image of test/game_script/fixtures/<folder>/ as GameAssets loads them:
  // each *.bmp that imageNameOf names (so never icon.bmp), in name order, added to
  // one GameCore::ImageBudget; a file that fails is a test failure.
  void useImages(const std::string& folder) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::string(GAME_SCRIPT_FIXTURES_DIR) + "/" + folder)) {
      files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    imageSpans.clear();
    imagePixels.clear();
    GameCore::ImageBudget budget;
    for (const auto& file : files) {
      const std::string fileName = file.filename().string();
      char name[GameCore::IMAGE_NAME_BYTES + 1];
      if (!GameCore::imageNameOf(fileName.data(), fileName.size(), name)) continue;
      const std::string bytes = readFixture(folder + "/" + fileName);
      const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
      GameCore::ImageHeader header;
      const GameCore::ImageCheck check = budget.add(data, bytes.size(), bytes.size(), header);
      if (check != GameCore::ImageCheck::Ok) {
        ADD_FAILURE() << folder << "/" << fileName << ": " << GameCore::imageCheckName(check);
        continue;
      }
      GameCore::ImageSpan span{};
      std::memcpy(span.name, name, sizeof(name));
      span.width = header.width;
      span.height = header.height;
      span.rowBytes = header.rowBytes;
      span.offset = static_cast<uint32_t>(imagePixels.size());
      imagePixels.insert(imagePixels.end(), data + GameCore::IMAGE_HEADER_BYTES, data + bytes.size());
      imageSpans.push_back(span);
    }
    images.spans = imageSpans.data();
    images.count = imageSpans.size();
    images.pixels = imagePixels.data();
  }

  // test/game_script/fixtures/faults/<name>.lua as main.lua.
  void useFault(const std::string& name) { useSource("main", readFixture("faults/" + name + ".lua")); }

  // The GameVM task's composition (GameVM::run): a Session of `roster` (solo unless a
  // test gives one) in the arena, taken before the Lua state, over a LuaGame, driven by
  // the production round loop (GameScript::MatchRounds) over its own input queue, so a
  // regression in the loop fails these tests too.
  struct SessionGame {
    explicit SessionGame(LuaGameTest& test, const GameCore::Roster& roster = GameCore::Roster::solo())
        : arena(test.arena), game(test.arena, test.frames, test.sources, test.ports, test.canvas, test.images) {
      session = arena.create<GameCore::Session>(roster, game);
    }
    ~SessionGame() {
      game.close();
      arena.destroy(session);
    }
    // load(), then the first round (MatchRounds::start: setup, status, draw).
    GameScript::Outcome start() {
      if (!session) return GameScript::Outcome::ScriptError;
      const GameScript::Outcome outcome = game.load();
      if (outcome != GameScript::Outcome::Ok) return outcome;
      return rounds.start(*session);
    }
    // One event as GameVM handles it (MatchRounds::step): a stale timer event is
    // dropped, then input, the pending move, and a draw.
    GameScript::Outcome step(const GameScript::InputEvent& event) { return rounds.step(event); }
    // Play again as the match asks for it and GameVM::run answers it: the queue is
    // cleared, then a new round on the same Session.
    GameScript::Outcome playAgain() {
      rounds.requestPlayAgain();
      if (!rounds.takePlayAgain()) return GameScript::Outcome::ScriptError;
      return rounds.restart();
    }
    GameScript::Outcome tap(int16_t x, int16_t y) {
      return step(GameScript::InputEvent{GameScript::InputKind::Tap, x, y});
    }
    const char* errorMessage() const { return game.errorMessage(); }

    GameScript::ArenaAllocator& arena;
    GameScript::LuaGame game;
    GameScript::InputQueue queue;
    GameScript::MatchRounds rounds{game.timer(), queue};
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
  // The 480 x 800 portrait panel (no device's canvas), with the stand-in text metrics.
  GameScript::Canvas canvas{480, 800, GameScript::TextMetrics::standIn()};
  std::string text;
  std::vector<GameScript::SourceSpan> spans;
  GameScript::GameSources sources;
  std::vector<GameCore::ImageSpan> imageSpans;
  std::vector<uint8_t> imagePixels;
  // Empty until useImages.
  GameCore::GameImages images;
};

}  // namespace GameScriptTestSupport
