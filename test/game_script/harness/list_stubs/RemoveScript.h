#pragma once

// GamePackageInstaller::remove for the screens that list games, scripted by the test (the real one, over a fake
// card, is GameRemoveTest's). The launcher needs what remove answers and when it is asked: `result` is the answer,
// `onRemove` runs inside the call so a test can take the folder (or only its .pkg) off the card at the moment the
// real remove would, and the record shows the ids it was called with and what the screen looked like when it ran.
// A test that never sets a script gets an installer that removes nothing and reports None.

#include <functional>
#include <string>
#include <vector>

#include "games/GamePackageInstaller.h"

namespace removescript {

struct Script {
  GamePackageInstaller::Error result = GamePackageInstaller::Error::None;  // what remove() returns
  std::function<void()> onRemove;                                          // runs inside remove(), before it returns
  std::vector<std::string> ids;                                            // every id remove() was called with
  bool popupWhenRemoving = false;  // whether the "Removing..." popup had been drawn when remove() ran
  bool lockWhenRemoving = false;   // whether the render lock was held when remove() ran
};

Script& script();
inline void reset() { script() = Script{}; }

}  // namespace removescript
