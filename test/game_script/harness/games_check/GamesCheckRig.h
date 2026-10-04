#pragma once

// Everything one LuaGame or ScriptVm borrows, owned in one place: the arena block the Lua state's heap lives in, the
// two frame lists, ch.store's slot, the host ports (a seeded random, a clock the steps move, a log that keeps every
// ch.log line), and the device canvas. The wiring is LuaGameTest's (test/game_script/LuaGameFixture.h), copied here
// because the check cannot derive from a gtest fixture; the canvas is the X4 Pro's and the Sticky's logical one, 474 x
// 788 (api-level-1.txt's frame_icon_image_pixels note), not the fixture's 480 x 800.
//
// A test double: the host canvas measures text with the harness's stand-in metrics, so a game that fits its text on
// this canvas may still overflow on the device's fonts (the device run proves that, not this check).

#include <ArenaAllocator.h>
#include <FrameBuffers.h>
#include <IClock.h>
#include <IGameLog.h>
#include <LuaGame.h>
#include <Memory.h>
#include <StoreSlot.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SeededRandom.h"

namespace games_check {

// The canvas a game sees on the X4 Pro and the Sticky.
inline constexpr int16_t CANVAS_WIDTH = 474;
inline constexpr int16_t CANVAS_HEIGHT = 788;

// A clock the steps move (a step's `wait`); ch.time.ms counts from the game's load.
class RigClock final : public GameCore::IClock {
 public:
  uint64_t nowMs() const override { return now; }
  void advance(const uint64_t ms) { now += ms; }

 private:
  uint64_t now = 1000000;  // any start
};

// Keeps every ch.log and print line, in order.
class RigLog final : public GameCore::IGameLog {
 public:
  void write(const char* line) override { lines.emplace_back(line); }
  std::vector<std::string> lines;
};

class GamesCheckRig {
 public:
  // Null when the arena or a buffer could not be allocated (no exceptions: new (std::nothrow)).
  static std::unique_ptr<GamesCheckRig> create(const uint32_t seed) {
    auto rig = std::unique_ptr<GamesCheckRig>(new (std::nothrow) GamesCheckRig(seed));
    if (!rig || !rig->allocated()) return nullptr;
    rig->arenaAllocator.split(rig->arenaBlock.get(), GameScript::LUA_REGION_BYTES, GameScript::SCRATCH_RESERVE_BYTES);
    return rig;
  }

  GameScript::ArenaAllocator& arena() { return arenaAllocator; }
  GameScript::FrameBuffers& frames() { return frameBuffers; }
  const GameScript::HostPorts& ports() const { return hostPorts; }
  const GameScript::Canvas& canvas() const { return hostCanvas; }
  RigClock& clock() { return rigClock; }
  RigLog& log() { return rigLog; }

 private:
  explicit GamesCheckRig(const uint32_t seed)
      : arenaBlock(makeUniqueNoThrow<uint8_t[]>(GameScript::ARENA_BYTES)),
        front(makeUniqueNoThrow<uint8_t[]>(GameScript::MAX_BYTES)),
        back(makeUniqueNoThrow<uint8_t[]>(GameScript::MAX_BYTES)),
        storeBytes(makeUniqueNoThrow<uint8_t[]>(GameScript::Codec::STORE_LIMIT)),
        frameBuffers(front.get(), back.get(), GameScript::MAX_BYTES),
        random(seed),
        slot(storeBytes.get(), GameScript::Codec::STORE_LIMIT),
        hostPorts{random, rigClock, rigLog, slot} {}

  bool allocated() const { return arenaBlock && front && back && storeBytes; }

  std::unique_ptr<uint8_t[]> arenaBlock;
  std::unique_ptr<uint8_t[]> front;
  std::unique_ptr<uint8_t[]> back;
  std::unique_ptr<uint8_t[]> storeBytes;
  GameScript::ArenaAllocator arenaAllocator;
  GameScript::FrameBuffers frameBuffers;
  SeededRandom random;
  RigClock rigClock;
  RigLog rigLog;
  GameScript::StoreSlot slot;
  GameScript::HostPorts hostPorts;
  GameScript::Canvas hostCanvas{CANVAS_WIDTH, CANVAS_HEIGHT, GameScript::TextMetrics::standIn()};
};

}  // namespace games_check
