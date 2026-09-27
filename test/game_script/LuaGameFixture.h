#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ArenaAllocator.h"
#include "CallGuard.h"
#include "DisplayList.h"
#include "FrameBuffers.h"
#include "GameSources.h"
#include "IRandom.h"
#include "LuaGame.h"

namespace GameScriptTestSupport {

class FixedRandom : public GameCore::IRandom {
 public:
  explicit FixedRandom(uint32_t value = 0x12345678u) : value(value) {}
  uint32_t next32() override { return value; }

 private:
  uint32_t value;
};

inline std::string readFixture(const std::string& relative) {
  std::ifstream in(std::string(GAME_SCRIPT_FIXTURES_DIR) + "/" + relative, std::ios::binary);
  EXPECT_TRUE(in.good()) << relative;
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

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
    arena.reset(arenaBlock.data(), arenaBlock.size());
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
  std::string text;
  std::vector<GameScript::SourceSpan> spans;
  GameScript::GameSources sources;
};

}  // namespace GameScriptTestSupport
