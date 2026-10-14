#pragma once

#include <cstdint>

#include "components/UITheme.h"
#include "components/UiAppHost.h"  // freeink::ui's styles

// The games screens' framed panel, as OptionPopup draws it, so it stands out over what is under it: white with black
// text and a black frame of the theme's popup thickness and corner radius, the same in every state. Writes `styles` in
// place, so no StyleSet copy lands on the render task's stack: it sets only the fields above in every state and
// `explicitlySet`, and leaves the rest (BoxStyle::corners) as they were, the default since no caller sets them. The
// match's menus and Result banner, GameConfirmDialog, and the title screen's nearby refusal draw it.
inline void setGamePanelStyle(freeink::ui::StyleSet& styles) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  fui::BoxStyle& panel = styles.normal;
  panel.background = fui::Paint::solid(fui::Color::White);
  panel.foreground = fui::Paint::solid(fui::Color::Black);
  panel.border = fui::Paint::solid(fui::Color::Black);
  panel.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  panel.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  styles.selected = panel;
  styles.focused = panel;
  styles.active = panel;
  styles.disabled = panel;
  styles.explicitlySet = true;
}
