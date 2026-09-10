#include <lib/util/src/util.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace
{

TEST(Utils, ReadLinesFields)
{
    auto lines = atlasagent::read_lines_fields("testdata/resources/proc", "stat");
    auto expected = std::vector<std::string>{"ctxt", "97596359"};
    EXPECT_EQ(lines.size(), 16);
    EXPECT_EQ(lines[10], expected);
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

// Sets an env var for a scope and restores the prior value (or unsets it). gtest runs every test
// in one process, so a leaked setenv would be visible to the tests that follow.
class ScopedEnv
{
   public:
    ScopedEnv(const char* name, const char* value) : name_(name)
    {
        if (const char* prior = std::getenv(name); prior != nullptr)
        {
            had_prior_ = true;
            prior_ = prior;
        }
        setenv(name, value, 1);
    }
    ~ScopedEnv()
    {
        if (had_prior_)
        {
            setenv(name_, prior_.c_str(), 1);
        }
        else
        {
            unsetenv(name_);
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

   private:
    const char* name_;
    bool had_prior_ = false;
    std::string prior_;
};

// A whitespace-only value is non-empty, so first_non_empty() hands it to trim(), which threw
// std::length_error on an all-whitespace string before its guard (mechanism in util.cpp). Uncaught,
// and get_common_tags() runs in main() before the Registry exists, so the agent aborted at startup.
TEST(Utils, GetCommonTagsSurvivesWhitespaceOnlyEnvVar)
{
    {
        ScopedEnv stack{"NETFLIX_STACK", " "};
        ASSERT_NO_THROW(atlasagent::get_common_tags());
        // Trimmed to empty, so the tag is omitted rather than published blank.
        EXPECT_FALSE(atlasagent::get_common_tags().contains("nf.stack"));
    }
    {
        ScopedEnv stack{"NETFLIX_STACK", "\t\n \v\f\r"};
        ASSERT_NO_THROW(atlasagent::get_common_tags());
        EXPECT_FALSE(atlasagent::get_common_tags().contains("nf.stack"));
    }
    {
        // strip_nimble_prefix reduces this to " " before trim() sees it, so the env var itself
        // need not look like whitespace to hit the same path.
        ScopedEnv app{"NETFLIX_APP", "nimble_ "};
        ASSERT_NO_THROW(atlasagent::get_common_tags());
        EXPECT_FALSE(atlasagent::get_common_tags().contains("nf.app"));
    }
}

// Stops the fix degenerating into a no-op: the test above would also pass if trim() returned {}
// unconditionally.
TEST(Utils, GetCommonTagsTrimsSurroundingWhitespace)
{
    ScopedEnv stack{"NETFLIX_STACK", "  prod\t"};
    auto tags = atlasagent::get_common_tags();
    ASSERT_TRUE(tags.contains("nf.stack"));
    EXPECT_EQ(tags.at("nf.stack"), "prod");
}
}  // namespace
