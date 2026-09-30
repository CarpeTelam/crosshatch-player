#include <GameIcons.h>
#include <MatchLifecycle.h>
#include <components/overlays/option-dialog.h>
#include <gtest/gtest.h>

#include <cstring>

#include "GameViewIcons.h"

using GameCore::MatchEvent;
using GameCore::MatchLifecycle;
using GameCore::MatchMenu;
using GameCore::MatchState;

namespace {

constexpr MatchState ALL_STATES[] = {MatchState::Starting, MatchState::Playing, MatchState::Paused,
                                     MatchState::Over,     MatchState::Error,   MatchState::Leaving};
constexpr MatchEvent ALL_EVENTS[] = {MatchEvent::Started,   MatchEvent::Back,        MatchEvent::Home,
                                     MatchEvent::Resume,    MatchEvent::Leave,       MatchEvent::RoundOver,
                                     MatchEvent::PlayAgain, MatchEvent::ScriptError, MatchEvent::ForcedExit};

bool inLibrary(const char* name) { return name && GameIcons::find(name, std::strlen(name)) >= 0; }

bool isMenuChoice(const MatchEvent event) {
  for (const MatchState state : ALL_STATES) {
    const MatchMenu menu = MatchLifecycle::menuFor(state);
    for (uint8_t i = 0; i < menu.count; ++i) {
      if (menu.events[i] == event) return true;
    }
  }
  return false;
}

// A portrait dialog row about as wide as the X4 Pro's and Sticky's, optionDialog's default 44 px tall with its 8 px
// gap.
constexpr int ROW_W = 352;
constexpr int ROW_H = 44;
constexpr int GAP = 8;

namespace fui = freeink::ui;

// A draw target that measures every glyph 8 px wide and 20 px tall and records
// nothing: the dialog's layout shows in the Frame's hit rects.
class LayoutTarget final : public fui::DrawTarget {
 public:
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(8 * std::strlen(text)), 20};
  }
  int16_t lineHeight(fui::FontId) const override { return 20; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};

}  // namespace

TEST(GameViewIconsTest, SizesAreTheLibrarys) {
  EXPECT_EQ(GameViewIcons::VIEW_PIXELS, GameIcons::MEDIUM_PIXELS);
  EXPECT_EQ(GameViewIcons::ROW_PIXELS, GameIcons::SMALL_PIXELS);
}

TEST(GameViewIconsTest, EveryViewAndMenuRowNamesALibraryIcon) {
  int views = 0;
  for (const MatchState state : ALL_STATES) {
    const MatchMenu menu = MatchLifecycle::menuFor(state);
    if (menu.count == 0) continue;
    ++views;
    EXPECT_TRUE(inLibrary(GameViewIcons::forView(state))) << MatchLifecycle::name(state);
    for (uint8_t i = 0; i < menu.count; ++i) {
      EXPECT_TRUE(inLibrary(GameViewIcons::forOption(menu.events[i])))
          << MatchLifecycle::name(state) << " row " << static_cast<int>(i) << " "
          << MatchLifecycle::name(menu.events[i]);
    }
  }
  EXPECT_EQ(views, 3);
}

TEST(GameViewIconsTest, TheNamesAreTheAgreedOnes) {
  EXPECT_STREQ(GameViewIcons::forView(MatchState::Paused), "pause");
  EXPECT_STREQ(GameViewIcons::forView(MatchState::Over), "flag-checkered");
  EXPECT_STREQ(GameViewIcons::forView(MatchState::Error), "warning");
  EXPECT_STREQ(GameViewIcons::forOption(MatchEvent::Resume), "play");
  EXPECT_STREQ(GameViewIcons::forOption(MatchEvent::Leave), "sign-out");
  EXPECT_STREQ(GameViewIcons::forOption(MatchEvent::PlayAgain), "arrows-clockwise");
  EXPECT_STREQ(GameViewIcons::forOption(MatchEvent::Back), "sign-out");
}

TEST(GameViewIconsTest, StatesWithNoViewAndNonMenuEventsHaveNoIcon) {
  EXPECT_EQ(GameViewIcons::forView(MatchState::Starting), nullptr);
  EXPECT_EQ(GameViewIcons::forView(MatchState::Playing), nullptr);
  EXPECT_EQ(GameViewIcons::forView(MatchState::Leaving), nullptr);
  int nonMenu = 0;
  for (const MatchEvent event : ALL_EVENTS) {
    if (isMenuChoice(event)) continue;
    ++nonMenu;
    EXPECT_EQ(GameViewIcons::forOption(event), nullptr) << MatchLifecycle::name(event);
  }
  EXPECT_EQ(nonMenu, 5);
}

TEST(GameViewIconsTest, RowIconInsetIsTheRowsVerticalMargin) {
  EXPECT_EQ(GameViewIcons::rowIconInset(ROW_H), 6);
  EXPECT_EQ(GameViewIcons::rowIconInset(32), 0);
  EXPECT_EQ(GameViewIcons::rowIconInset(20), 0);
}

TEST(GameViewIconsTest, RowIconFitsBesideAShortLabel) {
  EXPECT_TRUE(GameViewIcons::rowIconFits(ROW_W, ROW_H, 60, GAP));
}

TEST(GameViewIconsTest, RowIconNeedsTheInsetIconAndGapLeftOfTheLabel) {
  // Room needed: 6 inset + 32 icon + 8 gap = 46 px left of the centred label.
  const int boundary = ROW_W - 2 * 46;  // label left exactly 46
  EXPECT_TRUE(GameViewIcons::rowIconFits(ROW_W, ROW_H, boundary, GAP));
  // One more pixel of label moves its centred left edge to 45 px.
  EXPECT_FALSE(GameViewIcons::rowIconFits(ROW_W, ROW_H, boundary + 1, GAP));
  EXPECT_FALSE(GameViewIcons::rowIconFits(ROW_W, ROW_H, ROW_W, GAP));
  EXPECT_FALSE(GameViewIcons::rowIconFits(ROW_W, ROW_H, ROW_W + 10, GAP));
}

TEST(GameViewIconsTest, NoRowIconInARowShorterThanTheIcon) {
  EXPECT_FALSE(GameViewIcons::rowIconFits(ROW_W, GameViewIcons::ROW_PIXELS - 1, 10, GAP));
  EXPECT_TRUE(GameViewIcons::rowIconFits(ROW_W, GameViewIcons::ROW_PIXELS, 10, GAP));
}

// drawViewIcons places row icons at rowTop; this pins that to where optionDialog
// really puts its vertical rows, so an SDK layout change fails here.
TEST(GameViewIconsTest, RowTopMatchesOptionDialogsRows) {
  LayoutTarget target;
  fui::DeviceContext device;
  device.width = 480;
  device.height = 800;
  device.hasTouch = true;
  device.minTouchSize = 44;  // no taller than a row, so the hit rects are the rows
  fui::InputSnapshot input;
  fui::InteractionBuffer<8> interactions;
  fui::Frame<8> frame(target, device, input, interactions);

  fui::DialogOption options[2];
  options[0].label = "Resume";
  options[0].action = 1;
  options[0].value = 0;
  options[1].label = "Leave";
  options[1].action = 1;
  options[1].value = 1;
  fui::OptionDialogProps props;
  props.title = "Tracer";
  props.headline = "Paused";
  props.options = options;
  props.optionCount = 2;
  props.contentHeight = GameViewIcons::VIEW_PIXELS;
  props.verticalOptions = true;
  ASSERT_EQ(props.buttonHeight, ROW_H);
  ASSERT_EQ(props.gap, GAP);

  const fui::Rect band = fui::optionDialog(frame, fui::Rect{48, 200, 384, 400}, props);
  ASSERT_FALSE(band.empty());
  EXPECT_EQ(band.height, GameViewIcons::VIEW_PIXELS);
  ASSERT_EQ(interactions.count(), 2u);
  for (int i = 0; i < 2; ++i) {
    const fui::Rect row = interactions.data()[i].rect;
    EXPECT_EQ(row.y, GameViewIcons::rowTop(band.bottom(), i, ROW_H, GAP)) << "row " << i;
    EXPECT_EQ(row.x, band.x) << "row " << i;
    EXPECT_EQ(row.width, band.width) << "row " << i;
    EXPECT_EQ(row.height, ROW_H) << "row " << i;
  }
}

// Retro R8 e and R9 g: an icon takes the ink of the label beside it. A solid paint carries its colour into the text;
// under any other paint (a dither, a bitmap, none) the label keeps its own style's colour, so the icon follows that.
TEST(GameViewIconsTest, AnIconTakesTheInkItsLabelDrawsIn) {
  // (solid, paintWhite, textWhite) -> black
  EXPECT_TRUE(GameViewIcons::labelIsBlack(true, false, false));  // a solid black paint
  EXPECT_FALSE(GameViewIcons::labelIsBlack(true, true, false));  // a solid white paint (an inverted, selected row)
  EXPECT_TRUE(GameViewIcons::labelIsBlack(true, false, true));   // the text style's colour is beside the point
  EXPECT_FALSE(GameViewIcons::labelIsBlack(true, true, true));
  // A dithered focused row: the paint's colour is what it dithers, not what the label is drawn in.
  EXPECT_TRUE(GameViewIcons::labelIsBlack(false, true, false));   // a white-dither paint, a black label
  EXPECT_FALSE(GameViewIcons::labelIsBlack(false, false, true));  // a black-dither paint, a white label
  EXPECT_TRUE(GameViewIcons::labelIsBlack(false, false, false));
  EXPECT_FALSE(GameViewIcons::labelIsBlack(false, true, true));
}

// An inverted text style draws in the opposite ink, so a non-solid paint's label that is inverted white text is black.
TEST(GameViewIconsTest, AnInvertedTextStyleFlipsTheInkTheIconFollows) {
  EXPECT_FALSE(GameViewIcons::textInkIsWhite(false, false));
  EXPECT_TRUE(GameViewIcons::textInkIsWhite(true, false));
  EXPECT_TRUE(GameViewIcons::textInkIsWhite(false, true));  // black text, inverted: white
  EXPECT_FALSE(GameViewIcons::textInkIsWhite(true, true));  // white text, inverted: black
  // Through the rule: under a dither, the icon beside inverted black text is white.
  EXPECT_FALSE(GameViewIcons::labelIsBlack(false, false, GameViewIcons::textInkIsWhite(false, true)));
  EXPECT_TRUE(GameViewIcons::labelIsBlack(false, true, GameViewIcons::textInkIsWhite(true, true)));
  // A solid paint colours the text whatever the style says: `inverted` is cleared for it, so it is not read.
  EXPECT_FALSE(GameViewIcons::labelIsBlack(true, true, GameViewIcons::textInkIsWhite(false, false)));
}
