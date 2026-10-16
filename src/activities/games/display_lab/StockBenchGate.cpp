#include "StockBenchGate.h"

#if CROSSHATCH_DISPLAY_LAB

#include <I18n.h>

#include "ScanBackends.h"

bool usesStockBench(const HalDisplay::Controller controller) {
  const bool ultraChip = controller == HalDisplay::Controller::UC8179 || controller == HalDisplay::Controller::UC8279;
  return ultraChip && !hasScanBackend(controller);  // a backend always wins
}

const char* labRowSubtitle() {
  // BoardConfig comes with HalDisplay.h (HalDisplay::Controller is its DisplayController).
  if (BoardConfig::isX4Pro() && usesStockBench(display.getController())) return tr(STR_GAMES_DLAB_STOCK_ROW_DESC);
  return tr(STR_GAMES_DLAB_ROW_DESC);
}

#endif  // CROSSHATCH_DISPLAY_LAB
