#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "StoreSlot.h"

using GameScript::StoreSlot;

namespace {

TEST(StoreSlotTest, StartsEmptyAndClean) {
  std::vector<uint8_t> storage(16);
  StoreSlot slot(storage.data(), storage.size());
  std::vector<uint8_t> out(16);
  EXPECT_EQ(slot.read(out), 0u);
  EXPECT_FALSE(slot.dirty());
  EXPECT_EQ(slot.takeIfDirty(out), 0u);
}

TEST(StoreSlotTest, DirtyOnlyWhenTheBytesChange) {
  std::vector<uint8_t> storage(16);
  StoreSlot slot(storage.data(), storage.size());
  const std::vector<uint8_t> a = {6, 0, 0};
  const std::vector<uint8_t> b = {6, 1, 3, 2, 0};
  std::vector<uint8_t> out(16);

  ASSERT_TRUE(slot.post(a));
  EXPECT_TRUE(slot.dirty());
  EXPECT_EQ(slot.takeIfDirty(out), a.size());
  EXPECT_FALSE(slot.dirty());
  ASSERT_TRUE(slot.post(a));
  EXPECT_FALSE(slot.dirty());
  EXPECT_EQ(slot.takeIfDirty(out), 0u);

  ASSERT_TRUE(slot.post(b));
  EXPECT_TRUE(slot.dirty());
  ASSERT_EQ(slot.read(out), b.size());
  EXPECT_EQ(std::vector<uint8_t>(out.begin(), out.begin() + b.size()), b);
}

TEST(StoreSlotTest, RestoreIsClean) {
  std::vector<uint8_t> storage(16);
  StoreSlot slot(storage.data(), storage.size());
  const std::vector<uint8_t> saved = {6, 0, 0};
  std::vector<uint8_t> out(16);
  ASSERT_TRUE(slot.restore(saved));
  EXPECT_FALSE(slot.dirty());
  EXPECT_EQ(slot.read(out), saved.size());
  ASSERT_TRUE(slot.post(saved));
  EXPECT_FALSE(slot.dirty());
}

TEST(StoreSlotTest, RefusesWhatDoesNotFit) {
  std::vector<uint8_t> storage(4);
  StoreSlot slot(storage.data(), storage.size());
  const std::vector<uint8_t> big(5, 1);
  const std::vector<uint8_t> small = {6, 0, 0};
  EXPECT_FALSE(slot.post(big));
  EXPECT_FALSE(slot.post({}));
  EXPECT_FALSE(slot.dirty());
  EXPECT_FALSE(slot.restore(big));
  ASSERT_TRUE(slot.post(small));
  std::vector<uint8_t> tiny(2);
  EXPECT_EQ(slot.read(tiny), 0u);
  EXPECT_EQ(slot.takeIfDirty(tiny), 0u);
  EXPECT_TRUE(slot.dirty());
}

}  // namespace
