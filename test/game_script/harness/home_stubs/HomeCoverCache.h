#pragma once

#include <FreeInkUICore.h>

#include <string>

class GfxRenderer;

// HomeCoverCache's surface as CoverGridHomeUi calls it (src/components/HomeCoverCache.h). The real one decodes
// a cover bitmap off the card into a PSRAM snapshot; here no cover is ever painted (paint answers false, so the
// grid draws its empty cover), and `MAX_COVERS` is the real one's, which CoverGridHomeUi static_asserts against.
class HomeCoverCache {
 public:
  static constexpr size_t MAX_COVERS = 7;
  explicit HomeCoverCache(GfxRenderer&) {}
  void begin() {}
  void prepare() {}
  void invalidate() {}
  void invalidate(size_t) {}
  bool paint(freeink::ui::Rect, size_t, const std::string&) { return false; }
};
