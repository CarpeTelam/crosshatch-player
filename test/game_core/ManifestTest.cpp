#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

#include "Manifest.h"

using GameCore::Manifest;
using GameCore::ManifestError;
using GameCore::ManifestReader;

namespace {

constexpr const char* FULL = R"({
  "id": "tic-tac-toe",
  "name": "Tic-Tac-Toe",
  "version": "1.0.0",
  "api": 1,
  "seats": { "min": 2, "max": 2 },
  "modes": ["pass", "nearby"],
  "hidden": true,
  "icon": "mark_x"
})";

// A minimal valid manifest with one extra member spliced in before the closing brace.
std::string withExtra(const std::string& member) {
  return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, "modes": ["solo"])" +
         (member.empty() ? std::string() : ", " + member) + "}";
}

ManifestError parse(const std::string& json, Manifest& out) { return Manifest::parse(json, out); }

ManifestError parse(const std::string& json) {
  Manifest out;
  return parse(json, out);
}

TEST(ManifestTest, ParsesEveryKey) {
  Manifest m;
  ASSERT_EQ(parse(FULL, m), ManifestError::None);
  EXPECT_STREQ(m.id, "tic-tac-toe");
  EXPECT_STREQ(m.name, "Tic-Tac-Toe");
  EXPECT_STREQ(m.version, "1.0.0");
  EXPECT_EQ(m.api, 1);
  EXPECT_EQ(m.seatsMin, 2);
  EXPECT_EQ(m.seatsMax, 2);
  EXPECT_FALSE(m.hasMode(Manifest::MODE_SOLO));
  EXPECT_TRUE(m.hasMode(Manifest::MODE_PASS));
  EXPECT_TRUE(m.hasMode(Manifest::MODE_NEARBY));
  EXPECT_TRUE(m.hidden);
  EXPECT_STREQ(m.icon, "mark_x");
}

TEST(ManifestTest, OptionalKeysDefault) {
  Manifest m;
  ASSERT_EQ(parse(withExtra(""), m), ManifestError::None);
  EXPECT_FALSE(m.hidden);
  EXPECT_STREQ(m.icon, "");
  EXPECT_STREQ(m.version, "");
  EXPECT_TRUE(m.hasMode(Manifest::MODE_SOLO));
}

TEST(ManifestTest, IgnoresUnknownKeysOfAnyType) {
  Manifest m;
  const std::string json = withExtra(
      R"("author": "x", "year": 2026, "beta": false, "none": null, "extra": {"id": "zzz", "api": [1, {"a": 2}]},)"
      R"( "tags": ["a", ["b"], {"c": null}])");
  ASSERT_EQ(parse(json, m), ManifestError::None);
  EXPECT_STREQ(m.id, "g");
  EXPECT_EQ(m.api, 1);
  // Unknown keys inside seats are ignored too.
  EXPECT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1,)"
                  R"( "seats": {"min": 1, "note": {"x": [1]}, "max": 2, "why": "x"}, "modes": ["solo"]})",
                  m),
            ManifestError::None);
  EXPECT_EQ(m.seatsMax, 2);
}

TEST(ManifestTest, IgnoresUnknownStringsLongerThanTheParserBuffer) {
  // The JSON parser drops string tokens over 511 bytes without a callback.
  const std::string longText(600, 'd');
  EXPECT_EQ(parse(withExtra(R"("description": ")" + longText + R"(", "x": 1)")), ManifestError::None);
  EXPECT_EQ(parse(withExtra(R"("description": ")" + longText + R"(")")), ManifestError::None);  // last member
  EXPECT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "note": ")" + longText +
                  R"(", "max": 1}, "modes": ["solo"]})"),
            ManifestError::None);
  EXPECT_NE(parse(withExtra(R"("icon": ")" + longText + R"(")")), ManifestError::None);  // a known key still fails
}

TEST(ManifestTest, IgnoresUnknownKeysLongerThanTheParserBuffer) {
  // The JSON parser drops a key over 511 bytes, so its value arrives with no key.
  const std::string longKey(600, 'k');
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": "v", "x": 1)")), ManifestError::None);
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": 5)")), ManifestError::None);
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": {"a": [1, 2]})")), ManifestError::None);
  EXPECT_EQ(parse(withExtra("\"" + longKey + R"(": null, "hidden": true)")), ManifestError::None);
  Manifest m;
  ASSERT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, ")" + longKey +
                      R"(": 3, "max": 2}, "modes": ["solo"]})",
                  m),
            ManifestError::None);
  EXPECT_EQ(m.seatsMax, 2);
  EXPECT_EQ(parse(R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, ")" + longKey +
                  R"(": {"q": 1}}, "modes": ["solo"]})"),
            ManifestError::BadSeats);  // still needs max
}

TEST(ManifestTest, ReadsInChunks) {
  auto reader = std::make_unique<ManifestReader>();
  const std::string json = FULL;
  reader->begin();
  for (size_t i = 0; i < json.size(); i += 3) {
    reader->feed(json.data() + i, std::min<size_t>(3, json.size() - i));
  }
  Manifest m;
  ASSERT_EQ(reader->finish(m), ManifestError::None);
  EXPECT_STREQ(m.icon, "mark_x");
  // The same reader is reusable after begin().
  reader->begin();
  reader->feed("[]", 2);
  EXPECT_EQ(reader->finish(m), ManifestError::Syntax);
}

TEST(ManifestTest, RejectsBadIds) {
  for (const char* id : {"", "-abc", "Abc", "a_b", "a.b", "a b", "abcdefghijklmnopqrstuvwxyz0123456"}) {
    const std::string json =
        R"({"id": ")" + std::string(id) +
        R"(", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, "modes": ["solo"]})";
    EXPECT_EQ(parse(json), ManifestError::BadId) << id;
  }
  const std::string ok = R"({"id": "0abcdefghijklmnopqrstuvwxyz-123", "name": "G", "version": "", "api": 1,)"
                         R"( "seats": {"min": 1, "max": 1}, "modes": ["solo"]})";
  EXPECT_EQ(parse(ok), ManifestError::None);  // 32 characters, digit first, dash inside
}

TEST(ManifestTest, RejectsMissingRequiredKeys) {
  for (const char* key : {"id", "name", "version", "api", "seats", "modes"}) {
    std::string json = FULL;
    const size_t at = json.find(std::string("\"") + key + "\"");
    ASSERT_NE(at, std::string::npos);
    json.replace(at + 1, 1, "x");  // renames the key, which is then ignored
    EXPECT_EQ(parse(json), ManifestError::MissingKey) << key;
  }
}

TEST(ManifestTest, RejectsWrongTypes) {
  EXPECT_EQ(parse(R"({"id": 5})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"name": ["G"]})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"api": "1"})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"seats": [1, 2]})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"seats": {"min": "1", "max": 1}})"), ManifestError::WrongType);
  EXPECT_EQ(parse(R"({"modes": "solo"})"), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("hidden": "yes")")), ManifestError::WrongType);
  EXPECT_EQ(parse(withExtra(R"("icon": null)")), ManifestError::WrongType);
}

TEST(ManifestTest, RejectsBadApi) {
  for (const char* api : {"0", "-1", "1.5", "1e2", "01", "1234567890"}) {
    std::string json = withExtra("");
    json.replace(json.find("\"api\": 1"), 8, std::string("\"api\": ") + api);
    EXPECT_EQ(parse(json), ManifestError::BadApi) << api;
  }
}

TEST(ManifestTest, RejectsBadSeats) {
  const auto withSeats = [](const std::string& seats) {
    return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": )" + seats + R"(, "modes": ["solo"]})";
  };
  EXPECT_EQ(parse(withSeats(R"({"min": 2, "max": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": 0, "max": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": -1, "max": 1})")), ManifestError::BadSeats);
  EXPECT_EQ(parse(withSeats(R"({"min": 1, "max": 1, "max": 2})")), ManifestError::DuplicateKey);
  EXPECT_EQ(parse(withSeats(R"({"min": 1, "max": 8})")), ManifestError::None);
}

TEST(ManifestTest, RejectsBadModes) {
  const auto withModes = [](const std::string& modes) {
    return R"({"id": "g", "name": "G", "version": "", "api": 1, "seats": {"min": 1, "max": 1}, "modes": )" + modes +
           "}";
  };
  EXPECT_EQ(parse(withModes("[]")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", "online"])")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", 1])")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", ["pass"]])")), ManifestError::BadModes);
  EXPECT_EQ(parse(withModes(R"(["solo", "solo"])")), ManifestError::None);
}

TEST(ManifestTest, RejectsBadTextFields) {
  EXPECT_EQ(parse(R"({"name": ""})"), ManifestError::BadName);
  EXPECT_EQ(parse(R"({"name": ")" + std::string(Manifest::MAX_NAME_BYTES + 1, 'n') + R"("})"), ManifestError::BadName);
  std::string longest = withExtra("");
  longest.replace(longest.find("\"G\""), 3, "\"" + std::string(Manifest::MAX_NAME_BYTES, 'n') + "\"");
  Manifest m;
  ASSERT_EQ(parse(longest, m), ManifestError::None);
  EXPECT_EQ(std::string(m.name).size(), Manifest::MAX_NAME_BYTES);
  EXPECT_EQ(parse(R"({"version": ")" + std::string(Manifest::MAX_VERSION_BYTES + 1, 'v') + R"("})"),
            ManifestError::BadVersion);
  EXPECT_EQ(parse(withExtra(R"("icon": "Mark")")), ManifestError::BadIcon);
  EXPECT_EQ(parse(withExtra(R"("icon": "")")), ManifestError::BadIcon);
  EXPECT_EQ(parse(withExtra(R"("icon": ")" + std::string(Manifest::MAX_ICON_BYTES + 1, 'i') + R"(")")),
            ManifestError::BadIcon);
}

TEST(ManifestTest, RejectsDuplicateKnownKeys) {
  EXPECT_EQ(parse(withExtra(R"("id": "h")")), ManifestError::DuplicateKey);
  EXPECT_EQ(parse(withExtra(R"("hidden": true, "hidden": false)")), ManifestError::DuplicateKey);
  EXPECT_EQ(parse(withExtra(R"("x": 1, "x": 2)")), ManifestError::None);  // unknown keys may repeat
}

TEST(ManifestTest, RejectsMalformedJson) {
  const std::string ok = withExtra("");
  EXPECT_EQ(parse(""), ManifestError::Syntax);
  EXPECT_EQ(parse("[]"), ManifestError::Syntax);
  EXPECT_EQ(parse("\"id\""), ManifestError::Syntax);
  EXPECT_EQ(parse("42"), ManifestError::Syntax);
  EXPECT_EQ(parse(ok.substr(0, ok.size() - 1)), ManifestError::Syntax);  // truncated
  EXPECT_EQ(parse(ok + "{}"), ManifestError::Syntax);                    // a second value
  EXPECT_EQ(parse(R"({"id" "g"})"), ManifestError::Syntax);              // key without a value
  EXPECT_EQ(parse(R"({"id": })"), ManifestError::Syntax);
  EXPECT_EQ(parse(R"({"extra": [1})"), ManifestError::Syntax);  // mismatched brackets
  EXPECT_EQ(parse(R"({"hidden": tru})"), ManifestError::Syntax);
}

}  // namespace
