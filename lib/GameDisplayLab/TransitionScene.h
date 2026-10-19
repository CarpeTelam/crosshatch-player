#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "GrayModel.h"
#include "GrayPreset.h"
#include "PanelGeometry.h"

// The transition test's picture (the display-lab epic, R4; InkBoy's tuning.md, digest Q6; story dlab-c3's modes),
// pure and pinned by test_transition_scene.cpp. Over the logical screen (what the renderer's orientation shows the
// user): seven level bands 0 (black) .. 6 (white) top to bottom (story dlab-g2, the epic's D14), each a seventh of the
// height; each band has one lane holding a rectangle of level (band + 3) mod 7, inset RECT_INSET px top and bottom in
// the band. Pixels below the seventh band (a height not divisible by seven) belong to the last band. The mode
// (setMode) sets the rectangles' width and motion:
// - Fast: RECT_W px wide, sweeping from just off the left edge (-width) to the right edge over SWEEP_MS, linearly,
//   then hidden for PAUSE_MS, and the cycle repeats (the SSD1677's sweep, dlab-c1). Like Slow's, the rectangles wait
//   off-screen while the bands paint from white; the sweep clock's first crossing starts once the paint has settled.
// - Slow: RECT_W_SLOW px wide, the same sweep and pause over a crossing time sized from the slot period and the
//   preset's longest dose run in slots (slowSweepMs, GrayPreset::longestRunSlots), so a pixel stays under a rectangle
//   until its transition can finish. The rectangles wait off-screen while the bands paint from white; once the paint
//   has settled the crossing is sized from the longest of the paint's slots and starts (slotDone).
// - Static: a third of the width wide, centred, never moving. The pattern settles, then flips: every target moves on by
//   the flip's offset, +1, +2, .., +6, +1, ... (flipOffset); the bands hold every level, so flip k drives every
//   transition of offset k and six flips take the pixels through all 42 between them (slotDone).
//
// Holds for photos (story dlab-c4, the epic's D13), with setHold(true) (the Hold row), and always in Ladder
// (holdsAtSpots): slotDone also decides when a slot reaches a photo spot (HoldSpot): Static's every settle; Slow's
// painted bands (the slot that measured), its rectangles after a full dwell, and the end of each crossing; Fast's
// painted bands and the end of each sweep; the Ladder's two stage ends. At a spot the scene shows the hold marker, a
// pause symbol: two bars (markerBars) inside markerRect, a MARKER_W px wide box at the bottom-right of the logical
// screen (in the bottom band's margin below its lane's rectangle; in Ladder MARKER_PX high under the columns), in the
// level farther from its band's (holdLevelOver: 0 or 6, as the gray viewer's marker), the gap between them never
// driven; the sweep clock stands still (clockStill) while the task runs slots until the marker settles, then the run
// holds (Held). resumeHold turns the marker back to its band's shade; once it has settled the run goes on (Static's
// flip only then). The marker's slots never reach the modes' decisions, so they never settle Static, count as its
// slots, or feed Slow's measurement.
//
// Ladder (story dlab-g1, the epic's D14): no bands or rectangles but a measurement of the panel's response to single
// pulses. The rows above the marker's (ladderHeight; in Ladder the marker is MARKER_PX high at the bottom-right, with
// no lanes to keep clear of) split into a top half starting from white and a bottom half starting from black; the
// marker's rows belong to the bottom half. Each half holds up to LADDER_STEPS columns (ladderLayout), column n (1..24,
// left to right) at logical x (n - 1) * pitch, a 1 px gutter of the half's origin after each. The run paints the base
// (the bottom half and the marker's rows to black, through retarget and the preset's W>K dose), then stage 1 (the
// ladder: column n gets exactly n pulses toward the opposite shade through GrayModel::driveRect, one a slot, each one
// scan, since the task runs ladderPulsing slots on slotPreset's held rhythm), then stage 2 (the return: each column n
// pulses back toward its origin), then ends (finished). A stage is LADDER_STEPS slots, and its columns are end-aligned:
// column n is driven before the stage's slot LADDER_STEPS - n (from 0), so its n pulses are the stage's last n slots
// and every column's last pulse lands in the same slot (the same rest before the photo). Stage 1 and 2 each end at a
// photo spot (HoldSpot::Ladder 1 and 2) that always holds (with Hold off too: the Ladder is nothing but its photos),
// the next stage (or the end) waiting for the marker to be back.
//
// The model is in panel coordinates; toPanel (PanelGeometry.h, with PanelTurn and ScreenRect) maps a logical rectangle
// the way GfxRenderer::rotateCoordinates maps a logical pixel for each orientation; the marker's pause symbol and its
// settle check are PanelGeometry's pauseBars and barsSettled, which the gray viewer shares.
namespace GameDisplayLab {

// The transition test's modes, in the Mode row's order.
enum class SweepMode : uint8_t { Static, Slow, Fast, Ladder };

// The Mode row's next mode: Static, Slow, Fast, Ladder, then Static again.
SweepMode nextMode(SweepMode mode);

// The photo spots a run holds at (D13).
enum class HoldSpot : uint8_t { StaticSettle, SlowPainted, SlowDwelt, SlowCrossed, FastPainted, FastPause, Ladder };

// The spot's name in the `gray hold:` log line: "static settle", "slow painted", "slow dwelt", "slow crossed", "fast
// painted", "fast pause", "ladder".
const char* holdSpotName(HoldSpot spot);

// A level's name where the user sees it (the log and the Tuning rows): its digit (D14, D15), '0' black, '3' the 50 %
// gray, '6' white; '?' past white (TRANSPARENT).
char shadeName(Shade shade);

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
  // A photo spot: the marker is being driven to black or white, whichever is farther from its band (holdLevelOver);
  // the clock stands still.
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
  // Slow or Fast, the marker going out: that spot is skipped for this crossing (a painted spot: the crossing starts
  // without its photo); the next spot holds.
  SkippedThisCrossing,
  // Any mode, the marker on its way back: the photo was taken; the way back is cut off by the repaint.
  WayBackCutOff,
  // Ladder, the marker going out (or held): the ladder restarts from the base, so it holds at `ladder 1` first again,
  // whichever stage's hold was dropped.
  LadderFromBase,
};

// Where a Ladder run is.
enum class LadderStage : uint8_t {
  // The bottom half (and the marker's rows) painting to black from the clean's white.
  Base,
  // Stage 1: column n's n pulses toward the opposite shade.
  Ladder,
  // Stage 2: column n's n pulses back toward its origin.
  Return,
  // The return settled, held and resumed (the Ladder always holds): the run is over.
  Done,
};

// The Ladder's two halves: the top one starts from white, the bottom one from black.
enum class LadderHalf : uint8_t { FromWhite, FromBlack };

// The Ladder's columns across a logical width: how many, their width, and the gutter after each (pitch = width +
// gutter).
struct LadderLayout {
  int columns;
  int width;
  int gutter;
};

// The drop's text in the session-cap clean's `gray hold:` log line.
const char* holdDropName(HoldDrop drop);

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

// A Ladder stage that settled: which (Base, Ladder or Return), and the slots that drove pixels until it did.
struct LadderSettled {
  LadderStage stage;
  uint32_t slots;
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
  // Fast: the paint settled in this slot after this many slots that drove pixels; the sweep starts now.
  std::optional<uint32_t> fastPainted;
  // A photo spot that holds (holdsAtSpots: Hold on, or the Ladder): the marker was retargeted to its hold shade and
  // the clock stands still from here.
  std::optional<HoldStart> hold;
  // The marker settled on its hold shade in this slot: the run holds until a resume.
  bool held = false;
  // The marker settled back on its band's shade in this slot: the clock runs again (and a Static flip: reapply).
  bool resumed = false;
  // With `held` or `resumed`: the slots the marker took.
  uint32_t markerSlots = 0;
  // Hold on: a spot this crossing passed without holding (Slow's dwell at the right edge, a pause no slot fell in).
  std::optional<SkippedSpot> skipped;
  // Ladder: the stage that settled in this slot (stage 1 after the base, or the hold after stage 1 or 2, starts in the
  // same slot).
  std::optional<LadderSettled> ladderSettled;
  // Ladder: the run is over (the return settled, held and resumed); the task ends it.
  bool finished = false;
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
  // One band per level, one lane per band (story dlab-g2), its rectangle LANE_OFFSET levels along.
  static constexpr int BANDS = SHADE_COUNT;
  static constexpr int LANES = 1;
  static constexpr uint8_t LANE_OFFSET = 3;
  // The hold marker's box: MARKER_W px wide, at most MARKER_PX high (the bottom band's margin below its last lane's
  // rectangle; in Ladder MARKER_PX); a MARKER_PX square when the bands have no lanes (rectHeight() <= 0). It shows a
  // pause symbol: two bars MARKER_GAP px apart (12, 8, 12 at 32 wide).
  static constexpr int MARKER_W = 32;
  static constexpr int MARKER_PX = 16;
  static constexpr int MARKER_GAP = 8;
  // Ladder: columns per half, column n getting n pulses; a pulse count is a dose count, so at most DOSE_MAX.
  static constexpr int LADDER_STEPS = 24;
  static_assert(LADDER_STEPS <= DOSE_MAX, "a ladder column's pulses are one driveRect's doses");

  TransitionScene() = default;
  // A logical screen logicalW x logicalH shown on a panel panelW x panelH (the model's size) under `turn`; Fast.
  TransitionScene(PanelTurn turn, int logicalW, int logicalH, int panelW, int panelH);
  // The same screen and panel on this scene, everything else kept (setMode and setHold set a run's state): what the
  // constructor does, without a second scene on the caller's stack (its held-rhythm preset makes one over 200 bytes).
  void setScreen(PanelTurn turn, int logicalW, int logicalH, int panelW, int panelH);

  // The mode's geometry. Slow's crossing waits for its measurement (slotDone) and will hold a pixel under a rectangle
  // for `dwellSlots` slots (the preset's longestRunSlots()), which the other modes ignore; Slow's dwell spot counts
  // the same slots. Restarts Static's flips, Slow's measurement, the Ladder (at its base) and the hold (Running);
  // keeps setHold's choice.
  void setMode(SweepMode mode, uint32_t dwellSlots);
  // The Hold row: whether a run holds at its photo spots (off by default, dlab-c3's behaviour); the Ladder holds either
  // way (holdsAtSpots). Back to Running.
  void setHold(bool on);
  bool holdOn() const { return holdOn_; }
  HoldPhase holdPhase() const { return phase_; }
  // The sweep clock stands still: the marker is being driven out or back, or the run holds.
  bool clockStill() const { return phase_ != HoldPhase::Running; }
  SweepMode mode() const { return mode_; }
  int rectW() const { return rectW_; }
  // The crossing time (Fast, and Slow once measured; Static's rectangles do not move; Ladder: 0, nothing crosses).
  uint32_t sweepMs() const { return sweepMs_; }
  // Slow before its measurement: the crossing is not sized yet and the rectangles wait off-screen.
  bool slowPending() const { return mode_ == SweepMode::Slow && !painted_; }
  // Slow or Fast before the bands' paint has settled: the rectangles wait off-screen.
  bool paintPending() const { return (mode_ == SweepMode::Slow || mode_ == SweepMode::Fast) && !painted_; }
  // The slot period Slow's crossing was sized from (0 before its measurement; always 0 in Fast).
  uint32_t slowPeriodUs() const { return painted_ ? slowPeriodUs_ : 0; }
  // Slow's crossing time for a slot period on a logical width `logicalW`: the rectangle covers a pixel for at least
  // `dwellSlots` periods, rounded up to a millisecond, at most SLOW_SWEEP_MAX_MS.
  static uint32_t slowSweepMs(uint32_t slotPeriodUs, int logicalW, uint32_t dwellSlots);
  // Whole slots of `slotPeriodUs` a pixel stays under a rectangle at the current crossing (Fast and Slow).
  uint32_t dwellSlotsAt(uint32_t slotPeriodUs) const;
  // Slow's crossing was cut to SLOW_SWEEP_MAX_MS.
  bool sweepCapped() const { return sweepCapped_; }

  // The rectangles' left edge at `elapsedMs` (all seven share it); nothing during the pause, and nothing in Ladder,
  // which has none. Slow and Fast hold them just off the left edge until the paint has settled, and start the crossing
  // at that slot's clock.
  std::optional<int> rectX(uint32_t elapsedMs) const;
  // The shade logical pixel (lx, ly) shows at `elapsedMs` (the marker's hold shade while it is out; in Ladder its
  // half's origin, which the base paints: the stages' pulses are driveRect's, not the scene's).
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
  // nothing to reapply). Fast, until painted: the first slot that drove none starts the sweep at `elapsedMs`
  // (fastPainted, the FastPainted spot). Slow, measured, and Fast, painted: the crossing's photo spots. Ladder: the
  // base ends at its first slot that drove none, and stage 1 starts in `model`; in stage 1 and 2 each slot drives the
  // next column in `model` (end- aligned), and the first slot that drove none after LADDER_STEPS is the stage's end
  // (ladderSettled), a photo spot whose next stage (or the end) waits for the marker to be back. At a spot that holds
  // (holdsAtSpots), the marker in `model` is retargeted to its hold shade with `preset`.
  SceneStep slotDone(GrayModel& model, const GrayPreset& preset, size_t driven, bool driveDone, uint32_t periodUs,
                     uint32_t elapsedMs);
  // The task resumed the run: from Held, the marker in `model` is retargeted back to its band's shade (MarkerBack) and
  // true; in any other phase nothing, false.
  bool resumeHold(GrayModel& model, const GrayPreset& preset);
  // The session cap's clean repainted from white: a hold in progress is dropped (Running, the marker on its band's
  // shade, a pending Static flip or Ladder stage dropped), and what it was and what the drop cost returned; nothing
  // when Running. The crossing's passed spots stay passed, so Slow and Fast skip the dropped spot for that crossing.
  std::optional<DroppedHold> cancelHold();
  // Ladder, after the session cap's clean and cancelHold's `dropped`: true when the run is over (the marker's way back
  // from `ladder 2` was cut off, so both photos were taken; the stage is Done); otherwise the ladder restarts from its
  // base (restartLadder) and false.
  bool ladderCleaned(const std::optional<DroppedHold>& dropped);
  // The marker's logical rectangle, at the bottom-right: MARKER_W wide, its rows those below the last lane's rectangle
  // (rectTop(BANDS - 1, LANES - 1) + rectHeight() to the bottom), at most MARKER_PX, so it never shares a row with a
  // rectangle; without lanes (rectHeight() <= 0) a MARKER_PX square; in Ladder MARKER_W x MARKER_PX (no lanes to keep
  // clear of; the columns end above it). Clipped to a smaller screen.
  ScreenRect markerRect() const;
  // The shade the bottom band shows now, which the marker sits on (and returns to); in Ladder the bottom half's origin,
  // black, which the marker's rows belong to.
  Shade markerBandShade() const { return mode_ == SweepMode::Ladder ? Shade::K : shown(bandShade(BANDS - 1)); }
  // The pause symbol's two bars inside markerRect(), logical: each (w - MARKER_GAP) / 2 wide at its left and right
  // edge, full height, the gap between them never driven; on a box too narrow for both and the gap, the whole box and
  // an empty second bar.
  std::array<ScreenRect, 2> markerBars() const;
  // The marker's shade now: holdLevelOver(markerBandShade()) while it is out, markerBandShade() otherwise.
  Shade markerShade() const;
  // The hold level over a band of `band`: black or white, whichever is farther from it (black from level 3 up), the
  // gray viewer's rule (story dlab-g2: three along would put the marker over black on 3, the bottom lane's level).
  static Shade holdLevelOver(Shade band) {
    return farEndOver(uint64_t{static_cast<uint8_t>(band)} * GrayModel::LEVEL_STEP, 1);
  }
  // No pixel of the marker's bars has a dose left in `model`.
  bool markerSettled(const GrayModel& model) const;
  // The offset flip `flip` moves every level by: 0 for the first paint, then +1, +2, .., +6, +1, ...
  static uint8_t flipOffset(uint32_t flip);
  // Static: the sum of the flips' offsets so far (mod 7), which every level shows moved by.
  uint8_t shift() const { return shift_; }
  // Back to flip 0, unshifted, no flip pending (a replay, or the session cap's clean back to white).
  void restartFlips();

  // Ladder: the columns across logical width `logicalW` (story dlab-g1): pitch = logicalW / LADDER_STEPS, each column
  // pitch - 1 px wide with a 1 px gutter of its half's origin after it, so the columns can be counted and an untouched
  // strip of the origin sits beside each (480: 24 of 19 px; 800: 24 of 32 px); a pitch of 1 (24..47 px): 1 px columns,
  // no gutter; under LADDER_STEPS px, logicalW columns of 1 px; none for a width of 0 or less.
  static LadderLayout ladderLayout(int logicalW);
  // The layout across this scene's logical width.
  LadderLayout ladderLayout() const { return ladderLayout(logicalW_); }
  // Ladder: the rows the two halves share, those above the marker's (markerRect().y, logicalH - MARKER_PX, at least 0):
  // the top half is [0, h / 2), the bottom half [h / 2, h).
  int ladderHeight() const { return markerRect().y; }
  // Ladder: column `n`'s logical rectangle (1..ladderLayout's columns) in `half`; w or h 0 for no such column.
  ScreenRect ladderColumn(LadderHalf half, int n) const;
  LadderStage ladderStage() const { return ladderStage_; }
  // Ladder: the current stage's slots run so far (0..LADDER_STEPS; the base's: those that drove pixels).
  uint32_t ladderStageSlots() const { return ladderSlots_; }
  // Ladder: between two pulse slots of a stage (pulsing, at least one stage slot run and at least one to come): a pause
  // here puts a gap inside the stage's one-pulse-a-slot rhythm, after stage slot ladderStageSlots(), so columns n >
  // LADDER_STEPS - ladderStageSlots() carry it; a pause before the first pulse or after the last does not.
  bool ladderMidStage() const {
    return ladderPulsing() && ladderSlots_ > 0 && ladderSlots_ < static_cast<uint32_t>(LADDER_STEPS);
  }
  // Ladder: back to the base, no stage slots counted, no advance pending (setMode, a replay, the session cap's clean).
  void restartLadder();
  // The next slot delivers a ladder stage's pulses (Ladder, Running, stage 1 or 2): the task runs it with heldRhythm,
  // so each pulse is one scan.
  bool ladderPulsing() const {
    return mode_ == SweepMode::Ladder && phase_ == HoldPhase::Running &&
           (ladderStage_ == LadderStage::Ladder || ladderStage_ == LadderStage::Return);
  }
  // `preset` with every transition's hold bit set (HOLD_ALL): buildMasks then leaves the follow-up mask empty, and
  // runGraySlot
  // sends one scan a slot (one dose a slot for every pixel). Everything else is `preset`'s.
  static GrayPreset heldRhythm(const GrayPreset& preset);
  // The preset the next slot runs on (its runGraySlot): heldRhythm(preset) while ladderPulsing() (kept in the scene,
  // valid until the next call), `preset` itself otherwise (the base, the marker's slots, the other modes).
  const GrayPreset& slotPreset(const GrayPreset& preset);
  // Whether a run holds at its photo spots: with Hold on, and always in Ladder.
  bool holdsAtSpots() const { return holdOn_ || mode_ == SweepMode::Ladder; }

  static Shade bandShade(int band) { return static_cast<Shade>(band); }
  // The level of lane `lane` (0) in band `band`: (band + LANE_OFFSET + lane) mod 7.
  static Shade laneShade(int band, int lane) { return static_cast<Shade>((band + LANE_OFFSET + lane) % SHADE_COUNT); }
  // `shade` moved on by `offset` levels, 0, 1, .., 6, 0, ...
  static Shade shifted(Shade shade, uint8_t offset) {
    return static_cast<Shade>((static_cast<uint8_t>(shade) + offset) % SHADE_COUNT);
  }
  int bandHeight() const { return logicalH_ / BANDS; }
  int laneHeight() const { return bandHeight() / LANES; }
  // The logical top and height of lane `lane`'s rectangle in band `band`.
  int rectTop(int band, int lane) const { return band * bandHeight() + lane * laneHeight() + RECT_INSET; }
  int rectHeight() const { return laneHeight() - 2 * RECT_INSET; }

 private:
  // This scene's logical screen on the panel.
  LogicalScreen screen() const { return LogicalScreen{turn_, logicalW_, logicalH_, panelW_, panelH_}; }
  // Retargets the panel pixels of logical rectangle [x0, x1) x [y, y + h), clipped to the logical screen.
  void retargetLogical(GrayModel& model, int x0, int x1, int y, int h, Shade shade, const GrayPreset& preset) const;
  int bandOf(int ly) const;
  // Slow's crossing for `slotPeriodUs` and the dwell, and whether the cap cut it.
  void sizeSlow(uint32_t slotPeriodUs);
  // `shade` as the pattern shows it now (moved by the flips' shift).
  Shade shown(Shade shade) const { return shifted(shade, shift_); }
  // The next flip: the count and the shift move on.
  void flip();
  // The mode's decision for a Running slot (Static's settle and flip, Slow's measurement, the Ladder's stages, the
  // spots); the spot, if any.
  std::optional<HoldStart> patternStep(GrayModel& model, SceneStep& step, size_t driven, bool driveDone,
                                       uint32_t periodUs, uint32_t elapsedMs);
  // Ladder: a Running slot of the current stage; its spot, if any.
  std::optional<HoldStart> ladderStep(GrayModel& model, SceneStep& step, size_t driven);
  // Ladder: from the base to stage 1 or from stage 1 to stage 2 (no stage slot run yet, the longest column driven), or
  // from stage 2 to Done (`step.finished`).
  void advanceLadder(GrayModel& model, SceneStep& step);
  // Ladder: column `n` of both halves driven in `model` with n doses toward the current stage's shade (stage 1: the
  // opposite of its half's origin; stage 2: its origin); nothing for a column the layout has not.
  void driveLadderColumn(GrayModel& model, int n) const;
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
  // The marker's bars' panel pixels retargeted to markerShade().
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
  // Slow and Fast: the dwell Slow's crossing is sized for, whether the bands' paint has settled since setMode (Slow:
  // and sized the crossing), Slow's longest slot period seen while painting (then the one it was sized from), and the
  // sweep clock at which the first crossing started.
  uint32_t dwellSlots_ = 1;
  bool painted_ = false;
  uint32_t slowPeriodUs_ = 0;
  uint32_t crossStartMs_ = 0;
  // Fast: the slots that drove pixels while the bands painted.
  uint32_t paintSlots_ = 0;
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
  // Ladder: the stage, its slots so far (the base's: those that drove pixels; stage 1's and 2's: every slot, which
  // picks the next column to drive), and the next stage (or the end) waiting for the marker.
  LadderStage ladderStage_ = LadderStage::Base;
  uint32_t ladderSlots_ = 0;
  bool ladderPending_ = false;
  // slotPreset's held-rhythm copy.
  GrayPreset held_{};
};

}  // namespace GameDisplayLab
