// The scripted GamePackageInstaller (InstallerScript.h), in place of src/games/GamePackageInstaller.cpp.

#if FREEINK_CAP_GAMES

#include <I18n.h>

#include "InstallerScript.h"
#include "RenderLockProbe.h"
#include "components/UITheme.h"

namespace installerscript {

Script& script() {
  static Script instance;
  return instance;
}

}  // namespace installerscript

namespace GamePackageInstaller {

bool hasInbox() {
  installerscript::script().order.push_back("hasInbox");
  return installerscript::script().inbox;
}

Report installAll() {
  installerscript::Script& script = installerscript::script();
  script.order.push_back("installAll");
  script.popupWhenInstalling = UITheme::getInstance().getTheme().drew("drawPopup", tr(STR_GAMES_INSTALLING));
  script.lockWhenInstalling = fakelock::held();
  if (script.onInstall) script.onInstall();
  return script.report;
}

}  // namespace GamePackageInstaller

#endif  // FREEINK_CAP_GAMES
