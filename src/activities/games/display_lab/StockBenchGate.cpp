#include "StockBenchGate.h"

#if CROSSHATCH_DISPLAY_LAB

#include <I18n.h>

#include "ScanBackends.h"

bool isUltraChip(const HalDisplay::Controller controller) {
  return controller == HalDisplay::Controller::UC8179 || controller == HalDisplay::Controller::UC8279;
}

GameDisplayLab::LabMenu labMenuFor(const HalDisplay::Controller controller) {
  return GameDisplayLab::labMenuFor(isUltraChip(controller), hasScanBackend(controller));
}

bool usesStockBench(const HalDisplay::Controller controller) {
  return labMenuFor(controller) == GameDisplayLab::LabMenu::StockBench;  // a backend always gets the scan screens
}

const char* labRowSubtitle() {
  // BoardConfig comes with HalDisplay.h (HalDisplay::Controller is its DisplayController).
  if (BoardConfig::isX4Pro() && usesStockBench(display.getController())) return tr(STR_GAMES_DLAB_STOCK_ROW_DESC);
  return tr(STR_GAMES_DLAB_ROW_DESC);
}

#endif  // CROSSHATCH_DISPLAY_LAB
