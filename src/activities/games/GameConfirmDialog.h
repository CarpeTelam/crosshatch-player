#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <atomic>
#include <cstdint>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHost.h"

// A two-button question built into a FUI screen, Cancel first and focused when it opens: the title screen's New over a
// save and the launcher's remove ask with it. The screen keeps whether it is open and what it asks about, routes touch
// (routeTouch) before readButtons, and answers each Answer itself. Its own header, so the launcher does not depend on
// the title screen to ask.
struct GameConfirmDialog {
  // What an input did: nothing, moved the focus (draw it), or answered Cancel or the action.
  enum class Answer : uint8_t { None, Repaint, Cancel, Confirm };

  // The focused button: 0 Cancel, 1 the action. Set to 0 when the question opens, so a stray Confirm cancels. Written
  // by the loop task (the buttons, a tap) and read by the render task (build), so atomic.
  std::atomic<uint8_t> focus{0};

  // The physical buttons, while the question is open: Back cancels, Up/Left/Previous and Down/Right/Next move the
  // focus, and Confirm answers the focused button.
  Answer readButtons(const MappedInputManager& input) {
    using Button = MappedInputManager::Button;
    if (input.wasReleased(Button::Back)) return Answer::Cancel;
    if (input.wasReleased(Button::Up) || input.wasReleased(Button::Left) || input.wasReleased(Button::NavPrevious)) {
      focus.store(0);
      return Answer::Repaint;
    }
    if (input.wasReleased(Button::Down) || input.wasReleased(Button::Right) || input.wasReleased(Button::NavNext)) {
      focus.store(1);
      return Answer::Repaint;
    }
    if (input.wasReleased(Button::Confirm)) return focus.load() == 1 ? Answer::Confirm : Answer::Cancel;
    return Answer::None;
  }

  // A tap on one of the buttons the last build registered (event.value: 0 Cancel, 1 the action); it takes the focus.
  Answer answerTap(const freeink::ui::ActionEvent& event) {
    const uint8_t tapped = event.value == 1 ? 1 : 0;
    focus.store(tapped);
    return tapped == 1 ? Answer::Confirm : Answer::Cancel;
  }

  // Builds the question, a framed panel centred in the body, into `screen`; its buttons fire `action` on a tap.
  void build(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const freeink::ui::ActionId action,
             const char* title, const char* headline, const char* message, const char* actionLabel) {
    namespace fui = freeink::ui;
    // Read once: the loop task may move it while this draws.
    const uint8_t focused = focus.load();
    fui::DialogOption options[2];
    options[0].label = tr(STR_CANCEL);
    options[1].label = actionLabel;
    for (int i = 0; i < 2; ++i) {
      options[i].action = action;
      options[i].value = static_cast<int16_t>(i);
      options[i].state = focused == i ? fui::StateFocused : fui::StateNormal;
    }
    props.title = title;
    props.headline = headline;
    props.message = message;
    props.options = options;
    props.optionCount = 2;
    props.verticalOptions = true;
    props.titleText = screen.theme().smallText;
    props.titleText.bold = true;
    props.headlineText = screen.theme().bodyText;
    props.headlineText.maxLines = 2;  // a long name wraps; the dialog grows to fit
    props.messageText = screen.theme().smallText;
    props.messageText.maxLines = 2;
    props.buttonText = screen.theme().smallText;
    props.inputMask = fui::InputTouch;  // physical buttons stay in readButtons()
    // A framed panel, as OptionPopup draws it.
    const auto& metrics = UITheme::getInstance().getMetrics();
    props.styles = fui::defaultPopupStyles();
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
    props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
    props.styles.selected = props.styles.normal;
    props.styles.focused = props.styles.normal;
    props.styles.active = props.styles.normal;
    props.styles.disabled = props.styles.normal;

    const fui::Rect body = screen.body();
    int16_t width = static_cast<int16_t>(renderer.getScreenWidth() * 3 / 4);
    if (width > body.width) width = body.width;
    const int16_t height = fui::optionDialogHeight(screen.target(), props, width);
    fui::optionDialog(screen.frame(), fui::centeredRect(body, fui::Size{width, height}), props);
  }

 private:
  // Filled by each build and read only inside it (about 700 B: too big for the render task's stack). Its options
  // pointer is build's own array, so nothing reads it after build returns.
  freeink::ui::OptionDialogProps props;
};
