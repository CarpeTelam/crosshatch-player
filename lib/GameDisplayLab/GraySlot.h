#pragma once

#include <cstddef>
#include <cstdint>

#include "GrayModel.h"
#include "GrayPreset.h"
#include "MaskPlanes.h"

// One slot of the gray engine (the display-lab epic, D5; the digest's Q5 pair: drive scan, follow scan, commit),
// pure and host-tested (test_gray_model.cpp) with a fake scan. The transition screen passes a lambda that sends the
// masks through DisplayLabActivity::scan.
namespace GameDisplayLab {

struct SlotResult {
  // Pixels the drive scan carried; 0 means no scan was sent.
  size_t driven = 0;
  // Scans sent: 0, 1 (the drive scan was not delivered), or 2.
  uint8_t scans = 0;
  // The scans' summed busy time, which the slot period (SlotScheduler) is built from.
  uint32_t busyUs = 0;
  bool driveDone = false;
  bool followDone = false;
};

// Builds the slot's masks from `model`; when no pixel has a dose left, sends nothing. Otherwise sends the drive scan
// with `drive`, then, only when it was delivered, the follow-up scan with `follow` (its pixels driven again, every
// other pixel held: the HOLD-ending scan), then commits what was delivered. `scan(const MaskPlanes& masks, uint32_t&
// busyUs) -> bool` sends one scan of `masks` and returns true when it was delivered (DisplayLabActivity::scan's Done).
template <typename ScanFn>
SlotResult runGraySlot(GrayModel& model, MaskPlanes& drive, MaskPlanes& follow, const GrayPreset& preset,
                       const bool weakRow, ScanFn&& scan) {
  SlotResult result;
  result.driven = model.buildMasks(drive, follow, preset, weakRow);
  if (result.driven == 0) return result;
  uint32_t busyUs = 0;
  result.driveDone = scan(static_cast<const MaskPlanes&>(drive), busyUs);
  result.scans = 1;
  result.busyUs = busyUs;
  // The follow-up scan only when it would drive a pixel: a slot whose active transitions all hold sends one scan,
  // which halves its cost on a controller where every scan streams the whole panel (dlab-c2).
  if (result.driveDone && follow.any()) {
    busyUs = 0;
    result.followDone = scan(static_cast<const MaskPlanes&>(follow), busyUs);
    result.scans = 2;
    result.busyUs += busyUs;
  }
  model.commit(result.driveDone, result.followDone, preset);
  return result;
}

}  // namespace GameDisplayLab
