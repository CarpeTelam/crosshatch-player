#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

#include "activities/games/GameMatchActivity.h"

// GameMatchActivity copies no constant from another source any more (the hand-off's time guard, which did, is gone:
// the banner, "I'm ready" and Confirm are plain tap targets, 2026-10-03). The suite keeps its reader of constexpr
// definitions in the sources and the check that the two sources it knew are still found, for the next copy that needs
// a drift guard (deferred-work.md ## 5.12).

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
         " = defined more than once); GameMatchActivity.h's copy must follow the source";
}

TEST(CopiedConstantsTest, TheSourcesAreRead) {
  EXPECT_FALSE(readFile(INPUT_MANAGER_HEADER_PATH).empty()) << "cannot read " << INPUT_MANAGER_HEADER_PATH;
  EXPECT_FALSE(readFile(MAIN_CPP_PATH).empty()) << "cannot read " << MAIN_CPP_PATH;
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
