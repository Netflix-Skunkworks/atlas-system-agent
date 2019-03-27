
#include <gtest/gtest.h>

#include "../lib/util.h"

std::vector<std::string> expected = {"foo:", "bar", "baz", "43.2", "x&z"};
TEST(Utils, SplitEmpty) {
  std::vector<std::string> res;
  atlasagent::split("", isspace, &res);
  ASSERT_EQ(0, res.size());
}

TEST(Utils, Split) {
  std::vector<std::string> res;
  atlasagent::split("foo: bar baz 43.2 x&z", isspace, &res);
  EXPECT_EQ(expected, res);
}

TEST(Utils, SplitEndsWithSpaces) {
  std::vector<std::string> res;
  atlasagent::split("foo: bar  \t baz 43.2 x&z\t\n", isspace, &res);
  EXPECT_EQ(expected, res);
}

TEST(Utils, SplitStartsEndsWithSpaces) {
  std::vector<std::string> res;
  atlasagent::split("  \t\nfoo: bar  \t baz 43.2 x&z\t\n", isspace, &res);
  EXPECT_EQ(expected, res);
}

TEST(Utils, ReadOutputString) {
  auto s = atlasagent::read_output_string("echo hello world");
  EXPECT_EQ(s, "hello world\n");
}

TEST(Utils, ReadOutputLines) {
  auto lines = atlasagent::read_output_lines("echo first line;echo second line;echo third line");
  std::vector<std::string> expected = {"first line\n", "second line\n", "third line\n"};
  EXPECT_EQ(lines, expected);
}

TEST(Utils, ReadOutputStringErr) {
  auto s = atlasagent::read_output_string("/bin/does-not-exist");
  EXPECT_TRUE(s.empty());
}

TEST(Utils, ReadOutputLinesErr) {
  auto v = atlasagent::read_output_string("/bin/does-not-exist");
  EXPECT_TRUE(v.empty());
}
