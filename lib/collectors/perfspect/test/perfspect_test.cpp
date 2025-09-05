

#include <lib/collectors/perfspect/src/perfspect.h>
#include <gtest/gtest.h>

TEST(Perfspect, ParseProductName)
{
    // Test valid Intel instance names
    auto result = ParseProductName("m7i.large");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, '7');
    EXPECT_EQ(result->second, 'i');

    result = ParseProductName("c8i.xlarge");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, '8');
    EXPECT_EQ(result->second, 'i');

    // Test valid AMD instance names
    result = ParseProductName("m7a.large");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, '7');
    EXPECT_EQ(result->second, 'a');

    result = ParseProductName("c9a.xlarge");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, '9');
    EXPECT_EQ(result->second, 'a');

    // Test with additional characters after processor type
    result = ParseProductName("m7i.metal-24xl");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, '7');
    EXPECT_EQ(result->second, 'i');

    // Test edge cases - too short
    result = ParseProductName("m7");
    EXPECT_FALSE(result.has_value());

    result = ParseProductName("ab");
    EXPECT_FALSE(result.has_value());

    result = ParseProductName("");
    EXPECT_FALSE(result.has_value());

    // Test invalid processor types
    result = ParseProductName("m7x.large");
    EXPECT_FALSE(result.has_value());

    result = ParseProductName("m7b.large");
    EXPECT_FALSE(result.has_value());

    // Test missing dot (processor symbol after dot boundary)
    result = ParseProductName("m7ilarge");
    EXPECT_FALSE(result.has_value());

    // Test processor symbol before position 2
    result = ParseProductName("ai7.large");
    EXPECT_FALSE(result.has_value());

    // Test multiple valid processor symbols (should find first one)
    result = ParseProductName("m7ia.large");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, '7');
    EXPECT_EQ(result->second, 'i');  // Should find 'i' first since it appears at position 2

    // Test with dot but no processor symbol before it
    result = ParseProductName("m7.large");
    EXPECT_FALSE(result.has_value());
}