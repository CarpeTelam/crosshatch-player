#include <GameIcons.generated.h>
#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "BoardConfig.h"
#include "CrossPointSettings.h"
#include "Epub.h"
#include "MatchSupport.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/home/HomeActivity.h"
#include "icons/blocks.h"
#include "icons/book.h"
#include "icons/folder.h"
#include "icons/library.h"
#include "icons/settings2.h"
#include "icons/transfer.h"
#include "util/ButtonNavigator.h"

// The real HomeActivity and CoverGridHomeUi, unchanged, over the screen doubles and the Home doubles
// (home_stubs/). Entry 5 of epic-install-and-launcher, for deferred-work `## 3.6`: no host test pinned
// the cover grid's tab order (CoverGridHomeUi::drawTabs' icons and its OPDS skip) against HomeActivity's
// index mapping (indexToMenuItem, menuItemToIndex), so a reorder on one side alone would put the
// controller on a tab that opens another screen. The tests draw the tabs and tap each one: the icon
// under the finger and the screen the manager is asked for are two different files' business.

// ---- access to what HomeActivity keeps private ----
// The classic idiom: an explicit instantiation may name a private member, and stores its address where the
// test can reach it. HomeActivity.h is the real header, unchanged.
namespace reach {

template <typename Tag>
struct Slot {
  static typename Tag::type value;
};
template <typename Tag>
typename Tag::type Slot<Tag>::value;

template <typename Tag, typename Tag::type Member>
struct Open {
  Open() { Slot<Tag>::value = Member; }
  static Open instance;
};
template <typename Tag, typename Tag::type Member>
Open<Tag, Member> Open<Tag, Member>::instance;

struct CoverGridUiTag {
  using type = std::unique_ptr<CoverGridHomeUi> HomeActivity::*;
};
struct SelectorTag {
  using type = int HomeActivity::*;
};
struct IndexToItemTag {
  using type = HomeMenuItem (*)(int, bool);
};
struct ItemToIndexTag {
  using type = int (*)(HomeMenuItem, bool);
};
template struct Open<CoverGridUiTag, &HomeActivity::coverGridUi>;
template struct Open<SelectorTag, &HomeActivity::selectorIndex>;
template struct Open<IndexToItemTag, &HomeActivity::indexToMenuItem>;
template struct Open<ItemToIndexTag, &HomeActivity::menuItemToIndex>;

}  // namespace reach

namespace {

using Button = MappedInputManager::Button;

// A tab icon by the name of the screen it stands for, and its bytes (32 x 32, 1 bit a pixel).
struct KnownIcon {
  const char* name;
  const uint8_t* bits;
};
constexpr size_t ICON_BYTES = 32 * 32 / 8;
const KnownIcon KNOWN_ICONS[] = {
    {"files", FolderIcon},
    {"library", LibraryIcon},
    {"opds", BlocksIcon},
    {"transfer", TransferIcon},
    {"games", GameIcons::GAME_CONTROLLER_32},
    {"settings", Settings2Icon},
    // Not a tab: the empty home's book (drawEmpty), drawn at the same size.
    {"book", BookIcon},
};

// Home as the manager runs it: onEnter, then render (with the UI host's render the theme would ask for) and loop
// passes.
// The tabs, in Home's order, by the screen each opens. The boards without games (this file is built for them
// too, without FREEINK_CAP_GAMES) have no Games tab.
std::vector<std::string> tabNames(const bool opds) {
  std::vector<std::string> names{"files", "library"};
  if (opds) names.push_back("opds");
  names.push_back("transfer");
#if FREEINK_CAP_GAMES
  names.push_back("games");
#endif
  names.push_back("settings");
  return names;
}

class HomeTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    ButtonNavigator::setMappedInputManager(*input);
    RECENT_BOOKS.books.clear();
    OPDS_STORE.servers = false;
    BoardConfig::touchPanel() = true;
    UITheme::getInstance().coverGridHome = true;
    Epub::generates() = false;
  }

  void TearDown() override {
    fakertos::release();
    if (home) {
      activityManager.exitHolding(*home);
      activityManager.destroyHolding(home);
    }
    RECENT_BOOKS.books.clear();
    OPDS_STORE.servers = false;
    ScreenTest::TearDown();
  }

  // ---- the books and servers Home reads ----

  static void addBook(const std::string& path, const std::string& title) {
    fakesd::addFile(path, std::string("book"));
    RECENT_BOOKS.books.push_back(RecentBook{path, title, "Author", ""});
  }

  // ---- the screen ----

  void open(const HomeMenuItem initial = HomeMenuItem::NONE) {
    home = std::make_unique<HomeActivity>(*renderer, *input, initial);
    home->onEnter();
    render();
  }
  void reopen(const HomeMenuItem initial = HomeMenuItem::NONE) {
    activityManager.exitHolding(*home);
    activityManager.destroyHolding(home);
    activityManager.reset();
    open(initial);
  }
  // Draws Home. The cover grid's tabs are drawn by the UI host, which the firmware's theme asks to render
  // (UITheme::drawCoverGridHome); the theme double only records that ask, so the test makes it.
  void render() {
    renderer->forgetAll();
    UITheme::getInstance().getTheme().calls.clear();
    if (screen::RecordingTarget::newest()) screen::RecordingTarget::newest()->forget();
    home->render(RenderLock(*home));
    activityManager.markRendered();
    if (CoverGridHomeUi* grid = coverGrid()) grid->renderUi();
  }
  CoverGridHomeUi* coverGrid() { return ((*home).*reach::Slot<reach::CoverGridUiTag>::value).get(); }
  int selector() { return (*home).*reach::Slot<reach::SelectorTag>::value; }
  void frame() {
    home->loop();
    input->clear();
  }

  // ---- the tabs ----

  // The tab icons on screen, left to right, by the screen each stands for ("?" for a bitmap that is none of them).
  std::vector<std::string> tabsDrawn() {
    std::vector<GfxRenderer::IconDrawn> tabs;
    for (const GfxRenderer::IconDrawn& icon : renderer->icons)
      if (icon.size == 32) tabs.push_back(icon);
    std::stable_sort(tabs.begin(), tabs.end(),
                     [](const GfxRenderer::IconDrawn& a, const GfxRenderer::IconDrawn& b) { return a.x < b.x; });
    std::vector<std::string> names;
    for (const GfxRenderer::IconDrawn& tab : tabs) {
      std::string name = "?";
      for (const KnownIcon& known : KNOWN_ICONS)
        if (tab.bitmap.size() == ICON_BYTES && std::memcmp(tab.bitmap.data(), known.bits, ICON_BYTES) == 0)
          name = known.name;
      if (name != "book") names.push_back(name);
    }
    return names;
  }

  // Taps the middle of the tab drawn with `name`'s icon, as a finger would.
  void tapTab(const std::string& name) {
    for (const GfxRenderer::IconDrawn& icon : renderer->icons) {
      if (icon.size != 32 || icon.bitmap.size() != ICON_BYTES) continue;
      for (const KnownIcon& known : KNOWN_ICONS) {
        if (name != known.name || std::memcmp(icon.bitmap.data(), known.bits, ICON_BYTES) != 0) continue;
        input->tap(icon.x + icon.size / 2, icon.y + icon.size / 2);
        frame();
        return;
      }
    }
    FAIL() << "no \"" << name << "\" tab is drawn";
  }

  // What the manager was asked for since the last reset, as one word ("" when nothing).
  std::string asked() const {
    const auto& a = activityManager.asks;
    std::string what;
    auto note = [&what](const int count, const char* name) {
      if (count) what += std::string(what.empty() ? "" : "+") + name;
    };
    note(a.goToFileBrowser, "files");
    note(a.goToLibrary, "library");
    note(a.goToBrowser, "opds");
    note(a.goToFileTransfer, "transfer");
    note(a.goToGames, "games");
    note(a.goToSettings, "settings");
    note(a.goToReader, "reader");
    return what;
  }

  std::unique_ptr<HomeActivity> home;
};

// ---- the cover grid: the tabs and what each opens (## 3.6) ----

TEST_F(HomeTest, TheTabsAreInHomesOrderAndEachOpensItsOwnScreen) {
  const std::vector<std::string> expected = tabNames(false);
  for (const bool touchBoard : {true, false}) {  // a button board lays the grid out differently, the tabs alike
    SCOPED_TRACE(touchBoard ? "touch board" : "button board");
    BoardConfig::touchPanel() = touchBoard;
    if (home) {
      reopen();
    } else {
      open();
    }
    ASSERT_EQ(tabsDrawn(), expected);
    for (const std::string& name : expected) {
      SCOPED_TRACE(name);
      activityManager.reset();
      tapTab(name);
      EXPECT_EQ(asked(), name);
    }
  }
}

TEST_F(HomeTest, WithAnOpdsServerThereIsOneMoreTabBeforeTransferAndItOpensTheBrowser) {
  OPDS_STORE.servers = true;
  open();
  const std::vector<std::string> expected = tabNames(true);
  ASSERT_EQ(tabsDrawn(), expected);
  for (const std::string& name : expected) {
    SCOPED_TRACE(name);
    activityManager.reset();
    tapTab(name);
    EXPECT_EQ(asked(), name);
  }
}

TEST_F(HomeTest, RecentBooksComeBeforeTheTabsAndDoNotMoveThemOrWhatTheyOpen) {
  addBook("/books/one.epub", "One");
  addBook("/books/two.epub", "Two");
  for (const bool opds : {false, true}) {
    SCOPED_TRACE(opds ? "with an OPDS server" : "without");
    OPDS_STORE.servers = opds;
    if (home) {
      reopen();
    } else {
      open();
    }
    const std::vector<std::string> expected = tabNames(opds);
    ASSERT_EQ(tabsDrawn(), expected);
    for (const std::string& name : expected) {
      SCOPED_TRACE(name);
      activityManager.reset();
      tapTab(name);
      EXPECT_EQ(asked(), name);
    }
    // Confirm on the first cover opens that book, and so does Back (Resume).
    reopen();
    input->click(Button::Confirm);
    frame();
    EXPECT_EQ(asked(), "reader");
    EXPECT_EQ(activityManager.lastPath, "/books/one.epub");
    reopen();
    input->click(Button::Back);
    frame();
    EXPECT_EQ(asked(), "reader");
    EXPECT_EQ(activityManager.lastPath, "/books/one.epub");
  }
}

TEST_F(HomeTest, TheSideButtonsWalkTheTabsInTheOrderTheyAreDrawn) {
  // With no books the walk starts on the first tab (Files); Right steps over the drawn order.
  open();
  const std::vector<std::string> expected = tabNames(false);
  ASSERT_EQ(tabsDrawn(), expected);
  for (size_t step = 0; step < expected.size(); ++step) {
    SCOPED_TRACE(expected[step]);
    reopen();
    for (size_t i = 0; i < step; ++i) {
      input->press(Button::Right);
      frame();
    }
    input->click(Button::Confirm);
    frame();
    EXPECT_EQ(asked(), expected[step]);
  }
  // Left from the first wraps to the last: Settings.
  reopen();
  input->press(Button::Left);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(asked(), "settings");
}

TEST_F(HomeTest, WithBooksAndAnOpdsServerTheSideButtonsStillWalkTheTabsInOrder) {
  // The walk is flat: books first, then the tabs. A Right press from the book band jumps into the tab band at its
  // first tab, and each further press steps one tab on.
  addBook("/books/one.epub", "One");
  addBook("/books/two.epub", "Two");
  OPDS_STORE.servers = true;
  open();
  const std::vector<std::string> expected = tabNames(true);
  ASSERT_EQ(tabsDrawn(), expected);
  for (size_t step = 0; step < expected.size(); ++step) {
    SCOPED_TRACE(expected[step]);
    reopen();
    for (size_t i = 0; i <= step; ++i) {
      input->press(Button::Right);
      frame();
    }
    input->click(Button::Confirm);
    frame();
    EXPECT_EQ(asked(), expected[step]);
  }
}

// The reselected row: Home opened by goHome(initial item) puts the selection on that item's tab, whichever
// tabs there are (the Games list's Back goes Home to its Games row).
TEST_F(HomeTest, HomeOpenedOnAnItemSelectsThatItemsTab) {
  struct Case {
    HomeMenuItem item;
    const char* opens;
    bool opds;
  };
  const Case cases[] = {
      {HomeMenuItem::FILE_BROWSER, "files", false},
      {HomeMenuItem::LIBRARY, "library", false},
      {HomeMenuItem::FILE_TRANSFER, "transfer", false},
#if FREEINK_CAP_GAMES
      {HomeMenuItem::GAMES, "games", false},
#endif
      {HomeMenuItem::SETTINGS_MENU, "settings", false},
      {HomeMenuItem::OPDS_BROWSER, "opds", true},
      {HomeMenuItem::FILE_TRANSFER, "transfer", true},
#if FREEINK_CAP_GAMES
      {HomeMenuItem::GAMES, "games", true},
#endif
      {HomeMenuItem::SETTINGS_MENU, "settings", true},
      // No such tab, or none asked for: the first.
      {HomeMenuItem::OPDS_BROWSER, "files", false},
      {HomeMenuItem::NONE, "files", false},
      {HomeMenuItem::NONE, "files", true},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(std::string(c.opens) + (c.opds ? " with an OPDS server" : ""));
    OPDS_STORE.servers = c.opds;
    if (home) {
      activityManager.reset();
      reopen(c.item);
    } else {
      open(c.item);
    }
    input->click(Button::Confirm);
    frame();
    EXPECT_EQ(asked(), c.opens);
  }
}

TEST_F(HomeTest, TheTwoIndexMappingsAreInversesOverEveryItemThatHasATab) {
  const auto indexToItem = reach::Slot<reach::IndexToItemTag>::value;
  const auto itemToIndex = reach::Slot<reach::ItemToIndexTag>::value;
  for (const bool opds : {false, true}) {
    const HomeMenuItem items[] = {HomeMenuItem::FILE_BROWSER, HomeMenuItem::LIBRARY,
                                  HomeMenuItem::OPDS_BROWSER, HomeMenuItem::FILE_TRANSFER,
#if FREEINK_CAP_GAMES
                                  HomeMenuItem::GAMES,
#endif
                                  HomeMenuItem::SETTINGS_MENU};
    for (const HomeMenuItem item : items) {
      if (item == HomeMenuItem::OPDS_BROWSER && !opds) continue;  // no such row: its index falls back to 0
      EXPECT_EQ(indexToItem(itemToIndex(item, opds), opds), item)
          << "item " << static_cast<int>(item) << " opds " << opds;
    }
    const int rows = itemToIndex(HomeMenuItem::SETTINGS_MENU, opds) + 1;  // Settings is the last row
    EXPECT_EQ(rows, static_cast<int>(tabNames(opds).size()));
    EXPECT_EQ(indexToItem(rows, opds), HomeMenuItem::NONE) << "one past the last row";
    if (!opds) {
      EXPECT_EQ(itemToIndex(HomeMenuItem::OPDS_BROWSER, opds), 0) << "no such row: falls back to the first";
    }
  }
}

// ---- the classic list: the same mapping under its rows ----

TEST_F(HomeTest, TheListHomeRowsAreInHomesOrderAndEachOpensItsOwnScreen) {
  UITheme::getInstance().coverGridHome = false;
  OPDS_STORE.servers = true;
  open();
  std::string labels = std::string(tr(STR_BROWSE_FILES)) + "|" + tr(STR_LIBRARY) + "|" + tr(STR_OPDS_BROWSER) + "|" +
                       tr(STR_FILE_TRANSFER) + "|";
#if FREEINK_CAP_GAMES
  labels += std::string(tr(STR_GAMES_TITLE)) + "|";  // Games sits just above Settings
#endif
  labels += tr(STR_SETTINGS_TITLE);
  ASSERT_TRUE(UITheme::getInstance().getTheme().drew("drawButtonMenu", labels));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int step = UITheme::getInstance().getTheme().getMenuRowHeight(*renderer) + metrics.menuSpacing;
  const std::vector<std::string> rowsOpened = tabNames(true);
  for (size_t row = 0; row < rowsOpened.size(); ++row) {
    SCOPED_TRACE(rowsOpened[row]);
    activityManager.reset();
    input->tap(100, top + static_cast<int>(row) * step + 2);
    frame();
    EXPECT_EQ(asked(), rowsOpened[row]);
  }
}

}  // namespace
