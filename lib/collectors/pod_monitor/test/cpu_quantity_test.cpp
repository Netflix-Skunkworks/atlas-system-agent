#include <lib/collectors/pod_monitor/src/util/cpu_quantity.h>

#include <gtest/gtest.h>

#include <string_view>

namespace
{

// ParseCpuQuantity handles CPU request strings obtained from the pod spec. The cgroup filesystem
// exposes cpu.max (the limit), not the declared request.
TEST(CpuQuantity, ParseCpuQuantityAcceptsMillicpuAndDecimalCores)
{
    // EXPECT_DOUBLE_EQ, not EXPECT_EQ: the millicpu path divides by 1000, and no test should rest
    // on whether 100.0/1000.0 is bit-identical to the literal 0.1.
    auto expect_cores = [](std::string_view input, double expected) {
        auto result = atlasagent::ParseCpuQuantity(input);
        ASSERT_TRUE(result.has_value()) << "failed to parse: " << input;
        EXPECT_DOUBLE_EQ(*result, expected) << "input: " << input;
    };

    expect_cores("500m", 0.5);
    expect_cores("100m", 0.1);
    expect_cores("1500m", 1.5);
    expect_cores("2", 2.0);
    expect_cores("0.5", 0.5);
}

TEST(CpuQuantity, ParseCpuQuantityRejectsMalformedInput)
{
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("").has_value());
    // A bare suffix leaves nothing to parse once stripped.
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("m").has_value());
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("abc").has_value());
    // A negative request is meaningless.
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("-1").has_value());
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("-500m").has_value());
    // std::from_chars deliberately does not accept a leading '+'.
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("+2").has_value());
}

// Why ParseCpuQuantity checks that from_chars consumed the WHOLE input: from_chars stops at the
// first character it cannot use instead of failing, so without that check these silently parse as
// 0.5 and 5. Nothing else in this repo does a full-consumption check, so it is easy to drop in a
// refactor; this test exists to make that break loudly.
TEST(CpuQuantity, ParseCpuQuantityRejectsTrailingJunkRatherThanTruncating)
{
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("0.5.1").has_value());
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("5x0m").has_value());
}

// Memory quantities use SI suffixes ParseCpuQuantity does not implement. It must reject them
// rather than return a plausible-looking number -- "128Mi" must not parse as 128.
TEST(CpuQuantity, ParseCpuQuantityRejectsMemoryStyleSuffixes)
{
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("128Mi").has_value());
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("1Gi").has_value());
    EXPECT_FALSE(atlasagent::ParseCpuQuantity("64Ki").has_value());
}

}  // namespace
