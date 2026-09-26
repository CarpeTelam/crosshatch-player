#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <cstring>
#include <lua.hpp>
#include <string>

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");

namespace {

// Owns a state with only the base and math libraries open; the vendored build has no linit.c.
class LuaOnHostTest : public ::testing::Test {
 protected:
  void SetUp() override {
    L = luaL_newstate();
    ASSERT_NE(L, nullptr);
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_settop(L, 0);
  }

  void TearDown() override {
    if (L != nullptr) {
      lua_close(L);
    }
  }

  // Loads source as a text chunk; on failure leaves the message in lastError.
  bool load(const char* source) {
    const int status = luaL_loadbufferx(L, source, std::strlen(source), "=test", "t");
    if (status != LUA_OK) {
      takeError();
      return false;
    }
    return true;
  }

  // Loads and runs source, leaving every result on the stack.
  bool run(const char* source) {
    if (!load(source)) {
      return false;
    }
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
      takeError();
      return false;
    }
    return true;
  }

  // Moves the error object on top of the stack into lastError; it need not be a string.
  void takeError() {
    const char* message = lua_tostring(L, -1);
    lastError = message != nullptr ? message : luaL_typename(L, -1);
    lua_pop(L, 1);
  }

  lua_State* L = nullptr;
  std::string lastError;
};

TEST_F(LuaOnHostTest, IsRelease551) {
  EXPECT_EQ(LUA_VERSION_RELEASE_NUM, 50501);
  ASSERT_TRUE(run("return _VERSION")) << lastError;
  EXPECT_STREQ(lua_tostring(L, 1), "Lua 5.5");
}

TEST_F(LuaOnHostTest, IntegersAre64Bit) {
  EXPECT_EQ(LUA_MAXINTEGER, LLONG_MAX);
  ASSERT_TRUE(
      run("local a = 1 << 62\n"
          "return math.maxinteger, math.mininteger, a + (a - 1), math.type(a),\n"
          "  math.maxinteger + 1 == math.mininteger, math.maxinteger == 9223372036854775807"))
      << lastError;
  ASSERT_EQ(lua_gettop(L), 6);
  EXPECT_EQ(lua_tointeger(L, 1), INT64_MAX);
  EXPECT_EQ(lua_tointeger(L, 2), INT64_MIN);
  EXPECT_TRUE(lua_isinteger(L, 3));
  EXPECT_EQ(lua_tointeger(L, 3), INT64_MAX);
  EXPECT_STREQ(lua_tostring(L, 4), "integer");
  EXPECT_TRUE(lua_toboolean(L, 5));
  EXPECT_TRUE(lua_toboolean(L, 6));
}

TEST_F(LuaOnHostTest, GlobalIsReserved) {
  EXPECT_FALSE(load("local global = 1")) << "'global' parsed as a name: LUA_COMPAT_GLOBAL is on";
  EXPECT_NE(lastError.find("<name> expected near 'global'"), std::string::npos) << lastError;
}

TEST_F(LuaOnHostTest, GlobalDeclarationWorks) {
  ASSERT_TRUE(run("global x; x = 5; return x")) << lastError;
  EXPECT_EQ(lua_tointeger(L, 1), 5);
}

TEST_F(LuaOnHostTest, ForControlVariableIsReadOnly) {
  EXPECT_FALSE(load("for i = 1, 2 do i = 3 end"));
  EXPECT_NE(lastError.find("attempt to assign to const variable 'i'"), std::string::npos) << lastError;
}

}  // namespace
