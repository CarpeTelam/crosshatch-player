#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "IGameRules.h"
#include "MatchLifecycle.h"
#include "Roster.h"
#include "SeatShown.h"

// GameCore::seatShown (which seat's frame a device shows), Roster::pass, and passSeats
// (epic-pass-and-play entries 1 and 2). Pass runs n = 2 and n = 3: nothing assumes two seats.

using namespace GameCore;

namespace {

Status playing(const uint8_t turn) {
  Status status;
  status.turn = turn;
  return status;
}

Status over(const uint16_t winners) {
  Status status;
  status.over = true;
  status.winners = winners;
  return status;
}

// A two-seat roster this device plays seat 2 of (nearby's shape).
Roster secondSeatOnly() {
  Roster roster;
  roster.mode = Mode::Nearby;
  roster.seats = 2;
  roster.localSeats = 0b10;
  return roster;
}

struct Row {
  const char* what;
  Roster roster;
  MatchState state;
  Status status;
  uint8_t shown;
};

TEST(SeatShownTest, EachRosterAndStateShowsItsSeat) {
  const std::vector<Row> rows = {
      {"pass 2, playing, turn 1", Roster::pass(2), MatchState::Playing, playing(1), 1},
      {"pass 2, playing, turn 2", Roster::pass(2), MatchState::Playing, playing(2), 2},
      {"pass 2, over", Roster::pass(2), MatchState::Over, over(0b01), 0},
      {"pass 2, playing with the status over", Roster::pass(2), MatchState::Playing, over(0b10), 0},
      {"pass 3, playing, turn 3", Roster::pass(3), MatchState::Playing, playing(3), 3},
      {"pass 3, playing, turn 2", Roster::pass(3), MatchState::Playing, playing(2), 2},
      {"pass 3, over", Roster::pass(3), MatchState::Over, over(0), 0},
      {"pass 3, playing with the status over", Roster::pass(3), MatchState::Playing, over(0b100), 0},
      {"pass 3, paused", Roster::pass(3), MatchState::Paused, playing(3), 3},
      {"seat 2 only, playing, turn 1", secondSeatOnly(), MatchState::Playing, playing(1), 2},
      {"seat 2 only, playing, turn 2", secondSeatOnly(), MatchState::Playing, playing(2), 2},
      {"seat 2 only, over", secondSeatOnly(), MatchState::Over, over(0b01), 2},
      {"seat 2 only, playing with the status over", secondSeatOnly(), MatchState::Playing, over(0b01), 2},
      {"solo, playing", Roster::solo(), MatchState::Playing, playing(1), 1},
      {"solo, over", Roster::solo(), MatchState::Over, over(0b01), 1},
  };
  for (const Row& row : rows) {
    EXPECT_EQ(seatShown(row.state, row.roster, row.status), row.shown) << row.what;
  }
}

struct MoverRow {
  const char* what;
  Roster roster;
  MatchState state;
  Status status;
  uint8_t mover;
  uint8_t shown;
};

// Every state (entry 2's Result and HandOff included) for a hidden pass match of 2 and of 3
// seats, an open pass match, and a one-local-seat roster. A hidden pass match passes the seat
// that just moved in every state; only Result shows it.
TEST(SeatShownTest, EveryStateShowsItsSeatWithTheMover) {
  const std::vector<MoverRow> rows = {
      {"hidden pass 2, starting", Roster::pass(2), MatchState::Starting, playing(1), 2, 1},
      {"hidden pass 2, playing", Roster::pass(2), MatchState::Playing, playing(1), 2, 1},
      {"hidden pass 2, paused", Roster::pass(2), MatchState::Paused, playing(1), 2, 1},
      {"hidden pass 2, over", Roster::pass(2), MatchState::Over, over(0b10), 2, 0},
      {"hidden pass 2, error", Roster::pass(2), MatchState::Error, playing(1), 2, 1},
      {"hidden pass 2, leaving", Roster::pass(2), MatchState::Leaving, playing(1), 2, 1},
      {"hidden pass 2, result, mover 1", Roster::pass(2), MatchState::Result, playing(2), 1, 1},
      {"hidden pass 2, result, mover 2", Roster::pass(2), MatchState::Result, playing(1), 2, 2},
      {"hidden pass 2, result, no mover", Roster::pass(2), MatchState::Result, playing(2), NO_SEAT, NO_SEAT},
      {"hidden pass 2, result, mover 3 not local", Roster::pass(2), MatchState::Result, playing(1), 3, NO_SEAT},
      {"hidden pass 2, result, mover 0", Roster::pass(2), MatchState::Result, playing(1), 0, NO_SEAT},
      {"hidden pass 2, hand-off", Roster::pass(2), MatchState::HandOff, playing(2), 1, NO_SEAT},
      {"hidden pass 2, hand-off, no mover", Roster::pass(2), MatchState::HandOff, playing(1), NO_SEAT, NO_SEAT},
      {"hidden pass 2, result, the status over", Roster::pass(2), MatchState::Result, over(0b01), 1, 0},
      {"hidden pass 2, hand-off, the status over", Roster::pass(2), MatchState::HandOff, over(0b01), 1, NO_SEAT},
      {"hidden pass 3, starting", Roster::pass(3), MatchState::Starting, playing(1), 3, 1},
      {"hidden pass 3, playing", Roster::pass(3), MatchState::Playing, playing(3), 2, 3},
      {"hidden pass 3, paused", Roster::pass(3), MatchState::Paused, playing(3), 2, 3},
      {"hidden pass 3, over", Roster::pass(3), MatchState::Over, over(0b100), 3, 0},
      {"hidden pass 3, error", Roster::pass(3), MatchState::Error, playing(2), 1, 2},
      {"hidden pass 3, leaving", Roster::pass(3), MatchState::Leaving, playing(2), 1, 2},
      {"hidden pass 3, result, mover 3", Roster::pass(3), MatchState::Result, playing(1), 3, 3},
      {"hidden pass 3, result, mover 2", Roster::pass(3), MatchState::Result, playing(3), 2, 2},
      {"hidden pass 3, result, no mover", Roster::pass(3), MatchState::Result, playing(1), NO_SEAT, NO_SEAT},
      {"hidden pass 3, result, mover 4 not local", Roster::pass(3), MatchState::Result, playing(1), 4, NO_SEAT},
      {"hidden pass 3, hand-off", Roster::pass(3), MatchState::HandOff, playing(3), 2, NO_SEAT},
      {"open pass 2, starting", Roster::pass(2), MatchState::Starting, playing(1), NO_SEAT, 1},
      {"open pass 2, playing", Roster::pass(2), MatchState::Playing, playing(2), NO_SEAT, 2},
      {"open pass 2, paused", Roster::pass(2), MatchState::Paused, playing(2), NO_SEAT, 2},
      {"open pass 2, over", Roster::pass(2), MatchState::Over, over(0b01), NO_SEAT, 0},
      {"open pass 2, error", Roster::pass(2), MatchState::Error, playing(2), NO_SEAT, 2},
      {"open pass 2, leaving", Roster::pass(2), MatchState::Leaving, playing(1), NO_SEAT, 1},
      // An open pass match never reaches Result or HandOff; asked anyway, neither draws a seat.
      {"open pass 2, result, no mover", Roster::pass(2), MatchState::Result, playing(2), NO_SEAT, NO_SEAT},
      {"open pass 2, hand-off", Roster::pass(2), MatchState::HandOff, playing(2), NO_SEAT, NO_SEAT},
      {"seat 2 only, starting", secondSeatOnly(), MatchState::Starting, playing(1), NO_SEAT, 2},
      {"seat 2 only, playing", secondSeatOnly(), MatchState::Playing, playing(1), NO_SEAT, 2},
      {"seat 2 only, paused", secondSeatOnly(), MatchState::Paused, playing(1), NO_SEAT, 2},
      {"seat 2 only, over", secondSeatOnly(), MatchState::Over, over(0b01), NO_SEAT, 2},
      {"seat 2 only, error", secondSeatOnly(), MatchState::Error, playing(1), NO_SEAT, 2},
      {"seat 2 only, leaving", secondSeatOnly(), MatchState::Leaving, playing(1), NO_SEAT, 2},
      {"seat 2 only, result, mover 1", secondSeatOnly(), MatchState::Result, playing(2), 1, 2},
      {"seat 2 only, result, no mover", secondSeatOnly(), MatchState::Result, playing(2), NO_SEAT, 2},
      {"seat 2 only, hand-off", secondSeatOnly(), MatchState::HandOff, playing(2), 2, NO_SEAT},
      {"solo, result", Roster::solo(), MatchState::Result, playing(1), NO_SEAT, 1},
      {"solo, hand-off", Roster::solo(), MatchState::HandOff, playing(1), 1, NO_SEAT},
  };
  for (const MoverRow& row : rows) {
    EXPECT_EQ(seatShown(row.state, row.roster, row.status, row.mover), row.shown) << row.what;
  }
}

// A three-seat roster this device plays seats 1 and 2 of: several local seats, but not every one.
Roster firstTwoOfThree() {
  Roster roster;
  roster.mode = Mode::Nearby;
  roster.seats = 3;
  roster.localSeats = 0b011;
  return roster;
}

// Cross-story review row 8: Playing fails closed as Result does, so a turn seat this device does not play draws
// nothing and reads no input, rather than relying on LuaGame to refuse it.
TEST(SeatShownTest, ATurnSeatThisDeviceDoesNotPlayShowsNoSeat) {
  const Roster roster = firstTwoOfThree();
  EXPECT_EQ(seatShown(MatchState::Playing, roster, playing(1)), 1);
  EXPECT_EQ(seatShown(MatchState::Playing, roster, playing(2)), 2);
  EXPECT_EQ(seatShown(MatchState::Playing, roster, playing(3)), NO_SEAT) << "seat 3 is another device's";
  EXPECT_EQ(seatShown(MatchState::Paused, roster, playing(3)), NO_SEAT) << "paused shows what playing shows";
  EXPECT_EQ(seatShown(MatchState::Playing, roster, playing(0)), NO_SEAT) << "seat 0 is never a turn seat";
  EXPECT_EQ(seatShown(MatchState::Over, roster, over(0b100)), 0) << "over: the frame for everyone";
  EXPECT_EQ(seatShown(MatchState::Playing, Roster::pass(2), playing(3)), NO_SEAT) << "a turn past n";
}

TEST(SeatShownTest, NoSeatIsNoRosterSeat) {
  EXPECT_FALSE(Roster::pass(Roster::MAX_SEATS).isSeat(NO_SEAT));
  EXPECT_FALSE(Roster::pass(Roster::MAX_SEATS).isLocal(NO_SEAT));
  EXPECT_NE(NO_SEAT, 0) << "seat 0 is the frame for everyone";
}

TEST(SeatShownTest, APassRosterHasEverySeatLocal) {
  const Roster pair = Roster::pass(2);
  EXPECT_EQ(pair.mode, Mode::Pass);
  EXPECT_EQ(pair.seats, 2);
  EXPECT_EQ(pair.localSeats, 0b11);
  EXPECT_EQ(pair.localSeatCount(), 2);
  EXPECT_EQ(pair.firstLocalSeat(), 1);
  EXPECT_EQ(pair.api, API_LEVEL);
  const Roster three = Roster::pass(3);
  EXPECT_EQ(three.localSeats, 0b111);
  EXPECT_EQ(three.localSeatCount(), 3);
  EXPECT_TRUE(three.isLocal(3));
  EXPECT_FALSE(three.isLocal(4));
  const Roster full = Roster::pass(Roster::MAX_SEATS);
  EXPECT_EQ(full.localSeats, 0xFFFF);
  EXPECT_EQ(full.localSeatCount(), Roster::MAX_SEATS);
  EXPECT_EQ(Roster::solo().localSeatCount(), 1);
  EXPECT_EQ(secondSeatOnly().localSeatCount(), 1);
}

TEST(SeatShownTest, PassSeatsIsTheFewestAPassMatchCanHaveWhenItFits) {
  EXPECT_EQ(passSeats(1, 2, 2), 2) << "a solo and pass game: pass needs two";
  EXPECT_EQ(passSeats(2, 2, 2), 2);
  EXPECT_EQ(passSeats(2, 4, 2), 2) << "the fewest, not the most";
  EXPECT_EQ(passSeats(3, 4, 4), 3);
  EXPECT_EQ(passSeats(1, 3, 3), 2);
  EXPECT_EQ(passSeats(1, 1, 2), 0) << "the game has one seat";
  EXPECT_EQ(passSeats(3, 4, 2), 0) << "more seats than the host has";
  EXPECT_EQ(passSeats(2, 2, 1), 0);
  EXPECT_EQ(passSeats(2, 40, 40), 2);
  EXPECT_EQ(passSeats(17, 40, 40), 0) << "past the winners mask";
  EXPECT_EQ(passSeats(16, 40, 40), 16);
  EXPECT_EQ(passSeats(2, 300, 2), 2) << "a manifest's seats are not cut to a byte";
}

}  // namespace
