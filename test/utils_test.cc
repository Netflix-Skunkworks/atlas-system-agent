
#include <gtest/gtest.h>

#include "../lib/util.h"

std::vector<std::string> expected = {"foo:", "bar", "baz", "43.2", "x&z"};
TEST(Utils, SplitEmpty) {
  std::vector<std::string> res;
  atlasagent::split("", &res);
  ASSERT_EQ(0, res.size());
}

TEST(Utils, Split) {
  std::vector<std::string> res;
  atlasagent::split("foo: bar baz 43.2 x&z", &res);
  EXPECT_EQ(expected, res);
}

TEST(Utils, SplitEndsWithSpaces) {
  std::vector<std::string> res;
  atlasagent::split("foo: bar  \t baz 43.2 x&z\t\n", &res);
  EXPECT_EQ(expected, res);
}

TEST(Utils, SplitStartsEndsWithSpaces) {
  std::vector<std::string> res;
  atlasagent::split("  \t\nfoo: bar  \t baz 43.2 x&z\t\n", &res);
  EXPECT_EQ(expected, res);
}
