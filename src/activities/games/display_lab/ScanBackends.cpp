#include "ScanBackends.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Memory.h>

#include "Ssd1677ScanBackend.h"
#include "UC8179ScanBackend.h"
#include "UC8279ScanBackend.h"

using Controller = HalDisplay::Controller;

namespace {

using Factory = std::unique_ptr<ScanBackend> (*)(GfxRenderer& renderer);

template <typename T>
std::unique_ptr<ScanBackend> make(GfxRenderer& renderer) {
  return makeUniqueNoThrow<T>(renderer);
}

// The one table of which controller has which backend: hasScanBackend and makeScanBackend both read it, so the
// launcher row and the lab never disagree. Each later lane edits only its own controller's case.
Factory factoryFor(const Controller controller) {
  switch (controller) {
    case Controller::SSD1677:
      return &make<Ssd1677ScanBackend>;
    // The UC8179 batch. The check follows story dlab-a2's backend-choice row (no backend when the facade's driver
    // reports no lab support); every build with the lab compiles the seam, whose Uc8179Driver reports it, so every
    // UC8179 X4 Pro the lab builds for gets this backend and the scan menu.
    case Controller::UC8179:
      return display.eink().labSupported() ? &make<UC8179ScanBackend> : nullptr;
    // Not X4 Pro controllers.
    case Controller::UC8253:
    case Controller::ED2208:
    case Controller::LgfxEpd:
    case Controller::IT8951:
    case Controller::UC8279C:
      return nullptr;
    // The X4 Pro's UC8279 batch.
    case Controller::UC8279:
      return &make<UC8279ScanBackend>;
  }
  return nullptr;
}

}  // namespace

bool hasScanBackend(const Controller controller) { return factoryFor(controller) != nullptr; }

std::unique_ptr<ScanBackend> makeScanBackend(const Controller controller, GfxRenderer& renderer) {
  const Factory factory = factoryFor(controller);
  return factory ? factory(renderer) : nullptr;
}

#endif  // CROSSHATCH_DISPLAY_LAB
