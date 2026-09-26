#include <gtest/gtest.h>

#include <string>

#include "GameIcons.h"
#include "GameScript.h"

TEST(GameScriptTest, LinksLibraries) {
  EXPECT_EQ(std::string(GameScript::libraryName()), "GameScript");
  EXPECT_EQ(std::string(GameIcons::libraryName()), "GameIcons");
}
