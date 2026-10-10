// The games check plays every game on two canvases, the Sticky's 474 x 788 and the X4 Pro's 466 x 788
// (GamesCheckRig.h), which are the 480 x 800 panel less each board's bezel insets. The harness cannot ask a board
// profile for its insets, so it types them (STICKY_INSETS, X4_PRO_INSETS), and the simulator's shim
// (.claude/skills/run-crosshatch-player/shim/BoardConfig.h) and Sudoku's tile phase comment (games/sudoku/layout.lua)
// say the same numbers again. This test compiles the SDK's own BoardConfig.h (through the stubs in board_stubs/, which
// stand in for the Arduino core only) and pins the typed copies to it, so an inset changed in the SDK fails here
// instead of leaving the check playing a canvas no board has. The SDK's constants are read from its header; the
// literals below ({9, 7, 3, 7}, {9, 3, 3, 3}, 466, 474) are the values the SDK is expected to hold, so a change in the
// SDK must change them here. The simulator's shim literal and layout.lua's parity comment are named by this test but
// not read by it.

#include <BoardConfig.h>
#include <gtest/gtest.h>

#include "GamesCheckRig.h"

namespace {

using games_check::BezelInsets;
using games_check::CanvasSize;

BezelInsets insetsOf(const BoardConfig::ViewableInsets& insets) {
  return {insets.top, insets.right, insets.bottom, insets.left};
}

CanvasSize canvasOfInsets(const BoardConfig::ViewableInsets& insets) {
  return {static_cast<int16_t>(480 - insets.left - insets.right),
          static_cast<int16_t>(800 - insets.top - insets.bottom)};
}

// A game's canvas begins at (left, top) of the panel; the note tiles' dither is black where screen x + y is even, so a
// tile placed at an even x + y on the canvas continues it only when the canvas's own origin has an even x + y
// (games/sudoku/layout.lua note_tile). Both boards must keep that parity.
bool originIsEven(const BoardConfig::ViewableInsets& insets) { return (insets.left + insets.top) % 2 == 0; }

}  // namespace

TEST(BoardInsetsTest, AreTheSdksBoardProfilesInsets) {
  const BoardConfig::ViewableInsets x4pro = BoardConfig::XTEINK_X4_PRO.viewableInsets;
  const BoardConfig::ViewableInsets standard{};  // the struct's default: every profile that does not override it
  // The SDK's own values, as of the submodule's pin: the X4 Pro's {9, 7, 3, 7} and the default {9, 3, 3, 3}.
  EXPECT_EQ(x4pro.top, 9);
  EXPECT_EQ(x4pro.right, 7);
  EXPECT_EQ(x4pro.bottom, 3);
  EXPECT_EQ(x4pro.left, 7);
  EXPECT_EQ(standard.top, 9);
  EXPECT_EQ(standard.right, 3);
  EXPECT_EQ(standard.bottom, 3);
  EXPECT_EQ(standard.left, 3);
  // The Sticky keeps the default (its own profile sets none), so the harness's Sticky canvas is the default's.
  const BoardConfig::ViewableInsets sticky = BoardConfig::STICKY.viewableInsets;
  EXPECT_EQ(sticky.top, standard.top);
  EXPECT_EQ(sticky.right, standard.right);
  EXPECT_EQ(sticky.bottom, standard.bottom);
  EXPECT_EQ(sticky.left, standard.left);

  // The harness's typed insets are those.
  for (const auto& [typed, sdk] :
       {std::pair{games_check::X4_PRO_INSETS, x4pro}, std::pair{games_check::STICKY_INSETS, sticky}}) {
    const BezelInsets read = insetsOf(sdk);
    EXPECT_EQ(typed.top, read.top);
    EXPECT_EQ(typed.right, read.right);
    EXPECT_EQ(typed.bottom, read.bottom);
    EXPECT_EQ(typed.left, read.left);
  }

  // The canvases the check plays are the panel less those: 480 - left - right by 800 - top - bottom.
  EXPECT_EQ(canvasOfInsets(x4pro).width, 466);
  EXPECT_EQ(canvasOfInsets(x4pro).height, 788);
  EXPECT_EQ(canvasOfInsets(standard).width, 474);
  EXPECT_EQ(canvasOfInsets(standard).height, 788);
  EXPECT_EQ(games_check::CANVAS_466.width, canvasOfInsets(x4pro).width);
  EXPECT_EQ(games_check::CANVAS_466.height, canvasOfInsets(x4pro).height);
  EXPECT_EQ(games_check::CANVAS_474.width, canvasOfInsets(sticky).width);
  EXPECT_EQ(games_check::CANVAS_474.height, canvasOfInsets(sticky).height);

  // The panel is the one the canvases are taken from: the profile's glass is 800 x 480 landscape, and the insets are in
  // its native portrait frame, 480 wide by 800 tall (GamesCheckRig.h's PANEL_WIDTH and PANEL_HEIGHT).
  EXPECT_EQ(BoardConfig::XTEINK_X4_PRO.displayHeight, games_check::PANEL_WIDTH);
  EXPECT_EQ(BoardConfig::XTEINK_X4_PRO.displayWidth, games_check::PANEL_HEIGHT);
  EXPECT_EQ(BoardConfig::STICKY.displayHeight, games_check::PANEL_WIDTH);
  EXPECT_EQ(BoardConfig::STICKY.displayWidth, games_check::PANEL_HEIGHT);
}

TEST(BoardInsetsTest, EveryGameCanvasOriginHasAnEvenXPlusY) {
  EXPECT_TRUE(originIsEven(BoardConfig::XTEINK_X4_PRO.viewableInsets));  // (7, 9)
  EXPECT_TRUE(originIsEven(BoardConfig::STICKY.viewableInsets));         // (3, 9)
  EXPECT_TRUE(originIsEven(BoardConfig::ViewableInsets{}));
}
