#include <gtest/gtest.h>

#include <string>

#include "GameIcons.h"

TEST(GameScriptTest, LinksLibraries) { EXPECT_EQ(std::string(GameIcons::libraryName()), "GameIcons"); }
