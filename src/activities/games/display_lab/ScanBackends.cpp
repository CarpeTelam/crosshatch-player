#include "ScanBackends.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Memory.h>

#include "Ssd1677ScanBackend.h"
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
    case Controller::UC8179:
      return nullptr;
    // Not X4 Pro controllers.
    case Controller::UC8253:
    case Controller::ED2208:
    case Controller::LgfxEpd:
    case Controller::IT8951:
    case Controller::UC8279C:
      return nullptr;
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
