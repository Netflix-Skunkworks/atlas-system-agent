#include <lib/util/src/util.h>
#include <gtest/gtest.h>

namespace
{

TEST(Utils, ReadLinesFields)
{
    auto lines = atlasagent::read_lines_fields("testdata/resources/proc", "stat");
    auto expected = std::vector<std::string>{"ctxt", "290595647"};
    EXPECT_EQ(lines.size(), 10);
    EXPECT_EQ(lines[4], expected);
}

TEST(Utils, ReadNumVectorFromFile)
{
    // numbers are read successfully
    auto vector = atlasagent::read_num_vector_from_file("testdata/resources", "num_vector.1");
    auto expected = std::vector<int64_t>{100000, 100000};
    EXPECT_EQ(vector.size(), 2);
    EXPECT_EQ(vector, expected);

    // text values become zeroes
    vector = atlasagent::read_num_vector_from_file("testdata/resources", "num_vector.2");
    expected = std::vector<int64_t>{0, 100000};
    EXPECT_EQ(vector.size(), 2);
    EXPECT_EQ(vector, expected);
}

TEST(Utils, ReadOutputString)
{
    auto s = atlasagent::read_output_string("echo hello world");
    EXPECT_EQ(s, "hello world\n");
}

TEST(Utils, ReadOutputLines)
{
    auto lines = atlasagent::read_output_lines("echo first line;echo second line;echo third line");
    std::vector<std::string> expected = {"first line", "second line", "third line"};
    EXPECT_EQ(lines, expected);
}

TEST(Utils, ReadOutputTimeoutNoInput)
{
    auto lines = atlasagent::read_output_lines("sleep 4; echo hi", 10);
    EXPECT_TRUE(lines.empty());
}

TEST(Utils, ReadOutputTimeoutAfterInput)
{
    auto lines = atlasagent::read_output_lines("echo foo; sleep 1; echo bar", 10);
    EXPECT_TRUE(lines.empty());
}

TEST(Utils, ReadOutputStringErr)
{
    auto s = atlasagent::read_output_string("/bin/does-not-exist");
    EXPECT_TRUE(s.empty());
}

TEST(Utils, ReadOutputLinesErr)
{
    auto v = atlasagent::read_output_string("/bin/does-not-exist");
    EXPECT_TRUE(v.empty());
}

TEST(Utils, CanExecute)
{
    EXPECT_TRUE(atlasagent::can_execute("echo"));
    EXPECT_FALSE(atlasagent::can_execute("program-does-not-exist"));
}

TEST(Utils, CanExecuteFullPath)
{
    EXPECT_TRUE(atlasagent::can_execute("/bin/sh"));
    EXPECT_FALSE(atlasagent::can_execute("/bin/pr-does-not-exist"));
}

TEST(Utils, ParseTags)
{
    auto tags = atlasagent::parse_tags("key=value,key2=value2");
    EXPECT_EQ(tags.size(), 2);
    EXPECT_EQ(tags.at("key"), "value");
    EXPECT_EQ(tags.at("key2"), "value2");
}

TEST(Utils, ParseTagsEmpty)
{
    auto tags = atlasagent::parse_tags("");
    EXPECT_EQ(tags.size(), 0);

    auto some_invalid = atlasagent::parse_tags("key=val, key2=, =");
    EXPECT_EQ(some_invalid.size(), 1);
    EXPECT_EQ(some_invalid.at("key"), "val");
}
}  // namespace
