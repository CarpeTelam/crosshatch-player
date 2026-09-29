#pragma once

// GamePackageInstaller for the screens that open the Games list: the real header, with the two
// functions the list calls (hasInbox, installAll) scripted by the test. The installer itself, over
// real packages, is GameInstallerTest's and PackageHardeningTest's; a screen test needs what the
// installer reports (a Report, one Error at a time) and when it is asked, not a package to
// unpack. `onInstall` runs inside installAll, so a test can put an installed game on the fake
// card at the moment the real installer would.

#include <functional>
#include <string>
#include <vector>

#include "games/GamePackageInstaller.h"

namespace installerscript {

struct Script {
  bool inbox = false;                   // what hasInbox() answers
  GamePackageInstaller::Report report;  // what installAll() returns
  std::function<void()> onInstall;      // runs inside installAll(), before it returns
  std::vector<std::string> order;       // "hasInbox" and "installAll", in the order the screen called them
  bool popupWhenInstalling = false;     // whether the "Installing" popup had been drawn when installAll ran

  int installCalls() const {
    int n = 0;
    for (const std::string& call : order) n += call == "installAll";
    return n;
  }
};

Script& script();
inline void reset() { script() = Script{}; }

}  // namespace installerscript
