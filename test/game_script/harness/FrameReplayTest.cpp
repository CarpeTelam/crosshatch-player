#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "DisplayList.h"
#include "FrameReplay.h"
#include "GameIconBlit.h"
#include "GameIconDraw.h"
#include "GameIcons.h"
#include "GameImageBlit.h"
#include "GameImages.h"
#include "GameViewport.h"
#include "GfxRenderer.h"
#include "HarnessSupport.h"
#include "fontIds.h"

// FrameReplay::draw and drawGameIconAt on the recording renderer: where an icon or
// an image lands (canvas origin, size, ink, clip), the icon weight, the fills a run
// costs, and the frame's own clip. The expected pixels are worked out here, pixel by
// pixel, from the generated icon data and the image rows: which bitmap a size and weight
// select, the origin, the clip, and the ink are this file's own arithmetic. Only the bit
// lookups (GameIconBlit::inkAt, GameImageBlit::blackAt) are shared with the replay; they
// are pinned by GameIconBlitTest and GameImageBlitTest, and the replay's run walking
// (inkRuns, runs) is what these tests check against them.

namespace {

using GameScript::Color;  // not the renderer's global ::Color
using GameScript::DisplayList;
using GameScript::IconWeight;
using GameScript::MAX_BYTES;
using GameScript::Refresh;
using GameScript::TextSize;
using harness::bmpFile;
using harness::Bytes;
using harness::logHas;
using harness::rowsOf;
using harness::speckle;
using Px = GfxRenderer::Pixel;

// The canvas on the 480 x 800 screen, off the corner as on the boards: 474 x 788 at (3, 6).
constexpr int OX = 3;
constexpr int OY = 6;
constexpr int CW = 474;
constexpr int CH = 788;

// One icon as drawn at one size, from the generated bitmaps: which bitmap, its side,
// and how many drawn pixels a bitmap pixel takes.
struct Drawn {
  const uint8_t* bitmap;
  int pixels;
  int scale;
  int side() const { return pixels * scale; }
};

Drawn drawnAs(const size_t index, const TextSize size, const IconWeight weight) {
  const GameIcons::Icon& icon = GameIcons::ICONS[index];
  const auto w = static_cast<size_t>(weight);
  switch (size) {
    case TextSize::Small:
      return {icon.small[w], GameIcons::SMALL_PIXELS, 1};
    case TextSize::Medium:
      return {icon.medium[w], GameIcons::MEDIUM_PIXELS, 1};
    case TextSize::Large:
      break;
  }
  return {icon.medium[w], GameIcons::MEDIUM_PIXELS, 2};
}

size_t iconIndex(const char* name) {
  const int index = GameIcons::find(name, std::strlen(name));
  EXPECT_GE(index, 0) << name;
  return static_cast<size_t>(index < 0 ? 0 : index);
}

const char* sizeName(const TextSize size) {
  return size == TextSize::Small ? "small" : size == TextSize::Medium ? "medium" : "large";
}

class FrameReplayTest : public harness::HarnessTest {
 protected:
  void SetUp() override {
    HarnessTest::SetUp();
    storage.assign(MAX_BYTES, 0);
    list = DisplayList(storage.data(), storage.size());
  }

  bool draw(const Refresh hint = Refresh::Fast) { return replay.draw(renderer, viewport, list, hint, images); }

  // The pixels the screen model must hold once the frame is drawn: `expected(x, y)` for every
  // screen pixel. Reports the first mismatches only.
  template <typename Fn>
  void expectScreen(Fn&& expected) {
    int wrong = 0;
    for (int y = 0; y < renderer.getScreenHeight() && wrong < 5; ++y) {
      for (int x = 0; x < renderer.getScreenWidth() && wrong < 5; ++x) {
        if (renderer.pixel(x, y) != expected(x, y)) {
          ADD_FAILURE() << "pixel (" << x << ", " << y << ") is " << static_cast<int>(renderer.pixel(x, y))
                        << ", expected " << static_cast<int>(expected(x, y));
          ++wrong;
        }
      }
    }
  }

  static bool onCanvas(const int x, const int y) { return x >= OX && y >= OY && x < OX + CW && y < OY + CH; }

  // One icon over a canvas cleared to `base` (white or black), in `ink`: the canvas pixel it
  // must leave.
  void expectIcon(const char* name, const TextSize size, const IconWeight weight, const Color ink, const Color base,
                  const int x, const int y) {
    SCOPED_TRACE(std::string(name) + " " + sizeName(size) + (weight == IconWeight::Fill ? " fill" : " regular") +
                 (ink == Color::Black ? " black" : " white") + " at " + std::to_string(x) + "," + std::to_string(y));
    renderer.forget();
    list.clear();
    if (base == Color::Black) {
      ASSERT_TRUE(list.appendClear(Color::Black));
    }
    const size_t index = iconIndex(name);
    ASSERT_TRUE(list.appendIcon(x, y, static_cast<uint16_t>(index), size, ink, weight));
    replay = FrameReplay();
    ASSERT_TRUE(draw());

    const Drawn drawn = drawnAs(index, size, weight);
    expectScreen([&](const int px, const int py) {
      if (!onCanvas(px, py)) return Px::PixelWhite;  // the cleared screen, off the canvas
      const int dx = px - OX - x;
      const int dy = py - OY - y;
      if (dx >= 0 && dy >= 0 && dx < drawn.side() && dy < drawn.side() &&
          GameIconBlit::inkAt(drawn.bitmap, drawn.pixels, dx / drawn.scale, dy / drawn.scale)) {
        return ink == Color::Black ? Px::PixelBlack : Px::PixelWhite;
      }
      return base == Color::Black ? Px::PixelBlack : Px::PixelWhite;
    });
    // Whatever the clip rectangle would hide, the replay asked for nothing off the canvas.
    EXPECT_EQ(renderer.fillsOutside(OX, OY, CW, CH), 0u);
  }

  // The images: `count` files' rows one after another, as GameAssets lays them out.
  void useImages(const std::vector<Bytes>& files, const std::vector<std::pair<int, int>>& sizes) {
    imageSpans.assign(files.size(), GameCore::ImageSpan{});
    imagePixels.clear();
    for (size_t i = 0; i < files.size(); ++i) {
      std::snprintf(imageSpans[i].name, sizeof(imageSpans[i].name), "img%zu", i);
      imageSpans[i].width = static_cast<uint32_t>(sizes[i].first);
      imageSpans[i].height = static_cast<uint32_t>(sizes[i].second);
      imageSpans[i].rowBytes = static_cast<uint32_t>((sizes[i].first + 31) / 32 * 4);
      imageSpans[i].offset = static_cast<uint32_t>(imagePixels.size());
      const Bytes rows = rowsOf(files[i]);
      imagePixels.insert(imagePixels.end(), rows.begin(), rows.end());
    }
    images.spans = imageSpans.data();
    images.count = imageSpans.size();
    images.pixels = imagePixels.data();
  }

  // One image over a canvas cleared to `base` (white or black): what it must leave. An image is
  // opaque, so every pixel of it is filled: black pixels in `ink`, white ones in the other,
  // whatever was under it.
  void expectImage(const size_t index, const Color ink, const int x, const int y, const Color base = Color::White) {
    SCOPED_TRACE("image " + std::to_string(index) + (ink == Color::Black ? " black" : " white") + " at " +
                 std::to_string(x) + "," + std::to_string(y) + (base == Color::Black ? " over black" : ""));
    renderer.forget();
    list.clear();
    if (base == Color::Black) {
      ASSERT_TRUE(list.appendClear(Color::Black));
    }
    ASSERT_TRUE(list.appendImage(x, y, static_cast<uint16_t>(index), ink));
    replay = FrameReplay();
    ASSERT_TRUE(draw());
    const GameCore::ImageSpan& span = images.spans[index];
    const uint8_t* const rows = images.pixels + span.offset;
    expectScreen([&](const int px, const int py) {
      if (!onCanvas(px, py)) return Px::PixelWhite;
      const int dx = px - OX - x;
      const int dy = py - OY - y;
      if (dx < 0 || dy < 0 || dx >= static_cast<int>(span.width) || dy >= static_cast<int>(span.height)) {
        return base == Color::Black ? Px::PixelBlack : Px::PixelWhite;
      }
      const bool blackPixel =
          GameImageBlit::blackAt(rows, span.rowBytes, static_cast<uint32_t>(dx), static_cast<uint32_t>(dy));
      return blackPixel == (ink == Color::Black) ? Px::PixelBlack : Px::PixelWhite;
    });
    EXPECT_EQ(renderer.fillsOutside(OX, OY, CW, CH), 0u);
  }

  GfxRenderer renderer;
  GameViewport viewport{OX, OY, CW, CH};
  FrameReplay replay;
  std::vector<uint8_t> storage;
  DisplayList list;
  std::vector<GameCore::ImageSpan> imageSpans;
  Bytes imagePixels;
  GameCore::GameImages images;
};

// ---- ## 3.1: an icon's canvas origin, size, ink, and clip ----

TEST_F(FrameReplayTest, AnIconIsDrawnFromTheCanvasOriginInEachSize) {
  for (const TextSize size : {TextSize::Small, TextSize::Medium, TextSize::Large}) {
    expectIcon("arrow-u-up-left", size, IconWeight::Regular, Color::Black, Color::White, 10, 20);
  }
}

TEST_F(FrameReplayTest, AnIconsInkIsBlackOrWhiteAndNothingElseIsDrawn) {
  expectIcon("arrow-u-up-left", TextSize::Medium, IconWeight::Regular, Color::White, Color::Black, 40, 50);
  expectIcon("arrow-u-up-left", TextSize::Medium, IconWeight::Regular, Color::Black, Color::White, 40, 50);
  expectIcon("dice-six", TextSize::Large, IconWeight::Regular, Color::White, Color::Black, 300, 500);
}

TEST_F(FrameReplayTest, AnIconPartlyOffTheCanvasIsClippedToTheCanvasBeforeItIsDrawn) {
  // Each side in turn, and a corner: the pixels that would land on the bezel are not asked for.
  expectIcon("arrow-u-up-left", TextSize::Large, IconWeight::Regular, Color::Black, Color::White, -40, -30);
  expectIcon("arrow-u-up-left", TextSize::Large, IconWeight::Regular, Color::Black, Color::White, CW - 50, 100);
  expectIcon("arrow-u-up-left", TextSize::Large, IconWeight::Regular, Color::Black, Color::White, 100, CH - 60);
  expectIcon("dice-six", TextSize::Medium, IconWeight::Regular, Color::White, Color::Black, CW - 20, CH - 20);
}

TEST_F(FrameReplayTest, AnIconWhollyOffTheCanvasCostsNothing) {
  const size_t index = iconIndex("dice-six");
  const int16_t off[][2] = {{-200, 0}, {CW, 0}, {0, CH}, {0, -128}, {32767, 32767}, {-32768, -32768}};
  for (const auto& at : off) {
    ASSERT_TRUE(list.appendIcon(at[0], at[1], static_cast<uint16_t>(index), TextSize::Large, Color::Black,
                                IconWeight::Regular));
  }
  ASSERT_TRUE(draw());
  EXPECT_EQ(renderer.count(GfxRenderer::Kind::FillRect), 0u);
  EXPECT_EQ(renderer.pixelCount(Px::PixelBlack), 0u);
}

TEST_F(FrameReplayTest, AnIconTheLibraryLacksDrawsNothingAndIsLogged) {
  ASSERT_TRUE(list.appendIcon(0, 0, static_cast<uint16_t>(GameIcons::ICON_COUNT), TextSize::Small, Color::Black,
                              IconWeight::Regular));
  ASSERT_TRUE(draw());
  EXPECT_EQ(renderer.count(GfxRenderer::Kind::FillRect), 0u);
  EXPECT_TRUE(logHas("No game icon " + std::to_string(GameIcons::ICON_COUNT) + " at 32 px"));
}

TEST_F(FrameReplayTest, DrawGameIconAtTakesOnlyTheSizesTheLibraryHas) {
  const size_t index = iconIndex("dice-six");
  EXPECT_TRUE(drawGameIconAt(renderer, index, 32, 0, 0, 480, 800, 0, 0, true, false));
  EXPECT_TRUE(drawGameIconAt(renderer, index, 64, 0, 0, 480, 800, 0, 0, true, false));
  EXPECT_TRUE(drawGameIconAt(renderer, index, 128, 0, 0, 480, 800, 0, 0, true, false));
  const size_t calls = renderer.calls.size();
  EXPECT_FALSE(drawGameIconAt(renderer, index, 48, 0, 0, 480, 800, 0, 0, true, false));
  EXPECT_TRUE(logHas("at 48 px (icons come in 32, 64, and 128 px)"));
  EXPECT_FALSE(drawGameIconAt(renderer, GameIcons::ICON_COUNT, 32, 0, 0, 480, 800, 0, 0, true, false));
  EXPECT_EQ(renderer.calls.size(), calls);
}

TEST_F(FrameReplayTest, ScreensDrawAnIconByNameOnTheLogicalScreen) {
  ASSERT_TRUE(drawGameIcon(renderer, "arrow-u-up-left", 100, 200, 32, true));
  const Drawn drawn = drawnAs(iconIndex("arrow-u-up-left"), TextSize::Small, IconWeight::Regular);
  size_t ink = 0;
  for (int dy = 0; dy < 32; ++dy) {
    for (int dx = 0; dx < 32; ++dx) {
      const bool expected = GameIconBlit::inkAt(drawn.bitmap, drawn.pixels, dx, dy);
      EXPECT_EQ(renderer.pixel(100 + dx, 200 + dy), expected ? Px::PixelBlack : Px::Untouched) << dx << "," << dy;
      ink += expected;
    }
  }
  EXPECT_GT(ink, 0u);
  EXPECT_FALSE(drawGameIcon(renderer, "no-such-icon", 0, 0, 32, true));
  EXPECT_TRUE(logHas("No game icon named \"no-such-icon\""));
}

// ---- ## 3.9: the fill weight, through the replay ----

TEST_F(FrameReplayTest, TheFillWeightDrawsTheFillBitmapAndTheRegularWeightTheRegularOne) {
  for (const TextSize size : {TextSize::Small, TextSize::Large}) {
    expectIcon("heart", size, IconWeight::Regular, Color::Black, Color::White, 30, 40);
    const size_t regularInk = renderer.pixelCount(Px::PixelBlack);
    expectIcon("heart", size, IconWeight::Fill, Color::Black, Color::White, 30, 40);
    const size_t fillInk = renderer.pixelCount(Px::PixelBlack);
    // The two weights differ on this icon, so a test that passes for one passes for neither.
    EXPECT_GT(fillInk, regularInk) << sizeName(size);
  }
}

// ---- ## 3.2: an image's offset, through the replay ----

TEST_F(FrameReplayTest, AnImageIsDrawnFromItsOwnRowsAtTheCanvasOrigin) {
  const Bytes first = bmpFile(40, 10, [](int x, int y) { return speckle(x, y, 0); });
  const Bytes second = bmpFile(33, 5, [](int x, int y) { return speckle(x, y, 2); });
  useImages({first, second}, {{40, 10}, {33, 5}});
  ASSERT_NE(rowsOf(first), Bytes(rowsOf(second)));  // the two are told apart by their rows
  expectImage(0, Color::Black, 10, 20);
  expectImage(1, Color::Black, 10, 20);  // the second's rows sit after the first's
  expectImage(1, Color::White, 200, 300);
  // Opaque: the image's white pixels are drawn over black, and its black ones over black too.
  expectImage(0, Color::Black, 10, 20, Color::Black);
  expectImage(0, Color::White, 10, 20, Color::Black);
  expectImage(1, Color::White, 200, 300, Color::Black);
}

TEST_F(FrameReplayTest, AnImagePartlyOffTheCanvasIsClippedBeforeItIsDrawn) {
  const Bytes wide = bmpFile(100, 60, [](int x, int y) { return speckle(x, y, 1); });
  useImages({wide}, {{100, 60}});
  expectImage(0, Color::Black, -30, -20);
  expectImage(0, Color::Black, CW - 40, CH - 25);
  expectImage(0, Color::White, CW - 40, 5);
}

TEST_F(FrameReplayTest, AnImageIndexPastTheTableDrawsNothingAndIsLogged) {
  const Bytes one = bmpFile(8, 8, [](int x, int y) { return speckle(x, y); });
  useImages({one}, {{8, 8}});
  ASSERT_TRUE(list.appendImage(0, 0, 5, Color::Black));
  ASSERT_TRUE(draw());
  EXPECT_EQ(renderer.count(GfxRenderer::Kind::FillRect), 0u);
  EXPECT_TRUE(logHas("No game image 5 (the game has 1); not drawn"));

  // The first index past the table is refused too, and a game with no images refuses every one.
  fakelog::lines.clear();
  list.clear();
  ASSERT_TRUE(list.appendImage(0, 0, 1, Color::Black));
  replay = FrameReplay();
  ASSERT_TRUE(draw());
  EXPECT_EQ(renderer.count(GfxRenderer::Kind::FillRect), 0u);
  EXPECT_TRUE(logHas("No game image 1 (the game has 1); not drawn"));
  fakelog::lines.clear();
  list.clear();
  ASSERT_TRUE(list.appendImage(0, 0, 0, Color::Black));
  images = GameCore::GameImages{};
  replay = FrameReplay();
  ASSERT_TRUE(draw());
  EXPECT_EQ(renderer.count(GfxRenderer::Kind::FillRect), 0u);
  EXPECT_TRUE(logHas("No game image 0 (the game has 0); not drawn"));
}

// ---- ## e3r-1: what a run costs on the real FrameReplay::draw ----

TEST_F(FrameReplayTest, EachRunOfOneColourIsOneFillNotOnePerPixel) {
  // 100 x 60 all black: a run a row.
  const Bytes solid = bmpFile(100, 60, [](int, int) { return false; });
  useImages({solid}, {{100, 60}});
  ASSERT_TRUE(list.appendImage(20, 30, 0, Color::Black));
  ASSERT_TRUE(draw());
  ASSERT_EQ(renderer.count(GfxRenderer::Kind::FillRect), 60u);
  for (const auto& call : renderer.calls) {
    if (call.kind != GfxRenderer::Kind::FillRect) continue;
    EXPECT_EQ(call.w, 100);
    EXPECT_EQ(call.h, 1);
    EXPECT_EQ(call.x, OX + 20);
  }
}

TEST_F(FrameReplayTest, AnIconsInkRowsFillOneRunAtATime) {
  // Each run of ink on a row is one fill; per pixel would make a fill for every ink pixel.
  for (const char* name : {"square", "circle", "dice-six"}) {
    SCOPED_TRACE(name);
    const size_t index = iconIndex(name);
    renderer.forget();
    list.clear();
    ASSERT_TRUE(list.appendIcon(0, 0, static_cast<uint16_t>(index), TextSize::Large, Color::Black, IconWeight::Fill));
    replay = FrameReplay();
    ASSERT_TRUE(draw());
    const Drawn drawn = drawnAs(index, TextSize::Large, IconWeight::Fill);
    size_t runs = 0;
    size_t inkPixels = 0;
    for (int dy = 0; dy < drawn.side(); ++dy) {
      bool inRun = false;
      for (int dx = 0; dx < drawn.side(); ++dx) {
        const bool ink = GameIconBlit::inkAt(drawn.bitmap, drawn.pixels, dx / drawn.scale, dy / drawn.scale);
        if (ink && !inRun) ++runs;
        inRun = ink;
        inkPixels += ink;
      }
    }
    ASSERT_GT(inkPixels, runs);  // some run is longer than a pixel, so per pixel would cost more
    EXPECT_EQ(renderer.count(GfxRenderer::Kind::FillRect), runs);
  }
}

// ---- The frame's own clip, refresh, and the other ops on the origin ----

TEST_F(FrameReplayTest, TheFrameDrawsInsideTheCanvasClipAndRestoresTheCallersClip) {
  renderer.setClipRect(10, 10, 100, 100);
  renderer.calls.clear();
  ASSERT_TRUE(list.appendRect(0, 0, 50, 50, Color::Black, true));
  ASSERT_TRUE(list.appendIcon(5, 5, static_cast<uint16_t>(iconIndex("dice-six")), TextSize::Small, Color::Black,
                              IconWeight::Regular));
  ASSERT_TRUE(draw());
  ASSERT_GE(renderer.calls.size(), 4u);
  EXPECT_EQ(renderer.calls.front().kind, GfxRenderer::Kind::ClearScreen);
  EXPECT_EQ(renderer.calls[1].kind, GfxRenderer::Kind::SetClipRect);
  const std::array<int, 4> canvas{OX, OY, CW, CH};
  for (size_t i = 2; i + 1 < renderer.calls.size(); ++i) EXPECT_EQ(renderer.calls[i].clip, canvas) << i;
  EXPECT_EQ(renderer.calls.back().kind, GfxRenderer::Kind::SetClipRect);
  EXPECT_EQ(renderer.getClipRect(), (std::array<int, 4>{10, 10, 100, 100}));
}

TEST_F(FrameReplayTest, AFrameIdenticalToTheOneOnScreenIsNotDrawnAgain) {
  ASSERT_TRUE(list.appendRect(1, 1, 4, 4, Color::Black, true));
  ASSERT_TRUE(draw());
  EXPECT_EQ(replay.refreshMode(), HalDisplay::FULL_REFRESH);  // the first frame shows in full
  const size_t calls = renderer.calls.size();
  EXPECT_FALSE(draw());
  EXPECT_EQ(renderer.calls.size(), calls);
  replay.forceFull();
  EXPECT_TRUE(draw());
  EXPECT_GT(renderer.calls.size(), calls);
  ASSERT_TRUE(list.appendRect(9, 9, 4, 4, Color::Dark, true));
  EXPECT_TRUE(draw());
  EXPECT_EQ(replay.refreshMode(), HalDisplay::FAST_REFRESH);
}

TEST_F(FrameReplayTest, TheCanvasIsTheScreenLessTheBezelInsets) {
  renderer.setInsets(1, 2, 3, 4);  // top, right, bottom, left
  const GameViewport v = GameViewport::forRenderer(renderer);
  EXPECT_EQ(v.originX(), 4);
  EXPECT_EQ(v.originY(), 1);
  EXPECT_EQ(v.width(), 480 - 4 - 2);
  EXPECT_EQ(v.height(), 800 - 1 - 3);
  const GameViewport boards = GameViewport::forRenderer(GfxRenderer());  // the double's default: the boards' 474 x 788
  EXPECT_EQ(boards.width(), CW);
  EXPECT_EQ(boards.height(), CH);
  EXPECT_EQ(boards.originX(), OX);
  EXPECT_EQ(boards.originY(), OY);
}

TEST_F(FrameReplayTest, RectsLinesAndTextAreOffsetByTheCanvasOrigin) {
  renderer.addFont(UI_10_FONT_ID, 20);
  renderer.addFont(UI_12_FONT_ID, 24);
  renderer.addFont(NOTOSANS_18_FONT_ID, 30);
  replay.loadFonts(renderer);
  ASSERT_TRUE(list.appendRect(5, 6, 10, 4, Color::Dark, true));
  ASSERT_TRUE(list.appendLine(1, 2, 30, 40, Color::Black));
  ASSERT_TRUE(list.appendText(5, 7, "AB", 2, TextSize::Small, Color::Black));
  ASSERT_TRUE(draw());
  bool sawRect = false;
  bool sawLine = false;
  std::vector<std::pair<int, std::string>> glyphs;
  for (const auto& call : renderer.calls) {
    if (call.kind == GfxRenderer::Kind::FillRectDither && call.color == ::Color::DarkGray) {
      EXPECT_EQ(call.x, OX + 5);
      EXPECT_EQ(call.y, OY + 6);
      EXPECT_EQ(call.w, 10);
      EXPECT_EQ(call.h, 4);
      sawRect = true;
    } else if (call.kind == GfxRenderer::Kind::DrawLine) {
      EXPECT_EQ(call.x, OX + 1);
      EXPECT_EQ(call.y, OY + 2);
      EXPECT_EQ(call.x2, OX + 30);
      EXPECT_EQ(call.y2, OY + 40);
      EXPECT_TRUE(call.black);
      sawLine = true;
    } else if (call.kind == GfxRenderer::Kind::DrawText) {
      EXPECT_EQ(call.fontId, UI_10_FONT_ID);
      EXPECT_EQ(call.y, OY + 7);
      glyphs.emplace_back(call.x, call.text);
    }
  }
  EXPECT_TRUE(sawRect);
  EXPECT_TRUE(sawLine);
  // One code point a call, each at the previous one's advance (10 px in the double's font).
  EXPECT_EQ(glyphs, (std::vector<std::pair<int, std::string>>{{OX + 5, "A"}, {OX + 15, "B"}}));
}

}  // namespace
