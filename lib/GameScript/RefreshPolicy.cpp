#include "RefreshPolicy.h"

namespace GameScript {

bool RefreshPolicy::decide(const uint64_t frameHash, const Refresh hint, Refresh& mode) {
  if (!force && shown && frameHash == shownHash) return false;
  mode = force ? Refresh::Full : hint;
  force = false;
  if (mode == Refresh::Fast && ++fastInARow > FAST_REFRESH_LIMIT) mode = Refresh::Half;
  if (mode != Refresh::Fast) fastInARow = 0;
  shown = true;
  shownHash = frameHash;
  return true;
}

}  // namespace GameScript
