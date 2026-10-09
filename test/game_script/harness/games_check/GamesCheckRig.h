#pragma once

// Everything one LuaGame or ScriptVm borrows, owned in one place: the arena block the Lua state's heap lives in, the
// two frame lists, ch.store's slot, the host ports (a seeded random, a clock the steps move, a log that keeps every
// ch.log line), and the device canvas. The wiring is LuaGameTest's (test/game_script/LuaGameFixture.h), copied here
// because the check cannot derive from a gtest fixture; the canvas is the device's logical one, not the fixture's 480 x
// 800 (api-level-1.txt's frame_icon_image_pixels note): the Sticky's 474 x 788 and the X4 Pro's 466 x 788, the bezel
// insets GameViewport::forRenderer subtracts ({9, 3, 3, 3} and {9, 7, 3, 7}, BoardConfig.h). The check plays every game
// on both (the owner's Decision of 2026-10-05); each size is a stand-in for a board's insets and says so below.
//
// The Lua heap and the instruction budget are a VmLimits the caller names (there is no default): the device's for the
// game itself and the package-load probe, and the check's own, larger ones for the VMs that run only check code.
//
// A test double: the host canvas measures text with the harness's stand-in metrics, so a game that fits its text on
// this canvas may still overflow on the device's fonts (the device run proves that, not this check).

#include <ArenaAllocator.h>
#include <CallGuard.h>
#include <FrameBuffers.h>
#include <IClock.h>
#include <IGameLog.h>
#include <LuaGame.h>
#include <Memory.h>
#include <PauseClock.h>
#include <StoreSlot.h>

#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "HostBounds.h"
#include "SeededRandom.h"

namespace games_check {

// The bezel insets of a board, in the panel's native portrait frame (BoardConfig.h's ViewableInsets, field for field).
// Typed here, since the host cannot read a board's profile into a renderer; GamesCheckEngineTest's
// BoardInsetsTest.AreTheSdksBoardProfilesInsets compiles the SDK's header and pins these to it, so a change of an inset
// there fails this check rather than leaving it playing a canvas no device has.
struct BezelInsets {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
};
// The Sticky's, which are the SDK's default ViewableInsets: {9, 3, 3, 3}.
inline constexpr BezelInsets STICKY_INSETS{9, 3, 3, 3};
// The X4 Pro's (XTEINK_X4_PRO): {9, 7, 3, 7}.
inline constexpr BezelInsets X4_PRO_INSETS{9, 7, 3, 7};
// The panel every game's board has: 480 x 800 portrait.
inline constexpr int PANEL_WIDTH = 480;
inline constexpr int PANEL_HEIGHT = 800;

// The canvas a game sees: `ch.screen`. A double of GameViewport::forRenderer on a board, which the host cannot call for
// a board it is not (the stub renderer has one set of insets): the panel less the board's bezel insets. A test pins
// that the two sizes are the viewports of the two boards' insets (GamesCheckFlowTest.cpp,
// CanvasSizesTest.AreTheViewportsOfTheTwoBoardsInsets) and that the insets are the SDK's (BoardInsetsTest).
struct CanvasSize {
  int16_t width = 0;
  int16_t height = 0;
};
constexpr CanvasSize canvasOf(const BezelInsets insets) {
  return {static_cast<int16_t>(PANEL_WIDTH - insets.left - insets.right),
          static_cast<int16_t>(PANEL_HEIGHT - insets.top - insets.bottom)};
}
// The Sticky's (and the default profile's): the 480 x 800 panel less insets {9, 3, 3, 3}.
inline constexpr CanvasSize CANVAS_474 = canvasOf(STICKY_INSETS);
// The X4 Pro's: the panel less insets {9, 7, 3, 7}. Narrower than 474 by the 4 px of its two wider side bezels.
inline constexpr CanvasSize CANVAS_466 = canvasOf(X4_PRO_INSETS);
static_assert(CANVAS_474.width == 474 && CANVAS_474.height == host::CANVAS_HEIGHT, "the Sticky's canvas is 474 x 788");
static_assert(CANVAS_466.width == 466 && CANVAS_466.height == host::CANVAS_HEIGHT, "the X4 Pro's canvas is 466 x 788");

// What bounds one VM: the Lua heap cap and Lua's region of the arena, and the instructions one entry into Lua may
// spend. `device` is the device's (GameScript::LUA_HEAP_BYTES, LUA_REGION_BYTES, CallGuard::INSTRUCTION_BUDGET): the VM
// of the game itself (RoundPlayer's played LuaGame) and of the package-load probe keep it. `check` is a VM that runs
// only the check's own code (host::CHECK_* in HostBounds.h): a round file and its `steps(state)`, a game's checks.lua,
// the clash probe; its ScriptVm also gets the Lua global `host` and `within_device_budget`. `deviceLess` is the
// device's with the heap cap lowered by `margin`, for the played game's margin gate (RoundPlayer.h).
struct VmLimits {
  size_t luaHeapBytes = 0;
  size_t luaRegionBytes = 0;
  uint32_t instructionBudget = 0;
  // True for a VM that runs only check code.
  bool checkCode = false;

  static constexpr VmLimits device() {
    return {GameScript::LUA_HEAP_BYTES, GameScript::LUA_REGION_BYTES, GameScript::CallGuard::INSTRUCTION_BUDGET, false};
  }
  static constexpr VmLimits deviceLess(const size_t margin) {
    return {GameScript::LUA_HEAP_BYTES - margin, GameScript::LUA_REGION_BYTES,
            GameScript::CallGuard::INSTRUCTION_BUDGET, false};
  }
  static constexpr VmLimits check() {
    return {host::CHECK_LUA_HEAP_BYTES, host::CHECK_LUA_REGION_BYTES, host::CHECK_INSTRUCTION_BUDGET, true};
  }
};

// "The played game has the device's limits" rests on these, not on today's constants: the device's VmLimits are the
// arena the device allocates (Lua's region and the reserve) and the cap and budget the sandbox enforces.
static_assert(VmLimits::device().luaRegionBytes + GameScript::SCRATCH_RESERVE_BYTES == GameScript::ARENA_BYTES,
              "the device's limits are the device's arena");
static_assert(VmLimits::device().luaHeapBytes == GameScript::LUA_HEAP_BYTES, "the device's Lua heap cap");
static_assert(VmLimits::device().instructionBudget == GameScript::CallGuard::INSTRUCTION_BUDGET,
              "the device's instruction budget");

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
  // Null when the arena or a buffer could not be allocated (no exceptions: new (std::nothrow)). `canvas` and `limits`
  // have no default: a caller that forgot one would silently play the Sticky's canvas at the device's limits.
  static std::unique_ptr<GamesCheckRig> create(const uint32_t seed, const CanvasSize canvas, const VmLimits& limits) {
    auto rig = std::unique_ptr<GamesCheckRig>(new (std::nothrow) GamesCheckRig(seed, canvas, limits));
    if (!rig || !rig->allocated()) return nullptr;
    rig->arenaAllocator.split(rig->arenaBlock.get(), limits.luaRegionBytes, GameScript::SCRATCH_RESERVE_BYTES);
    rig->arenaAllocator.setLuaLimit(limits.luaHeapBytes);
    return rig;
  }

  GameScript::ArenaAllocator& arena() { return arenaAllocator; }
  GameScript::FrameBuffers& frames() { return frameBuffers; }
  const GameScript::HostPorts& ports() const { return hostPorts; }
  const GameScript::Canvas& canvas() const { return hostCanvas; }
  RigClock& clock() { return rigClock; }
  // The ledger ch.time.ms reads through HostPorts::paused, as the device's does; nothing pauses unless a step calls it.
  GameCore::PauseClock& pauses() { return pauseClock; }
  RigLog& log() { return rigLog; }

 private:
  GamesCheckRig(const uint32_t seed, const CanvasSize canvas, const VmLimits& limits)
      : arenaBlock(makeUniqueNoThrow<uint8_t[]>(limits.luaRegionBytes + GameScript::SCRATCH_RESERVE_BYTES)),
        front(makeUniqueNoThrow<uint8_t[]>(GameScript::MAX_BYTES)),
        back(makeUniqueNoThrow<uint8_t[]>(GameScript::MAX_BYTES)),
        storeBytes(makeUniqueNoThrow<uint8_t[]>(GameScript::Codec::STORE_LIMIT)),
        frameBuffers(front.get(), back.get(), GameScript::MAX_BYTES),
        random(seed),
        slot(storeBytes.get(), GameScript::Codec::STORE_LIMIT),
        hostPorts{random, rigClock, rigLog, slot, &pauseClock},
        hostCanvas{canvas.width, canvas.height, GameScript::TextMetrics::standIn()} {}

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
  GameCore::PauseClock pauseClock;
  GameScript::StoreSlot slot;
  GameScript::HostPorts hostPorts;
  GameScript::Canvas hostCanvas;
};

}  // namespace games_check
