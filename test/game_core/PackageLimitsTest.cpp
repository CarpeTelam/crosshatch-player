#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "GameImages.h"
#include "Manifest.h"
#include "PackageLimits.h"
#include "StreamingJsonParser.h"

// The package limits in C++ (PackageLimits.h, GameImages.h) against entry 2's package_vectors.json, which
// scripts/pack_game.py and its test read too: each limit is one number in all three places, with a case at
// the limit and one over. The installer's own at-limit and one-over packages are in the harness suite
// PackageHardeningTest, built from the same file.
//
// The file is read by looking inside one object at a time (objectOf), so a key that is missing from its
// object is an empty result and a failed test, never the next object's number.

namespace {

std::string vectorsText() {
  std::ifstream file(PACKAGE_VECTORS_PATH, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// The position just after `"key":` in `text`, or npos.
size_t after(const std::string& text, const std::string& key) {
  const size_t at = text.find("\"" + key + "\"");
  if (at == std::string::npos) return at;
  const size_t colon = text.find(':', at);
  return colon == std::string::npos ? colon : colon + 1;
}

// The `{ ... }` that `"key":` opens in `text`, braces included, or "" when the value is not an object.
std::string objectOf(const std::string& text, const std::string& key) {
  size_t at = after(text, key);
  if (at == std::string::npos) return "";
  while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
  if (at >= text.size() || text[at] != '{') return "";
  int depth = 0;
  for (size_t end = at; end < text.size(); ++end) {
    depth += text[end] == '{' ? 1 : text[end] == '}' ? -1 : 0;
    if (depth == 0) return text.substr(at, end - at + 1);
  }
  return "";
}

// The integer that follows `"key":` in `text`, or -1.
int64_t numberIn(const std::string& text, const std::string& key) {
  size_t at = after(text, key);
  if (at == std::string::npos) return -1;
  while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
  if (at >= text.size() || !std::isdigit(static_cast<unsigned char>(text[at]))) return -1;
  return std::stoll(text.substr(at));
}

// The string that follows `"key":` in `text`, its \" and \\ escapes decoded (the only ones the vectors use), or "".
std::string stringIn(const std::string& text, const std::string& key) {
  size_t at = after(text, key);
  if (at == std::string::npos) return "";
  while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
  if (at >= text.size() || text[at] != '"') return "";
  std::string out;
  for (++at; at < text.size() && text[at] != '"'; ++at) {
    if (text[at] == '\\' && at + 1 < text.size()) ++at;
    out += text[at];
  }
  return out;
}

// The [width, height] pairs of the `"images": [[w, h], ...]` in `text`.
std::vector<std::pair<int64_t, int64_t>> pairsIn(const std::string& text) {
  std::vector<std::pair<int64_t, int64_t>> pairs;
  const size_t at = after(text, "images");
  if (at == std::string::npos) return pairs;
  const size_t end = text.find("]]", at);
  // The outer bracket, then each inner one up to the last "]]".
  for (size_t open = text.find('[', text.find('[', at) + 1); open != std::string::npos && open <= end;
       open = text.find('[', open + 1)) {
    const size_t comma = text.find(',', open);
    if (comma == std::string::npos || comma > end) break;
    pairs.emplace_back(std::stoll(text.substr(open + 1)), std::stoll(text.substr(comma + 1)));
  }
  return pairs;
}

// One limit's numbers: its `limit`, the case `at` it, and the case `over` it.
void expectLimit(const std::string& limits, const std::string& name, const size_t constant) {
  const std::string limit = objectOf(limits, name);
  ASSERT_FALSE(limit.empty()) << name;
  EXPECT_EQ(numberIn(limit, "limit"), static_cast<int64_t>(constant)) << name;
  EXPECT_EQ(numberIn(limit, "at"), numberIn(limit, "limit")) << name << ": the at case is the limit";
  EXPECT_EQ(numberIn(limit, "over"), numberIn(limit, "limit") + 1) << name << ": the over case is one more";
}

}  // namespace

TEST(PackageLimitsTest, VectorFileLoads) {
  const std::string text = vectorsText();
  ASSERT_FALSE(text.empty()) << "cannot read " << PACKAGE_VECTORS_PATH;
  EXPECT_FALSE(objectOf(text, "limits").empty());
  EXPECT_FALSE(objectOf(text, "image_formula").empty());
}

TEST(PackageLimitsTest, ConstantsMatchTheVectors) {
  const std::string limits = objectOf(vectorsText(), "limits");
  ASSERT_FALSE(limits.empty());
  expectLimit(limits, "package_bytes", GameCore::PACKAGE_BYTES);
  expectLimit(limits, "member_bytes", GameCore::MEMBER_BYTES);
  expectLimit(limits, "members", GameCore::PACKAGE_MEMBERS);
  expectLimit(limits, "lua_sources_bytes", GameCore::LUA_SOURCES_BYTES);
  expectLimit(limits, "images", GameCore::MAX_IMAGES);
  expectLimit(limits, "member_name_chars", GameCore::MEMBER_STEM_BYTES);
  expectLimit(limits, "image_width", GameCore::IMAGE_MAX_WIDTH);
  expectLimit(limits, "image_height", GameCore::IMAGE_MAX_HEIGHT);
}

// The manifest nesting limit is StreamingJsonParser::MAX_NESTING, which scripts/pack_game.py copies as MAX_NESTING: the
// vectors' "nesting" case holds a whole manifest nested to the limit and one nested one deeper, and pack_game_test.py
// checks the same two strings against the packer.
TEST(PackageLimitsTest, TheManifestNestingLimitIsTheParsersAndTheVectorsAtAndOverCases) {
  const std::string nesting = objectOf(vectorsText(), "nesting");
  ASSERT_FALSE(nesting.empty());
  EXPECT_EQ(numberIn(nesting, "limit"), static_cast<int64_t>(StreamingJsonParser::MAX_NESTING));
  const std::string at = stringIn(nesting, "at");
  const std::string over = stringIn(nesting, "over");
  ASSERT_FALSE(at.empty());
  ASSERT_FALSE(over.empty());
  // Depth by counting the open brackets of the text (it has no brackets in a string).
  const auto deepest = [](const std::string& text) {
    int64_t depth = 0;
    int64_t most = 0;
    for (const char c : text) {
      depth += (c == '{' || c == '[') ? 1 : (c == '}' || c == ']') ? -1 : 0;
      most = std::max(most, depth);
    }
    return most;
  };
  EXPECT_EQ(deepest(at), numberIn(nesting, "limit"));
  EXPECT_EQ(deepest(over), numberIn(nesting, "limit") + 1);
  // A string over the parser's token buffer would be dropped without a callback, and the case would prove nothing.
  EXPECT_LT(at.size(), StreamingJsonParser::TOKEN_BUF_SIZE);
  EXPECT_LT(over.size(), StreamingJsonParser::TOKEN_BUF_SIZE);

  GameCore::Manifest manifest;
  EXPECT_EQ(GameCore::Manifest::parse(at, manifest), GameCore::ManifestError::None);
  EXPECT_STREQ(manifest.id, "demo");
  EXPECT_EQ(GameCore::Manifest::parse(over, manifest), GameCore::ManifestError::Syntax);
}

TEST(PackageLimitsTest, TheImageFormulaIsTheLoadersAndTheImageBytesLimitMatches) {
  const std::string text = vectorsText();
  const std::string formula = objectOf(text, "image_formula");
  ASSERT_FALSE(formula.empty());
  const int64_t header = numberIn(formula, "header_bytes");
  const int64_t pixels = numberIn(formula, "row_pixels_per_word");
  const int64_t word = numberIn(formula, "word_bytes");
  EXPECT_EQ(header, static_cast<int64_t>(GameCore::IMAGE_HEADER_BYTES));
  EXPECT_EQ(pixels, 32);
  EXPECT_EQ(word, 4);

  const std::string bytes = objectOf(objectOf(text, "limits"), "images_bytes");
  ASSERT_FALSE(bytes.empty());
  EXPECT_EQ(numberIn(bytes, "limit"), static_cast<int64_t>(GameCore::IMAGES_BYTES));
  // Each case lists its images: the formula's sum is its stated bytes, exactly at the limit and over it.
  int64_t sums[2] = {0, 0};
  const char* cases[2] = {"at", "over"};
  for (int i = 0; i < 2; ++i) {
    const std::string one = objectOf(bytes, cases[i]);
    ASSERT_FALSE(one.empty()) << cases[i];
    const auto images = pairsIn(one);
    ASSERT_FALSE(images.empty()) << cases[i];
    for (const auto& [width, height] : images) sums[i] += header + (width + pixels - 1) / pixels * word * height;
    EXPECT_EQ(sums[i], numberIn(one, "bytes")) << cases[i];
  }
  EXPECT_EQ(sums[0], static_cast<int64_t>(GameCore::IMAGES_BYTES));
  EXPECT_GT(sums[1], static_cast<int64_t>(GameCore::IMAGES_BYTES));
}

TEST(PackageLimitsTest, ANameHasRoomForItsStemAnExtensionAndTheTerminator) {
  EXPECT_EQ(GameCore::MEMBER_NAME_BYTES, GameCore::MEMBER_STEM_BYTES + std::string(".lua").size() + 1);
  EXPECT_GT(GameCore::MEMBER_NAME_BYTES, std::string("manifest.json").size());
  EXPECT_LE(GameCore::MAX_IMAGES, GameCore::PACKAGE_MEMBERS);
}
