#include <lib/collectors/pod_monitor/src/util/cgroup_pod_discovery.h>
#include "pod_monitor_test_support.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace
{

using namespace atlasagent::pod_monitor_test;

// Exposes CgroupPodDiscovery's protected parsing helpers.
class CgroupPodDiscoveryTest : public atlasagent::CgroupPodDiscovery
{
   public:
    using CgroupPodDiscovery::MatchPodSliceName;
    using CgroupPodDiscovery::NormalizePodUid;
};

TEST(CgroupPodDiscovery, NormalizePodUidUnderscoresToDash)
{
    auto result = CgroupPodDiscoveryTest::NormalizePodUid("11111111_1111_1111_1111_111111111111");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "11111111-1111-1111-1111-111111111111");
}

TEST(CgroupPodDiscovery, NormalizePodUidDashesUnchanged)
{
    auto result = CgroupPodDiscoveryTest::NormalizePodUid("44444444-4444-4444-4444-444444444444");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "44444444-4444-4444-4444-444444444444");
}

TEST(CgroupPodDiscovery, NormalizePodUidTooShortFails)
{
    auto result = CgroupPodDiscoveryTest::NormalizePodUid("11111111-1111-1111-1111-11111111");
    EXPECT_FALSE(result.has_value());
}

TEST(CgroupPodDiscovery, NormalizePodUidNonHexCharacterFails)
{
    auto result = CgroupPodDiscoveryTest::NormalizePodUid("1111111g-1111-1111-1111-111111111111");
    EXPECT_FALSE(result.has_value());
}

TEST(CgroupPodDiscovery, NormalizePodUidBadSeparatorFails)
{
    auto result = CgroupPodDiscoveryTest::NormalizePodUid("11111111*1111-1111-1111-111111111111");
    EXPECT_FALSE(result.has_value());
}

TEST(CgroupPodDiscovery, MatchPodSliceNameMatches)
{
    auto result = CgroupPodDiscoveryTest::MatchPodSliceName("kubepods-pod11111111_1111_1111_1111_111111111111.slice",
                                                              "kubepods-pod", ".slice");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "11111111_1111_1111_1111_111111111111");
}

TEST(CgroupPodDiscovery, MatchPodSliceNameWrongPrefixFails)
{
    auto result = CgroupPodDiscoveryTest::MatchPodSliceName(
        "kubepods-burstable-pod22222222_2222_2222_2222_222222222222.slice", "kubepods-pod", ".slice");
    EXPECT_FALSE(result.has_value());
}

TEST(CgroupPodDiscovery, MatchPodSliceNameTooShortFails)
{
    auto result = CgroupPodDiscoveryTest::MatchPodSliceName("kubepods-pod.slice", "kubepods-pod", ".slice");
    EXPECT_FALSE(result.has_value());
}

TEST(CgroupPodDiscovery, DiscoverReturnsCompleteSystemdSnapshot)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/systemd_pod_with_containers"};
    auto result = discovery.Discover();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& pod = result->at(kPod1Uid);
    EXPECT_EQ(pod.cgroup_path, Pod1SlicePath("systemd_pod_with_containers"));
    ASSERT_EQ(pod.containers.size(), 1);
    EXPECT_TRUE(pod.containers.contains(ContainerId('a')));
}

TEST(CgroupPodDiscovery, DiscoverReturnsPodsFromAllSystemdQosLocations)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/systemd"};
    auto result = discovery.Discover();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3);

    EXPECT_EQ(result->at("11111111-1111-1111-1111-111111111111").cgroup_path,
              std::filesystem::path(
                  std::string(kResources) + "/systemd/kubepods.slice/"
                  "kubepods-pod11111111_1111_1111_1111_111111111111.slice"));
    EXPECT_EQ(result->at("22222222-2222-2222-2222-222222222222").cgroup_path,
              std::filesystem::path(
                  std::string(kResources) + "/systemd/kubepods.slice/kubepods-burstable.slice/"
                  "kubepods-burstable-pod22222222_2222_2222_2222_222222222222.slice"));
    EXPECT_EQ(result->at("33333333-3333-3333-3333-333333333333").cgroup_path,
              std::filesystem::path(
                  std::string(kResources) + "/systemd/kubepods.slice/kubepods-besteffort.slice/"
                  "kubepods-besteffort-pod33333333_3333_3333_3333_333333333333.slice"));

    EXPECT_TRUE(result->at("11111111-1111-1111-1111-111111111111").containers.empty());
    EXPECT_TRUE(result->at("22222222-2222-2222-2222-222222222222").containers.empty());
    EXPECT_TRUE(result->at("33333333-3333-3333-3333-333333333333").containers.empty());
}

TEST(CgroupPodDiscovery, DiscoverReportsUnsupportedCgroupfsLayout)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/cgroupfs"};
    auto result = discovery.Discover();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::CgroupDiscoveryErrorKind::kUnsupportedCgroupfsLayout);
    EXPECT_EQ(result.error().path, std::filesystem::path(kResources) / "cgroupfs" / "kubepods");
    EXPECT_FALSE(result.error().cause);
}

TEST(CgroupPodDiscovery, DiscoverReportsMissingRoot)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/does_not_exist"};
    auto result = discovery.Discover();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::CgroupDiscoveryErrorKind::kMissingRoot);
    EXPECT_EQ(result.error().path, std::filesystem::path(kResources) / "does_not_exist");
    EXPECT_EQ(result.error().cause, std::make_error_code(std::errc::no_such_file_or_directory));
}

TEST(CgroupPodDiscovery, DiscoverReportsNonDirectoryRootWithContext)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/unreadable_root"};
    auto result = discovery.Discover();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::CgroupDiscoveryErrorKind::kUnreadableRoot);
    EXPECT_EQ(result.error().path, std::filesystem::path(kResources) / "unreadable_root" / "kubepods.slice");
    EXPECT_EQ(result.error().cause.default_error_condition(),
              std::make_error_condition(std::errc::not_a_directory));
}

TEST(CgroupPodDiscovery, DiscoverReportsUnreadableScanWithContext)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/unreadable_scan"};
    auto result = discovery.Discover();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::CgroupDiscoveryErrorKind::kUnreadableScan);
    EXPECT_EQ(result.error().path,
              std::filesystem::path(kResources) / "unreadable_scan" / "kubepods.slice" /
                  "kubepods-burstable.slice");
    EXPECT_EQ(result.error().cause.default_error_condition(),
              std::make_error_condition(std::errc::not_a_directory));
}

TEST(CgroupPodDiscovery, DiscoverTreatsEmptySystemdHierarchyAsSuccessfulSnapshot)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/systemd_empty"};
    auto result = discovery.Discover();

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(CgroupPodDiscovery, DiscoverDoesNotPreferEmptySystemdRootOverCgroupfs)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/mixed_layout_empty_systemd"};
    auto result = discovery.Discover();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::CgroupDiscoveryErrorKind::kUnsupportedCgroupfsLayout);
    EXPECT_EQ(result.error().path, std::filesystem::path(kResources) / "mixed_layout_empty_systemd" / "kubepods");
    EXPECT_FALSE(result.error().cause);
}

TEST(CgroupPodDiscovery, DiscoverReturnsAllContainerScopes)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/systemd_pod_with_two_containers"};
    auto result = discovery.Discover();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    const auto& pod = result->at(kPod1Uid);
    ASSERT_EQ(pod.containers.size(), 2);
    EXPECT_TRUE(pod.containers.contains(ContainerId('a')));
    EXPECT_TRUE(pod.containers.contains(ContainerId('b')));
    EXPECT_EQ(pod.containers.at(ContainerId('a')),
              std::filesystem::path(Pod1SlicePath("systemd_pod_with_two_containers")) /
                  ("cri-containerd-" + ContainerId('a') + ".scope"));
    EXPECT_EQ(pod.containers.at(ContainerId('b')),
              std::filesystem::path(Pod1SlicePath("systemd_pod_with_two_containers")) /
                  ("cri-containerd-" + ContainerId('b') + ".scope"));
}

}  // namespace
