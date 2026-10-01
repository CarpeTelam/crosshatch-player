#pragma once

#include <Manifest.h>

#include <cstddef>
#include <cstdint>

#include "activities/UiListActivity.h"
#include "games/GameRegistry.h"

namespace GameCore {
struct Roster;
}

// A two-button question built into a FUI screen, Cancel first and focused when it opens: the title screen's New over a
// save and the launcher's remove ask with it. The screen keeps whether it is open and what it asks about, routes touch
// (routeTouch) before readButtons, and answers each Answer itself.
struct GameConfirmDialog {
  // What an input did: nothing, moved the focus (draw it), or answered Cancel or the action.
  enum class Answer : uint8_t { None, Repaint, Cancel, Confirm };

  // The focused button: 0 Cancel, 1 the action. Set to 0 when the question opens, so a stray Confirm cancels.
  uint8_t focus = 0;

  // The physical buttons, while the question is open: Back cancels, Up/Left/Previous and Down/Right/Next move the
  // focus, and Confirm answers the focused button.
  Answer readButtons(const MappedInputManager& input);
  // A tap on one of the buttons the last build registered (event.value: 0 Cancel, 1 the action); it takes the focus.
  Answer answerTap(const freeink::ui::ActionEvent& event);
  // Builds the question, a framed panel centred in the body, into `screen`; its buttons fire `action` on a tap.
  void build(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, freeink::ui::ActionId action, const char* title,
             const char* headline, const char* message, const char* actionLabel);

 private:
  // Filled by each build and read only inside it (about 700 B: too big for the render task's stack). Its options
  // pointer is build's own array, so nothing reads it after build returns.
  freeink::ui::OptionDialogProps props;
};

// A game's title screen (Home → Games → a game; spine AD-22), headed by the game's name. Every launcher row of a game
// this host can start pushes it, so Back pops to the launcher as it was. Its rows, top to bottom: Continue ("Go on with
// the saved game") when GameSaveStore::peek(game, pkgHash, host) finds a save it can start, or one it could not check
// (Unreadable: a card or heap fault, so the screen never offers only New over what may be a good save); then one New
// row for each mode Manifest::check leaves for this host, in the order solo, pass and play, play nearby. The save is
// peeked once, when the screen opens, never while a row is drawn. The selection starts on the first row, so a Confirm
// with nothing moved resumes when there is a save.
//
// A tap or Confirm on a row replaces this screen with the game's match. Continue resumes the save (Start::Resume): the
// match plays the roster the save records (GameMatchActivity::seedResume), and one it cannot read stops in its error
// view with the file kept. The roster Continue passes is played only when the match finds no usable save (a new match):
// pass with the fewest seats for a game that starts pass, else solo (startResume has the no-pass-seats cases). A New
// row with no save starts that mode at once: solo; pass as an open pass match with the fewest seats it can have
// (passSeats; the seat choice is deferred, deferred-work.md ## e5-inception); nearby solo until epic-play-nearby. A New
// row over a save asks first, Cancel focused ("Start a new game?" / "This replaces the saved game."), since the new
// match replaces the one save the game has; Cancel or Back keeps it, and New game starts the match.
class GameModeActivity final : public UiListActivity {
 public:
  // The activity's name, which ActivityManager::goHome maps to Home's Games row (ledger row 5): one constant, so the
  // mapping and the constructor cannot disagree.
  static constexpr const char* NAME = "GameMode";

  // The Manifest::Mode bits there are, and so the most New rows; with the Continue row, the most rows.
  static constexpr size_t MAX_MODES = 3;
  static constexpr size_t MAX_ROWS = 1 + MAX_MODES;

  // `game` is a launcher entry this host can start (check.ok()); its manifest, check modes, and package hash are
  // copied.
  GameModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameRegistry::Entry& game);

 private:
  // What a row starts: a New match in one of the three modes (in the cpp's mode table's order), or the save.
  enum class RowKind : uint8_t { Solo, Pass, Nearby, Continue };

  int listCount() const override { return static_cast<int>(rowCount); }
  const char* headerTitle() const override { return manifest.name; }
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;

  // The rows for the save peek() found and the modes check left.
  void buildRows();
  // Starts a New match in the mode of row kind `kind` (not Continue).
  void startNew(RowKind kind);
  // Starts the match from the save.
  void startResume();
  // Replaces this screen with the match; repaints this screen when the activity cannot be allocated.
  void startMatch(const GameCore::Roster& roster, bool resume);

  // The New-over-a-save confirmation: opened on a New row, answered by Cancel (Back), or New game.
  void openConfirm(int row);
  void closeConfirm();
  void confirmNew();
  bool handleConfirmInput();
  void answerConfirm(GameConfirmDialog::Answer answer);
  void buildConfirmDialog(UiScreen& screen);
  static void onConfirmChoice(const freeink::ui::ActionEvent& event, void* user);

  static constexpr freeink::ui::ActionId ACTION_CONFIRM_CHOICE = ACTION_USER;

  GameCore::Manifest manifest;
  uint8_t modes = 0;  // Manifest::Mode bits: CheckResult::modes of the game's check on this host
  uint8_t pkgHash[GamePkg::HASH_BYTES] = {};
  // Whether peek() found a save (Valid or Unreadable) when the screen opened.
  bool hasSave = false;
  // The rows, built in onEnter, and what each one starts.
  RowKind rowKind[MAX_ROWS] = {};
  freeink::ui::ListItem rowItems[MAX_ROWS] = {};
  size_t rowCount = 0;
  // The New row the open confirmation asks about (-1: none), and the question (its focus: 0 Cancel, 1 New game).
  int8_t confirmRow = -1;
  GameConfirmDialog confirm;
};
