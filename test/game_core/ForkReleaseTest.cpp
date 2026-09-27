#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "ForkRelease.h"
#include "StreamingJsonParser.h"

// The expected values live in fork_version_vectors.json, which the release
// workflow reads too; this suite only loads and applies them.

namespace {

using Record = std::map<std::string, std::string>;

struct Vectors {
  bool loaded = false;
  std::map<std::string, std::string> scalars;
  std::map<std::string, std::vector<Record>> sections;

  const std::vector<Record>& section(const std::string& name) const {
    static const std::vector<Record> empty;
    const auto found = sections.find(name);
    return found == sections.end() ? empty : found->second;
  }
};

// Collects the file's shape: top-level scalars, and top-level arrays of flat
// objects whose values are kept as text (booleans as "true"/"false").
class VectorCollector {
 public:
  explicit VectorCollector(Vectors& out) : out(out) {}

  // The parser flags malformed input but not input that stops early, so a
  // truncated file must also be caught by its unclosed top-level object.
  bool complete() const { return started && depth == 0; }

  JsonCallbacks callbacks() {
    return JsonCallbacks{this,   onKey,         onString,    onNumber,     onBool,
                         onNull, onObjectStart, onObjectEnd, onArrayStart, onArrayEnd};
  }

 private:
  static VectorCollector& self(void* ctx) { return *static_cast<VectorCollector*>(ctx); }

  static void onKey(void* ctx, const char* key, size_t len) {
    auto& c = self(ctx);
    if (c.depth == 1) c.section.assign(key, len);
    if (c.depth == 3) c.field.assign(key, len);
  }
  static void onString(void* ctx, const char* value, size_t len) { self(ctx).value(std::string(value, len)); }
  static void onNumber(void* ctx, const char* value, size_t len) { self(ctx).value(std::string(value, len)); }
  static void onBool(void* ctx, bool value) { self(ctx).value(value ? "true" : "false"); }
  static void onNull(void* ctx) { self(ctx).value("null"); }
  static void onObjectStart(void* ctx) {
    auto& c = self(ctx);
    c.started = true;
    if (c.depth == 2) c.out.sections[c.section].emplace_back();
    ++c.depth;
  }
  static void onObjectEnd(void* ctx) { --self(ctx).depth; }
  static void onArrayStart(void* ctx) {
    auto& c = self(ctx);
    if (c.depth == 1) c.out.sections[c.section];
    ++c.depth;
  }
  static void onArrayEnd(void* ctx) { --self(ctx).depth; }

  void value(const std::string& text) {
    if (depth == 1) out.scalars[section] = text;
    if (depth == 3) out.sections[section].back()[field] = text;
  }

  Vectors& out;
  int depth = 0;
  bool started = false;
  std::string section;
  std::string field;
};

Vectors loadVectors() {
  Vectors vectors;
  std::ifstream file(FORK_VERSION_VECTORS_PATH, std::ios::binary);
  if (!file) return vectors;
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  VectorCollector collector(vectors);
  StreamingJsonParser parser(collector.callbacks());
  parser.feed(text.data(), text.size());
  vectors.loaded = !parser.hasError() && collector.complete();
  return vectors;
}

const Vectors& vectors() {
  static const Vectors loaded = loadVectors();
  return loaded;
}

uint32_t toBuildNumber(const std::string& text) { return static_cast<uint32_t>(std::stoul(text)); }

std::string assetName(const std::string& tag, const std::string& board,
                      size_t bufferSize = ForkRelease::ASSET_NAME_CAPACITY) {
  std::vector<char> buffer(bufferSize, 'x');
  const bool ok = ForkRelease::formatAssetName(buffer.data(), buffer.size(), tag, board);
  const std::string written(buffer.data());
  EXPECT_EQ(ok, !written.empty()) << tag << " / " << board;
  return written;
}

}  // namespace

TEST(ForkReleaseTest, VectorFileLoads) {
  const Vectors& v = vectors();
  ASSERT_TRUE(v.loaded) << "cannot read " << FORK_VERSION_VECTORS_PATH;
  for (const char* name :
       {"tag_grammar", "max_tag_length", "asset_name_capacity", "release_url", "upstream_release_url_fragment"}) {
    EXPECT_FALSE(v.scalars.count(name) == 0 || v.scalars.at(name).empty()) << name;
  }
  for (const char* name : {"valid_tags", "invalid_tags", "running_versions", "newer", "asset_names"}) {
    EXPECT_FALSE(v.section(name).empty()) << name;
  }
}

TEST(ForkReleaseTest, ConstantsMatchVectors) {
  const Vectors& v = vectors();
  ASSERT_TRUE(v.loaded);
  EXPECT_EQ(v.scalars.at("release_url"), ForkRelease::LATEST_RELEASE_URL);
  EXPECT_EQ(std::stoul(v.scalars.at("max_tag_length")), ForkRelease::MAX_TAG_LEN);
  EXPECT_EQ(std::stoul(v.scalars.at("asset_name_capacity")), ForkRelease::ASSET_NAME_CAPACITY);
  EXPECT_EQ(std::string(ForkRelease::LATEST_RELEASE_URL).find(v.scalars.at("upstream_release_url_fragment")),
            std::string::npos);
}

TEST(ForkReleaseTest, ValidTags) {
  for (const Record& r : vectors().section("valid_tags")) {
    EXPECT_EQ(ForkRelease::tagBuildNumber(r.at("tag")), toBuildNumber(r.at("n"))) << r.at("tag");
  }
}

TEST(ForkReleaseTest, InvalidTags) {
  for (const Record& r : vectors().section("invalid_tags")) {
    EXPECT_EQ(ForkRelease::tagBuildNumber(r.at("tag")), 0u) << r.at("tag") << " (" << r.at("why") << ")";
  }
}

// The workflow checks tags with the grammar string, the firmware with the
// parser; both must accept exactly the same tags and read the same N.
TEST(ForkReleaseTest, GrammarAgreesWithParser) {
  const Vectors& v = vectors();
  ASSERT_TRUE(v.loaded);
  const std::regex grammar(v.scalars.at("tag_grammar"), std::regex::extended);
  const size_t maxLength = std::stoul(v.scalars.at("max_tag_length"));
  std::vector<Record> tags = v.section("valid_tags");
  const std::vector<Record>& invalid = v.section("invalid_tags");
  tags.insert(tags.end(), invalid.begin(), invalid.end());
  for (const Record& r : tags) {
    const std::string& tag = r.at("tag");
    std::smatch match;
    const bool matches = std::regex_match(tag, match, grammar) && tag.size() <= maxLength;
    const uint32_t grammarN = matches ? toBuildNumber(match[4].str()) : 0;
    EXPECT_EQ(grammarN, ForkRelease::tagBuildNumber(tag)) << tag;
  }
}

TEST(ForkReleaseTest, RunningVersions) {
  for (const Record& r : vectors().section("running_versions")) {
    EXPECT_EQ(ForkRelease::runningBuildNumber(r.at("version")), toBuildNumber(r.at("n")))
        << r.at("version") << " (" << r.at("why") << ")";
  }
}

TEST(ForkReleaseTest, Newer) {
  for (const Record& r : vectors().section("newer")) {
    EXPECT_EQ(ForkRelease::isNewer(r.at("latest"), r.at("running")), r.at("newer") == "true")
        << r.at("latest") << " vs " << r.at("running") << " (" << r.at("why") << ")";
  }
}

TEST(ForkReleaseTest, AssetNames) {
  for (const Record& r : vectors().section("asset_names")) {
    EXPECT_EQ(assetName(r.at("tag"), r.at("board")), r.at("asset")) << r.at("tag") << " / " << r.at("board");
  }
}

TEST(ForkReleaseTest, AssetNameNeedsRoomForTerminator) {
  const std::string expected = "crosspoint-1.6.5-ch.7-x4pro.bin";
  EXPECT_EQ(assetName("1.6.5-ch.7", "x4pro", expected.size() + 1), expected);
  EXPECT_EQ(assetName("1.6.5-ch.7", "x4pro", expected.size()), "");
  EXPECT_FALSE(ForkRelease::formatAssetName(nullptr, 0, "1.6.5-ch.7", "x4pro"));
}

// The longest tag and board name must fit the update path's asset-name buffers.
TEST(ForkReleaseTest, LongestAssetNameFitsUpdateBuffer) {
  const std::string tag = "1.1." + std::string(ForkRelease::MAX_TAG_LEN - std::strlen("1.1.-ch.1"), '1') + "-ch.1";
  ASSERT_EQ(tag.size(), ForkRelease::MAX_TAG_LEN);
  ASSERT_NE(ForkRelease::tagBuildNumber(tag), 0u);
  EXPECT_NE(assetName(tag, "sticky"), "");
}

// The vectors hold a name that fills the capacity exactly and one a byte over.
TEST(ForkReleaseTest, AssetVectorsReachTheCapacity) {
  bool atCapacity = false;
  bool oneOver = false;
  for (const Record& r : vectors().section("asset_names")) {
    const size_t length =
        std::strlen("crosspoint-") + r.at("tag").size() + 1 + r.at("board").size() + std::strlen(".bin");
    const bool formable = ForkRelease::tagBuildNumber(r.at("tag")) != 0 && !r.at("board").empty();
    if (formable && length == ForkRelease::ASSET_NAME_CAPACITY - 1) atCapacity = !r.at("asset").empty();
    if (formable && length == ForkRelease::ASSET_NAME_CAPACITY) oneOver = r.at("asset").empty();
  }
  EXPECT_TRUE(atCapacity) << "no vector names an asset of ASSET_NAME_CAPACITY - 1 characters";
  EXPECT_TRUE(oneOver) << "no vector refuses an asset of ASSET_NAME_CAPACITY characters";
}

static_assert(ForkRelease::tagBuildNumber("1.6.5-ch.7") == 7, "usable in constant expressions");
static_assert(ForkRelease::isNewer("1.6.5-ch.8", "1.6.5-ch.7-rc+abc1234"), "usable in constant expressions");
