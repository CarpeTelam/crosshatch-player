// The scripted GamePackageInstaller::remove (RemoveScript.h), in place of the one in
// src/games/GamePackageInstaller.cpp. hasInbox and installAll are GamePackageInstallerDouble.cpp's.

#if FREEINK_CAP_GAMES

#include <I18n.h>

#include "RemoveScript.h"
#include "RenderLockProbe.h"
#include "components/UITheme.h"

namespace removescript {

Script& script() {
  static Script instance;
  return instance;
}

}  // namespace removescript

namespace GamePackageInstaller {

Error remove(const char* id) {
  removescript::Script& script = removescript::script();
  script.ids.push_back(id ? id : "");
  script.popupWhenRemoving = UITheme::getInstance().getTheme().drew("drawPopup", tr(STR_GAMES_REMOVING));
  script.lockWhenRemoving = fakelock::held();
  if (script.onRemove) script.onRemove();
  return script.result;
}

}  // namespace GamePackageInstaller

#endif  // FREEINK_CAP_GAMES
