
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

TEST(Utils, CanExecute) {
  EXPECT_TRUE(atlasagent::can_execute("echo"));
  EXPECT_FALSE(atlasagent::can_execute("program-does-not-exist"));
}

TEST(Utils, CanExecuteFullPath) {
  EXPECT_TRUE(atlasagent::can_execute("/bin/sh"));
  EXPECT_FALSE(atlasagent::can_execute("/bin/pr-does-not-exist"));
}

TEST(Utils, ParseTags) {
  auto tags = atlasagent::parse_tags("key=value,key2=value2");
  EXPECT_EQ(tags.size(), 2);
  EXPECT_EQ(tags.at("key"), "value");
  EXPECT_EQ(tags.at("key2"), "value2");
}

TEST(Utils, ParseTagsEmpty) {
  auto tags = atlasagent::parse_tags("");
  EXPECT_EQ(tags.size(), 0);

  auto some_invalid = atlasagent::parse_tags("key=val, key2=, =");
  EXPECT_EQ(some_invalid.size(), 1);
  EXPECT_EQ(some_invalid.at("key"), "val");
}
