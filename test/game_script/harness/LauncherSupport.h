#pragma once

// What two launcher suites (GamesLauncherTest, GameRemoveLauncherTest) share about the screen a launcher row opens
// (ModePickerTest goes on into the title screen, so its fixture opens the pushed screen itself: openPushedTitle). Since
// entry 7 of epic-pass-and-play every startable row pushes the game's title screen (GameModeActivity), and since entry
// 8 there is one row per game, so "which game did the row open" is the name heading the title screen it pushed
// (openedTitle). A case that must Leave a real match starts one on that screen (startNewOnTitle). Both stand in for
// what ActivityManager does with the pushed screen: enter it, draw it, and let it go (exitActivity, then the
// destructor, both under the render lock: activityManager.exitHolding, destroyHolding).

#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/UiListActivity.h"
#include "activities/games/GameModeActivity.h"
#include "activities/games/GamesLauncherActivity.h"
#include "components/UITheme.h"

namespace launcher {

// The recording target a list screen draws into: its UiAppHost is a protected base, reached from a class derived from
// UiListActivity (never constructed), as NameOf reaches Activity's protected name.
struct TargetOf : UiListActivity {
  static screen::RecordingTarget& of(UiListActivity& screen) { return static_cast<UiAppHost&>(screen).uiTarget; }
};

// The title screen `list` pushed: entered and drawn as the manager would, then let go as Back lets it go (the manager
// exits and destroys it), so the launcher is the screen again and RecordingTarget::newest() is the launcher's once more
// (the title screen's target was built last, and took newest() with it when it went). Returns the name heading the
// title screen, which is the game's name, or a <...> diagnostic when no title screen was pushed or none was drawn.
inline std::string openedTitle(GamesLauncherActivity& list) {
  if (activityManager.pushedActivities.size() != 1) {
    return "<" + std::to_string(activityManager.pushedActivities.size()) + " screens pushed>";
  }
  std::unique_ptr<Activity>& pushed = activityManager.pushedActivities.back();
  if (dynamic_cast<GameModeActivity*>(pushed.get()) == nullptr) return "<not a title screen>";
  // The launcher only pushes: a row that also started a match is a fault the header alone would not show.
  if (!activityManager.replacements.empty()) return "<a match started>";
  ThemeDouble& theme = UITheme::getInstance().getTheme();
  const size_t from = theme.calls.size();  // the title screen's draw calls follow the launcher's
  pushed->onEnter();
  pushed->render(RenderLock(*pushed));
  std::string header = "<no header drawn>";
  for (size_t i = from; i < theme.calls.size(); ++i)
    if (theme.calls[i].what == "drawHeader") header = theme.calls[i].text;
  activityManager.exitHolding(*pushed);
  activityManager.destroyHolding(pushed);
  activityManager.pushedActivities.pop_back();
  screen::RecordingTarget::newest() = &TargetOf::of(list);
  return header;
}

// A case that must Leave a real match: opens the pushed title screen as the manager would, taps its first New row (and
// New game when the screen asks first, over a save), and lets the screen go as the manager lets a replaced one go, so
// the match is the replacement the case reads. RecordingTarget::newest() is left to the screens that went, so the case
// builds the launcher again (as goToGames() does after Leave) before it reads one.
inline void startNewOnTitle(MappedInputManager& input) {
  ASSERT_EQ(activityManager.pushedActivities.size(), 1u) << "the row pushes the game's title screen";
  Activity& title = *activityManager.pushedActivities.back();
  ASSERT_NE(dynamic_cast<GameModeActivity*>(&title), nullptr);
  title.onEnter();
  screen::RecordingTarget& target = *screen::RecordingTarget::newest();  // the title screen's: built last
  const auto tapFirst = [&](const std::vector<std::string>& labels) {
    target.forget();
    title.render(RenderLock(title));
    for (const screen::DrawnText& drawn : target.drawn) {
      if (std::find(labels.begin(), labels.end(), drawn.text) == labels.end()) continue;
      input.tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
      title.loop();
      input.clear();
      return true;
    }
    return false;
  };
  const size_t before = activityManager.replacements.size();
  ASSERT_TRUE(tapFirst({tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY)}))
      << target.joined();
  if (activityManager.replacements.size() == before) {  // no match yet: the screen asks first, over a save
    ASSERT_TRUE(tapFirst({tr(STR_GAMES_NEW_GAME)})) << target.joined();
  }
  activityManager.exitHolding(title);
  activityManager.destroyHolding(activityManager.pushedActivities.back());
  activityManager.pushedActivities.pop_back();
}

}  // namespace launcher
