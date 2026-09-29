#pragma once

// The firmware's UITheme and the theme behind GUI, for screens built on the host. The data is the
// real header's: ThemeMetrics, Rect, UIIcon come from components/themes/BaseTheme.h itself, and
// the metrics are BaseMetrics::values (the base theme's, not touch-adjusted as the real
// getMetrics() adjusts them), so a screen lays out with the real numbers. What is a double is
// what draws: `GUI` is a ThemeDouble whose every draw call (the members of BaseTheme a screen
// calls: drawHeader, drawSubHeader, drawButtonHints, drawSideButtonHints, drawButtonMenu,
// drawRecentBookCover, drawPopup, fillPopupProgress, drawTextField, getMenuRowHeight,
// homeCoverThumbHeight, showsFileIcons) is recorded in `calls`, with its text, and paints nothing.
// UITheme's statics that read the settings or the cover cache (getCoverThumbPath, getFileIcon,
// the cover-grid hooks) are recorded stand-ins: getCoverThumbPath returns the path with the
// height spliced in, as the real one names a thumb; supportsCoverGrid is false and
// `coverGridHome` turns hasCoverGridHome on. getScreenSafeArea models a portrait screen only.
// The real UITheme.cpp and BaseTheme.cpp are not built.

#include <EpdFontFamily.h>

#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "components/themes/BaseTheme.h"

class CoverGridHomeUi;

// One drawButtonHints call: the four labels, as passed.
struct ButtonHints {
  std::string btn1, btn2, btn3, btn4;
};

class ThemeDouble {
 public:
  // One recorded draw call: which, and the text it was given (a title, a message, a label).
  struct Call {
    std::string what;
    std::string text;
  };

  void drawButtonHints(GfxRenderer&, const char* btn1, const char* btn2, const char* btn3, const char* btn4) {
    hints.push_back({btn1 ? btn1 : "", btn2 ? btn2 : "", btn3 ? btn3 : "", btn4 ? btn4 : ""});
    record("drawButtonHints", btn1);
  }
  void drawSideButtonHints(const GfxRenderer&, const char* topBtn, const char* bottomBtn) {
    record("drawSideButtonHints", std::string(topBtn ? topBtn : "") + "|" + (bottomBtn ? bottomBtn : ""));
  }
  int getMenuRowHeight(const GfxRenderer&) const { return BaseMetrics::values.menuRowHeight; }
  void drawHeader(const GfxRenderer&, Rect, const char* title, const char* subtitle = nullptr, bool backButton = true) {
    record(backButton ? "drawHeader" : "drawHeader(no back)",
           std::string(title ? title : "") + (subtitle ? "|" : "") + (subtitle ? subtitle : ""));
  }
  void drawSubHeader(const GfxRenderer&, Rect, const char* label, const char* rightLabel = nullptr) {
    record("drawSubHeader", std::string(label ? label : "") + (rightLabel ? "|" : "") + (rightLabel ? rightLabel : ""));
  }
  void drawRecentBookCover(GfxRenderer&, Rect, const std::vector<RecentBook>&, const int, bool&, bool&, bool&,
                           std::function<bool()>) {
    record("drawRecentBookCover", "");
  }
  // Calls the label and icon callbacks for each row, as the real one does, and records the labels.
  void drawButtonMenu(GfxRenderer&, Rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) {
    std::string labels;
    for (int i = 0; i < buttonCount; ++i) {
      labels += (i ? "|" : "") + (buttonLabel ? buttonLabel(i) : std::string());
      if (rowIcon) (void)rowIcon(i);
    }
    menuSelected = selectedIndex;
    record("drawButtonMenu", labels);
  }
  Rect drawPopup(const GfxRenderer&, const char* message) {
    record("drawPopup", message);
    return Rect{};
  }
  void fillPopupProgress(const GfxRenderer&, const Rect&, const int progress) {
    record("fillPopupProgress", std::to_string(progress));
  }
  void drawTextField(const GfxRenderer&, Rect, const int, bool = false, int = 0, int = 0) {
    record("drawTextField", "");
  }
  bool showsFileIcons() const { return false; }
  int homeCoverThumbHeight(const GfxRenderer&) const { return 0; }

  bool drew(const std::string& what, const std::string& text = "") const {
    for (const Call& call : calls)
      if (call.what == what && (text.empty() || call.text == text)) return true;
    return false;
  }
  void reset() {
    hints.clear();
    calls.clear();
    menuSelected = -1;
  }

  std::vector<ButtonHints> hints;
  std::vector<Call> calls;
  int menuSelected = -1;  // drawButtonMenu's last selectedIndex

 private:
  void record(const char* what, const char* text) { calls.push_back({what, text ? text : ""}); }
  void record(const char* what, const std::string& text) { calls.push_back({what, text}); }
};

class UITheme {
 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }

  const ThemeMetrics& getMetrics() const { return metrics; }
  ThemeDouble& getTheme() { return theme; }
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false, bool = false) {
    Rect area{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()};
    if (hasFrontButtonHints) area.height -= metrics.buttonHintsHeight;
    return area;
  }
  static void drawCenteredText(const GfxRenderer&, Rect, int, int, const char* text, bool = true,
                               EpdFontFamily::Style = EpdFontFamily::REGULAR) {
    getInstance().theme.calls.push_back({"drawCenteredText", text ? text : ""});
  }
  static void drawCenteredWrappedText(const GfxRenderer&, Rect, int, const char* text, int, bool = true,
                                      EpdFontFamily::Style = EpdFontFamily::REGULAR,
                                      TextVerticalAlignment = TextVerticalAlignment::CENTER) {
    getInstance().theme.calls.push_back({"drawCenteredWrappedText", text ? text : ""});
  }
  static bool supportsCoverGrid() { return false; }
  static bool hasCoverGridHome() { return getInstance().coverGridHome; }
  static void drawCoverGridHome(CoverGridHomeUi&) { getInstance().theme.calls.push_back({"drawCoverGridHome", ""}); }
  void reload() {}
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
    const size_t dot = coverBmpPath.rfind(".bmp");
    if (dot != std::string::npos) coverBmpPath.replace(dot, 4, "_" + std::to_string(coverHeight) + ".bmp");
    return coverBmpPath;
  }
  static UIIcon getFileIcon(const std::string&) { return UIIcon::File; }
  static int getStatusBarHeight() { return 0; }
  static int getProgressBarHeight() { return BaseMetrics::values.progressBarHeight; }

  // A test's switch: hasCoverGridHome().
  bool coverGridHome = false;

 private:
  ThemeMetrics metrics = BaseMetrics::values;
  ThemeDouble theme;
};

#define GUI UITheme::getInstance().getTheme()
