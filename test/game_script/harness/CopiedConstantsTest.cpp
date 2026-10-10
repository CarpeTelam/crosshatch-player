#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

#include "activities/games/GameMatchActivity.h"

// The screen input double (screen_stubs/MappedInputManager.h) copies three device constants: the 90 ms a touch must be
// held before wasScreenTouchDown reports it (src/MappedInputManager.cpp, file-local TOUCH_DOWN_SELECT_DELAY_MS), the
// 500 ms a contact must be held for a long press (freeink-sdk's InputManager.h, private TOUCH_LONG_PRESS_MS) and the 28
// px a contact may move and still be a tap candidate (the same header's private TOUCH_TAP_SLOP_PX). These tests read
// each device source as text and fail when any value, or its definition, moves away from the double's, so a retune of
// the device cannot leave the host tests passing against the old number (epic-install-and-launcher retro AI-4, R13).
// The reader of constexpr definitions is below.

namespace {

std::string readFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// `text` with its // and /* */ comments blanked (string and character literals kept, escapes included), so a
// commented-out definition or comparison cannot satisfy a check below.
std::string withoutComments(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const char c = text[i];
    if (c == '"' || c == '\'') {
      const size_t start = i++;
      while (i < text.size() && text[i] != c) i += text[i] == '\\' ? 2 : 1;
      i = std::min(i + 1, text.size());
      out.append(text, start, i - start);
    } else if (text.compare(i, 2, "//") == 0) {
      while (i < text.size() && text[i] != '\n') ++i;
    } else if (text.compare(i, 2, "/*") == 0) {
      const size_t end = text.find("*/", i + 2);
      i = end == std::string::npos ? text.size() : end + 2;
      out += ' ';
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

// The source as read, comments blanked; empty when it cannot be read.
std::string readCode(const char* path) { return withoutComments(readFile(path)); }

constexpr long long NOT_FOUND = -1;  // no `constexpr ... NAME = <digits>;` (renamed, removed, or not a plain number)
constexpr long long DEFINED_TWICE = -2;

// The value of the one `constexpr ... NAME = <digits>;` in `text` (comments already blanked): NOT_FOUND when there is
// none, DEFINED_TWICE when there are several, so a renamed, removed, or doubled constant fails the comparison rather
// than reading another one.
long long constexprValue(const std::string& text, const std::string& name) {
  const std::regex definition("constexpr[^=;]*\\b" + name + "\\s*=\\s*([0-9]+)\\s*;");
  long long value = NOT_FOUND;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), definition); it != std::sregex_iterator(); ++it) {
    if (value != NOT_FOUND) return DEFINED_TWICE;
    value = std::stoll((*it)[1].str());
  }
  return value;
}

// What a failed comparison means, for its message.
std::string explain(const std::string& name, const char* path) {
  return name + " in " + path + " (the source's value: " + std::to_string(NOT_FOUND) +
         " = no plain `constexpr ... = <digits>;` definition found, " + std::to_string(DEFINED_TWICE) +
         " = defined more than once); the double's copy in screen_stubs/MappedInputManager.h must follow the source";
}

TEST(CopiedConstantsTest, TheSourcesAreRead) {
  EXPECT_FALSE(readFile(INPUT_MANAGER_HEADER_PATH).empty()) << "cannot read " << INPUT_MANAGER_HEADER_PATH;
  EXPECT_FALSE(readFile(MAPPED_INPUT_MANAGER_CPP_PATH).empty()) << "cannot read " << MAPPED_INPUT_MANAGER_CPP_PATH;
}

// The double's touch-down delay is the device's: wasScreenTouchDown reports a still finger only once it has been down
// this long (MappedInputManager.cpp, where the constant sits in an anonymous namespace).
TEST(CopiedConstantsTest, TheDoublesTouchDownDelayIsTheDevicesSelectDelay) {
  EXPECT_EQ(constexprValue(readCode(MAPPED_INPUT_MANAGER_CPP_PATH), "TOUCH_DOWN_SELECT_DELAY_MS"),
            static_cast<long long>(MappedInputManager::TOUCH_DOWN_SELECT_DELAY_MS))
      << explain("TOUCH_DOWN_SELECT_DELAY_MS", MAPPED_INPUT_MANAGER_CPP_PATH);
}

// The double's long press is the device's: InputManager reports a stationary contact as a long press once it has been
// held this long (a private constant of InputManager.h).
TEST(CopiedConstantsTest, TheDoublesLongPressIsTheDevicesTouchLongPress) {
  EXPECT_EQ(constexprValue(readCode(INPUT_MANAGER_HEADER_PATH), "TOUCH_LONG_PRESS_MS"),
            static_cast<long long>(MappedInputManager::TOUCH_LONG_PRESS_MS))
      << explain("TOUCH_LONG_PRESS_MS", INPUT_MANAGER_HEADER_PATH);
}

// The double's tap slop is the device's: a contact that moves more than this in either axis from its touch-down point
// is no tap candidate (wasScreenTouchDown reports nothing more for it).
TEST(CopiedConstantsTest, TheDoublesTapSlopIsTheDevicesTouchTapSlop) {
  EXPECT_EQ(constexprValue(readCode(INPUT_MANAGER_HEADER_PATH), "TOUCH_TAP_SLOP_PX"),
            static_cast<long long>(MappedInputManager::TOUCH_TAP_SLOP_PX))
      << explain("TOUCH_TAP_SLOP_PX", INPUT_MANAGER_HEADER_PATH);
}

// The reader itself: it finds the one definition, and refuses none or two.
TEST(CopiedConstantsTest, TheReaderTakesOnlyASingleDefinition) {
  EXPECT_EQ(constexprValue("static constexpr unsigned long A_MS = 700;", "A_MS"), 700);
  EXPECT_EQ(constexprValue("constexpr unsigned long A_MS = 1;  // and B_MS = 2", "A_MS"), 1);
  EXPECT_EQ(constexprValue("constexpr unsigned long AA_MS = 5;", "A_MS"), -1);
  EXPECT_EQ(constexprValue("x = A_MS;", "A_MS"), -1);
  EXPECT_EQ(constexprValue("constexpr int A_MS = 1;\nconstexpr int A_MS = 2;", "A_MS"), -2);
  // A commented-out definition does not count once comments are blanked; one in a string literal is kept as text.
  EXPECT_EQ(constexprValue(withoutComments("// constexpr int A_MS = 1;\nconstexpr int A_MS = 2;"), "A_MS"), 2);
  EXPECT_EQ(constexprValue(withoutComments("/* constexpr int A_MS = 1; */ int x;"), "A_MS"), -1);
  EXPECT_EQ(withoutComments("a(\"http://x\"); // b\nc /* d */ e"), "a(\"http://x\"); \nc   e");
}

}  // namespace
