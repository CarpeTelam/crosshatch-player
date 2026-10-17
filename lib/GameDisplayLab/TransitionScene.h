#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "GrayModel.h"
#include "GrayPreset.h"

// The transition test's picture (the display-lab epic, R4; InkBoy's tuning.md, digest Q6; story dlab-c3's modes),
// pure and pinned by test_transition_scene.cpp. Over the logical screen (what the renderer's orientation shows the
// user): four shade bands K, D, L, W top to bottom, each a quarter of the height; each band has three lanes (a third of
// the band each) holding a rectangle of one of the other three shades, in K, D, L, W order, inset RECT_INSET px top and
// bottom in its lane. Pixels below the fourth band (a height not divisible by four) belong to the last band, and lane
// leftovers to the band's shade. The mode (setMode) sets the rectangles' width and motion:
// - Fast: RECT_W px wide, sweeping from just off the left edge (-width) to the right edge over SWEEP_MS, linearly,
//   then hidden for PAUSE_MS, and the cycle repeats (the SSD1677's sweep, dlab-c1).
// - Slow: RECT_W_SLOW px wide, the same sweep and pause over a crossing time sized from the slot period and the
//   preset's longest dose run in slots (slowSweepMs, GrayPreset::longestRunSlots), so a pixel stays under a rectangle
//   until its transition can finish. The rectangles wait off-screen while the bands paint from white; once the paint
//   has settled the crossing is sized from the longest of the paint's slots and starts (slotDone).
// - Static: a third of the width wide, centred, never moving. The pattern settles, then flips: every target moves on by
//   the flip's offset, +1, +2, +3, +1, ... (flipOffset), so three flips take every pixel through all twelve
//   transitions between them (slotDone).
//
// Holds for photos (story dlab-c4, the epic's D13), only with setHold(true) (the Hold row): slotDone also decides when
// a slot reaches a photo spot (HoldSpot): Static's every settle; Slow's painted bands (the slot that measured), its
// rectangles after a full dwell, and the end of each crossing; Fast's end of each sweep. At a spot the scene shows the
// hold marker, a MARKER_W px dash at the bottom-right of the logical screen in the bottom band's margin below its last
// lane's rectangle (markerRect), in the opposite shade of its band, and retargets it; the sweep clock stands still
// (clockStill) while the task runs slots until the marker settles, then the run holds (Held). resumeHold turns the
// marker back to its band's shade; once it has settled the run goes on (Static's flip only then). The marker's slots
// never reach the modes' decisions, so they never settle Static, count as its slots, or feed Slow's measurement.
//
// The model is in panel coordinates; toPanel maps a logical rectangle the way GfxRenderer::rotateCoordinates maps a
// logical pixel for each orientation.
namespace GameDisplayLab {

// The four GfxRenderer orientations, in its enum's order.
enum class PanelTurn : uint8_t { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

// The transition test's modes, in the Mode row's order.
enum class SweepMode : uint8_t { Static, Slow, Fast };

// The Mode row's next mode: Static, Slow, Fast, then Static again.
SweepMode nextMode(SweepMode mode);

// The photo spots a run holds at (D13).
enum class HoldSpot : uint8_t { StaticSettle, SlowPainted, SlowDwelt, SlowCrossed, FastPause };

// The spot's name in the `gray hold:` log line: "static settle", "slow painted", "slow dwelt", "slow crossed", "fast
// pause".
const char* holdSpotName(HoldSpot spot);

// A shade's letter, K, D, L or W (the log and the Tuning rows).
char shadeLetter(Shade shade);

// Why a crossing passed a photo spot without holding (Hold on).
enum class SpotSkip : uint8_t {
  // Slow's dwell spot: the rectangles' right edge was already off-screen by the dwell's last slot.
  RightEdge,
  // The pause's spot: no slot started inside the pause (a slot outlasted it).
  PauseOutrun,
};

// The reason's text in the `gray hold: <spot> N skipped (<reason>)` log line.
const char* spotSkipName(SpotSkip skip);

// Where a run is with its hold.
enum class HoldPhase : uint8_t {
  // No hold: the sweep clock runs and the marker shows its band's shade.
  Running,
  // A photo spot: the marker is being driven to the opposite shade; the clock stands still.
  MarkerOut,
  // The marker settled: the task runs nothing until a resume.
  Held,
  // Resumed: the marker is being driven back to its band's shade; the clock still stands still.
  MarkerBack,
};

// What dropping a hold in progress (the session cap's clean) cost.
enum class HoldDrop : uint8_t {
  // Static, the marker going out (or held): the sequence restarts at flip 0 and holds again at its settle.
  StaticAgainAtFlipZero,
  // Slow or Fast, the marker going out: that spot is skipped for this crossing; the next spot holds.
  SkippedThisCrossing,
  // Any mode, the marker on its way back: the photo was taken; the way back is cut off by the repaint.
  WayBackCutOff,
};

// The drop's text in the session-cap clean's `gray hold:` log line.
const char* holdDropName(HoldDrop drop);

struct ScreenRect {
  int x;
  int y;
  int w;
  int h;
};

// The panel rectangle that logical rectangle `r` (w, h > 0) covers under `turn`, on a panel panelW x panelH.
ScreenRect toPanel(PanelTurn turn, const ScreenRect& r, int panelW, int panelH);

// A Static pattern that settled: its flip number (0 is the first paint), the offset that flip moved every shade by (0
// for the first paint), and the slots that drove pixels until it settled.
struct StaticSettled {
  uint32_t flip;
  uint8_t offset;
  uint32_t slots;
};

// A photo spot reached: which, and its number (Static: the flip that settled; Slow and Fast: the crossing, from 1;
// Slow's painted bands: 0, before the first crossing).
struct HoldStart {
  HoldSpot spot;
  uint32_t count;
};

// A spot a crossing passed without holding: which, its crossing (from 1), and why.
struct SkippedSpot {
  HoldSpot spot;
  uint32_t count;
  SpotSkip reason;
};

// A hold dropped by cancelHold: its spot, what the drop cost, and the marker's slots it had run in its phase.
struct DroppedHold {
  HoldSpot spot;
  HoldDrop drop;
  uint32_t markerSlots;
};

// What a slot left for the scene to do (TransitionScene::slotDone).
struct SceneStep {
  // Retarget every pixel to the scene (applyAll) before the next slot: a Static flip.
  bool reapply = false;
  // Static: the pattern that settled in this slot.
  std::optional<StaticSettled> settled;
  // Slow: the paint settled in this slot; the crossing is sized from the paint's longest slot and starts now.
  bool measured = false;
  // A photo spot (Hold on): the marker was retargeted to its hold shade and the clock stands still from here.
  std::optional<HoldStart> hold;
  // The marker settled on its hold shade in this slot: the run holds until a resume.
  bool held = false;
  // The marker settled back on its band's shade in this slot: the clock runs again (and a Static flip: reapply).
  bool resumed = false;
  // With `held` or `resumed`: the slots the marker took.
  uint32_t markerSlots = 0;
  // Hold on: a spot this crossing passed without holding (Slow's dwell at the right edge, a pause no slot fell in).
  std::optional<SkippedSpot> skipped;
};

class TransitionScene {
 public:
  // Fast: today's sweep.
  static constexpr uint32_t SWEEP_MS = 5000;
  static constexpr int RECT_W = 12;
  // Fast and Slow: the rectangles are hidden this long between two crossings.
  static constexpr uint32_t PAUSE_MS = 1000;
  // Slow: the rectangles' width and the longest crossing.
  static constexpr int RECT_W_SLOW = 48;
  static constexpr uint32_t SLOW_SWEEP_MAX_MS = 120000;
  static constexpr int RECT_INSET = 8;
  static constexpr int BANDS = 4;
  static constexpr int LANES = 3;
  // The hold marker: MARKER_W px wide, at most MARKER_PX high (the bottom band's margin below its last lane's
  // rectangle); a MARKER_PX square when the bands have no lanes (rectHeight() <= 0).
  static constexpr int MARKER_W = 32;
  static constexpr int MARKER_PX = 16;

  TransitionScene() = default;
  // A logical screen logicalW x logicalH shown on a panel panelW x panelH (the model's size) under `turn`; Fast.
  TransitionScene(PanelTurn turn, int logicalW, int logicalH, int panelW, int panelH);

  // The mode's geometry. Slow's crossing waits for its measurement (slotDone) and will hold a pixel under a rectangle
  // for `dwellSlots` slots (the preset's longestRunSlots()), which the other modes ignore; Slow's dwell spot counts
  // the same slots. Restarts Static's flips, Slow's measurement and the hold (Running); keeps setHold's choice.
  void setMode(SweepMode mode, uint32_t dwellSlots);
  // The Hold row: whether a run holds at its photo spots (off by default, dlab-c3's behaviour). Back to Running.
  void setHold(bool on);
  bool holdOn() const { return holdOn_; }
  HoldPhase holdPhase() const { return phase_; }
  // The sweep clock stands still: the marker is being driven out or back, or the run holds.
  bool clockStill() const { return phase_ != HoldPhase::Running; }
  SweepMode mode() const { return mode_; }
  int rectW() const { return rectW_; }
  // The crossing time (Fast, and Slow once measured; Static's rectangles do not move).
  uint32_t sweepMs() const { return sweepMs_; }
  // Slow before its measurement: the crossing is not sized yet and the rectangles wait off-screen.
  bool slowPending() const { return mode_ == SweepMode::Slow && !slowMeasured_; }
  // The slot period Slow's crossing was sized from (0 before its measurement).
  uint32_t slowPeriodUs() const { return slowMeasured_ ? slowPeriodUs_ : 0; }
  // Slow's crossing time for a slot period on a logical width `logicalW`: the rectangle covers a pixel for at least
  // `dwellSlots` periods, rounded up to a millisecond, at most SLOW_SWEEP_MAX_MS.
  static uint32_t slowSweepMs(uint32_t slotPeriodUs, int logicalW, uint32_t dwellSlots);
  // Whole slots of `slotPeriodUs` a pixel stays under a rectangle at the current crossing (Fast and Slow).
  uint32_t dwellSlotsAt(uint32_t slotPeriodUs) const;
  // Slow's crossing was cut to SLOW_SWEEP_MAX_MS.
  bool sweepCapped() const { return sweepCapped_; }

  // The rectangles' left edge at `elapsedMs` (all twelve share it); nothing during the pause. Slow holds them just off
  // the left edge until its measurement, and starts the crossing at that slot's clock.
  std::optional<int> rectX(uint32_t elapsedMs) const;
  // The shade logical pixel (lx, ly) shows at `elapsedMs` (the marker's hold shade while it is out).
  Shade targetAt(int lx, int ly, uint32_t elapsedMs) const;
  // Retargets every model pixel to the scene at `elapsedMs` (each pixel once).
  void applyAll(GrayModel& model, uint32_t elapsedMs, const GrayPreset& preset) const;
  // Moves the rectangles from `prevMs` to `nowMs`: each new rectangle's area to its shade, the old area it no longer
  // covers back to its band's shade; no other pixel is touched. Nothing when they did not move (always in Static).
  void update(GrayModel& model, uint32_t prevMs, uint32_t nowMs, const GrayPreset& preset) const;

  // After each slot that ran at the sweep clock `elapsedMs`: it drove `driven` pixels, its drive scan was `driveDone`,
  // and the slot took `periodUs` (its measured length); `model` has committed it. While the marker is out or back:
  // only whether it settled in `model` (held, or resumed with a pending Static flip). Otherwise the mode decides.
  // Static: a slot that drove none means the pattern settled; the scene moves on to the next flip and asks for a
  // reapply, or with Hold on, holds first and flips once resumed. Slow, until measured: each slot whose drive scan was
  // delivered offers its `periodUs`, and the first slot that drove none (the bands' paint from white has settled)
  // sizes the crossing from the largest offered and starts it at `elapsedMs` (the rectangles were off-screen, so
  // nothing to reapply). Slow, measured, and Fast: the crossing's photo spots. At a spot with Hold on, the marker in
  // `model` is retargeted to its hold shade with `preset`.
  SceneStep slotDone(GrayModel& model, const GrayPreset& preset, size_t driven, bool driveDone, uint32_t periodUs,
                     uint32_t elapsedMs);
  // The task resumed the run: from Held, the marker in `model` is retargeted back to its band's shade (MarkerBack) and
  // true; in any other phase nothing, false.
  bool resumeHold(GrayModel& model, const GrayPreset& preset);
  // The session cap's clean repainted from white: a hold in progress is dropped (Running, the marker on its band's
  // shade, a pending Static flip dropped), and what it was and what the drop cost returned; nothing when Running. The
  // crossing's passed spots stay passed, so Slow and Fast skip the dropped spot for that crossing.
  std::optional<DroppedHold> cancelHold();
  // The marker's logical rectangle, at the bottom-right: MARKER_W wide, its rows those below the last lane's rectangle
  // (rectTop(BANDS - 1, LANES - 1) + rectHeight() to the bottom), at most MARKER_PX, so it never shares a row with a
  // rectangle; without lanes (rectHeight() <= 0) a MARKER_PX square. Clipped to a smaller screen.
  ScreenRect markerRect() const;
  // The shade the bottom band shows now, which the marker sits on (and returns to).
  Shade markerBandShade() const { return shown(bandShade(BANDS - 1)); }
  // The marker's shade now: markerBandShade() moved on by 2 while it is out, markerBandShade() otherwise.
  Shade markerShade() const;
  // No pixel under the marker has a dose left in `model`.
  bool markerSettled(const GrayModel& model) const;
  // The offset flip `flip` moves every shade by: 0 for the first paint, then +1, +2, +3, +1, ...
  static uint8_t flipOffset(uint32_t flip);
  // Static: the sum of the flips' offsets so far (mod 4), which every shade shows moved by.
  uint8_t shift() const { return shift_; }
  // Back to flip 0, unshifted, no flip pending (a replay, or the session cap's clean back to white).
  void restartFlips();

  static Shade bandShade(int band) { return static_cast<Shade>(band); }
  // The shade of lane `lane` (0..2) in band `band`: the other shades in K, D, L, W order.
  static Shade laneShade(int band, int lane) { return static_cast<Shade>(lane < band ? lane : lane + 1); }
  // `shade` moved on by `offset` steps, K, D, L, W, K, ...
  static Shade shifted(Shade shade, uint8_t offset) {
    return static_cast<Shade>((static_cast<uint8_t>(shade) + offset) % SHADE_COUNT);
  }
  int bandHeight() const { return logicalH_ / BANDS; }
  int laneHeight() const { return bandHeight() / LANES; }
  // The logical top and height of lane `lane`'s rectangle in band `band`.
  int rectTop(int band, int lane) const { return band * bandHeight() + lane * laneHeight() + RECT_INSET; }
  int rectHeight() const { return laneHeight() - 2 * RECT_INSET; }

 private:
  // Retargets the panel pixels of logical rectangle [x0, x1) x [y, y + h), clipped to the logical screen.
  void retargetLogical(GrayModel& model, int x0, int x1, int y, int h, Shade shade, const GrayPreset& preset) const;
  int bandOf(int ly) const;
  // Slow's crossing for `slotPeriodUs` and the dwell, and whether the cap cut it.
  void sizeSlow(uint32_t slotPeriodUs);
  // `shade` as the pattern shows it now (moved by the flips' shift).
  Shade shown(Shade shade) const { return shifted(shade, shift_); }
  // The next flip: the count and the shift move on.
  void flip();
  // The mode's decision for a Running slot (Static's settle and flip, Slow's measurement, the spots); the spot, if any.
  std::optional<HoldStart> patternStep(SceneStep& step, size_t driven, bool driveDone, uint32_t periodUs,
                                       uint32_t elapsedMs);
  // Slow (measured) and Fast at `elapsedMs`: the crossing's number from 0 and the time into it (the crossing first,
  // then the pause); Slow's from its start.
  struct CrossingTime {
    uint32_t cycle;
    uint32_t ms;
  };
  CrossingTime crossingAt(uint32_t elapsedMs) const;
  // Slow (measured) and Fast: the crossing's spots at a Running slot at `elapsedMs`; a spot passed without holding
  // goes into `step` (Hold on).
  std::optional<HoldStart> crossingSpot(SceneStep& step, uint32_t elapsedMs);
  // Back to Running with no hold: the marker on its band's shade, no marker slots, no pending flip, no spot.
  void dropHold();
  // A new crossing `cycle` (from 0): no slot counted toward the dwell, no spot passed.
  void resetCrossingSpots(uint32_t cycle);
  // The marker's panel pixels retargeted to markerShade().
  void retargetMarker(GrayModel& model, const GrayPreset& preset) const;
  // A slot of the marker's (out or back): counted; true, with the count in `step`, once it has settled in `model`.
  bool markerArrived(const GrayModel& model, SceneStep& step);

  PanelTurn turn_ = PanelTurn::LandscapeCounterClockwise;
  int logicalW_ = 0;
  int logicalH_ = 0;
  int panelW_ = 0;
  int panelH_ = 0;
  SweepMode mode_ = SweepMode::Fast;
  int rectW_ = RECT_W;
  uint32_t sweepMs_ = SWEEP_MS;
  bool sweepCapped_ = false;
  // Static: flips so far, the shift they add up to, and the slots that drove pixels since the last one.
  uint32_t flips_ = 0;
  uint8_t shift_ = 0;
  uint32_t settleSlots_ = 0;
  // Slow: the dwell the crossing is sized for, whether the paint has settled and sized it since setMode, the longest
  // slot period seen while painting (then the one it was sized from), and the sweep clock at which the crossing
  // started.
  uint32_t dwellSlots_ = 1;
  bool slowMeasured_ = false;
  uint32_t slowPeriodUs_ = 0;
  uint32_t slowStartMs_ = 0;
  // Holds: the Hold row's choice, the phase, the marker out (its hold shade), the spot in progress, the marker's slots
  // so far, and Static's flip waiting for the resume.
  bool holdOn_ = false;
  HoldPhase phase_ = HoldPhase::Running;
  bool markerOut_ = false;
  std::optional<HoldSpot> spot_;
  uint32_t markerSlots_ = 0;
  bool flipPending_ = false;
  // Slow (measured) and Fast: the crossing cycle the spots below belong to, the slots run with the rectangles' left
  // edge on-screen in it, and which of its spots have passed.
  uint32_t cycle_ = 0;
  uint32_t enteredSlots_ = 0;
  bool dwellPassed_ = false;
  bool crossPassed_ = false;
};

}  // namespace GameDisplayLab
