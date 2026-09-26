#include <gtest/gtest.h>

#include <string>

#include "GameCore.h"

TEST(GameCoreTest, LinksLibrary) { EXPECT_EQ(std::string(GameCore::libraryName()), "GameCore"); }
