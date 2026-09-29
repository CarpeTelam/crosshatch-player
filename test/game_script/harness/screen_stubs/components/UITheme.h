#pragma once

// The two things of the firmware's UITheme the games screens use: the popup metrics and
// the button hints, which are recorded (`hints`) instead of drawn. The real theme is a
// large, settings-driven family; the values here are the base theme's popup metrics.

#include <string>
#include <vector>

#include "GfxRenderer.h"

struct ThemeMetrics {
  int popupFrameThickness = 2;
  int popupCornerRadius = 0;
};

// One drawButtonHints call: the four labels, as passed.
struct ButtonHints {
  std::string btn1, btn2, btn3, btn4;
};

class ThemeDouble {
 public:
  void drawButtonHints(GfxRenderer&, const char* btn1, const char* btn2, const char* btn3, const char* btn4) {
    hints.push_back({btn1 ? btn1 : "", btn2 ? btn2 : "", btn3 ? btn3 : "", btn4 ? btn4 : ""});
  }
  std::vector<ButtonHints> hints;
};

class UITheme {
 public:
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  const ThemeMetrics& getMetrics() const { return metrics; }
  ThemeDouble& getTheme() { return theme; }

 private:
  ThemeMetrics metrics;
  ThemeDouble theme;
};

#define GUI UITheme::getInstance().getTheme()
