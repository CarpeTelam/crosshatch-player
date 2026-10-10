#pragma once

// Simulator only, for the simulator_x4pro env: simulator.ini force-includes this header (`-include`) in every
// translation unit, ahead of everything else. (An `-I` folder does not work: PlatformIO puts a project's `-I` after the
// library folders, so the crosspoint-simulator library's own BoardConfig.h is found first.)
//
// The library's X4 Pro profile leaves the bezel insets at the default {9, 3, 3, 3} (top, right, bottom, left), so a
// simulated X4 Pro gives a game a 474 x 788 canvas. The device's profile (freeink-sdk BoardConfig.h, XTEINK_X4_PRO)
// sets {9, 7, 3, 7}, which GameViewport::forRenderer subtracts from 480 x 800 to give the 466 x 788 canvas a game sees
// on the X4 Pro (the owner's Decision of 2026-10-05, epic-first-party-games). This header includes the library's header
// itself with `selectDevice` renamed, then defines `selectDevice` to give the simulated X4 Pro those insets whenever
// the library selects the profile (HalGPIO::begin does, at startup), so a screenshot shows the device's canvas. The
// library, the SDK, and the firmware sources are untouched; the Sticky keeps the default insets, which are its own.
//
// A double of the SDK's X4 Pro profile, in one value: if the SDK's insets change, this one does not follow. The games
// check types the same {9, 7, 3, 7} (GamesCheckRig.h's X4_PRO_INSETS) and a host test pins that copy to the SDK's own
// header (test/game_script/harness/games_check/BoardInsetsTest.cpp, AreTheSdksBoardProfilesInsets); this literal is not
// read by it, so a change of the SDK's insets fails that test and is then made here by hand.
//
// When this header is in effect the simulator's startup log has a "[SIM] X4 Pro bezel insets" line, which is how a
// build is known to use it. Every translation unit gets the header, so one that cannot see the library's BoardConfig.h
// (a C file, a library with its own include path) skips it.
#if defined(__cplusplus) && __has_include(<BoardConfig.h>)
#define selectDevice crosshatchSimSelectDeviceFromLibrary
#include <BoardConfig.h>
#undef selectDevice

#include <cstdio>

namespace BoardConfig {

inline bool selectDevice(Board board) {
  const bool selected = crosshatchSimSelectDeviceFromLibrary(board);
  if (selected && board == Board::XteinkX4Pro) {
    ACTIVE.viewableInsets = {9, 7, 3, 7};
    std::fprintf(stderr, "[SIM] X4 Pro bezel insets {9, 7, 3, 7}: game canvas 466 x 788 at (7, 9)\n");
  }
  return selected;
}

}  // namespace BoardConfig
#endif  // C++ translation units that can see the library's BoardConfig.h (a library without it needs no insets)
