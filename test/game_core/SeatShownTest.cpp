#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "IGameRules.h"
#include "MatchLifecycle.h"
#include "Roster.h"
#include "SeatShown.h"

// GameCore::seatShown (which seat's frame a device shows), Roster::pass, and passSeats
// (epic-pass-and-play entry 1). Pass runs n = 2 and n = 3: nothing assumes two seats.

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
