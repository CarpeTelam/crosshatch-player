#pragma once

#include "ScanBackend.h"

#if CROSSHATCH_DISPLAY_LAB

#include <HalMemory.h>

#include <cstddef>
#include <cstdint>

class GfxRenderer;

// The SSD1677 batch's scan backend (Part A's path behind the D1 seam). Its pure parts are
// GameDisplayLab::Ssd1677Encoding: the 112-byte scan LUT (stock voltage tail) for the request's frames, rate index and
// polarity, and the two RAM planes from the masks (white to BW, black to RED). scan() writes both planes raw into RAM
// 0x24 and 0x26 through the facade's overlay-grayscale path (copyGrayscaleBuffers), then times
// displayGrayBuffer(turnOff=false, lut, factoryMode=true), which uploads the LUT (bytes 0-104 to 0x32, the voltage
// tail to 0x03, 0x04 and 0x2C), activates with 0x22 = 0xCC and blocks on BUSY. The OTP command is never sent.
//
// It owns the two encoded planes, `display.getBufferSize()` bytes each in PSRAM (48,000 on the X4 Pro), allocated by
// the constructor and freed with the backend, which the lab creates in onEnter and destroys in onExit; a backend whose
// planes did not fit is not available(). The masks of every request are `display.getBufferSize()` bytes too.
class Ssd1677ScanBackend final : public ScanBackend {
 public:
  explicit Ssd1677ScanBackend(GfxRenderer& renderer);

  bool available() override;
  const char* name() override;
  uint8_t rateCount() override;
  const char* rateLabel(uint8_t index) override;
  Scan scan(const ScanRequest& request, uint32_t& busyUs) override;
  void clean() override;
  // This LUT drives rows 1 and 2 only; both-bits pixels select LUT3, which holds.
  bool supportsWeakRow() override;
  // The unswapped drive: ssd1677RowLabel(row, false). A swapped request drives the other row's code.
  const char* rowLabel(uint8_t row) const override;

 private:
  // In this order: the constructor allocates the planes from planeBytes.
  GfxRenderer& renderer;
  size_t planeBytes;
  HalMemory::PsramBuffer bwPlane;
  HalMemory::PsramBuffer redPlane;
};

#endif  // CROSSHATCH_DISPLAY_LAB
