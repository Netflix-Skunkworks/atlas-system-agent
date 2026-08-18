#include <lib/collectors/pod_monitor/src/pod_monitor.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <gtest/gtest.h>

#include <utility>

class PodMonitorTest : public atlasagent::PodMonitor
{
   public:
    explicit PodMonitorTest(Registry* registry, std::string path_prefix = "/sys/fs/cgroup") noexcept
        : PodMonitor(registry, std::move(path_prefix))
    {
    }

    // Expose protected members and methods for testing
    using PodMonitor::MatchPodSliceName;
    using PodMonitor::NormalizePodUid;
    using PodMonitor::ScanPodSliceDirectory;
};

namespace
{

TEST(PodMonitor, NormalizePodUidUnderscoresToDash)
{
    auto result = PodMonitorTest::NormalizePodUid("11111111_1111_1111_1111_111111111111");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "11111111-1111-1111-1111-111111111111");
}

TEST(PodMonitor, NormalizePodUidDashesUnchanged)
{
    auto result = PodMonitorTest::NormalizePodUid("44444444-4444-4444-4444-444444444444");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "44444444-4444-4444-4444-444444444444");
}

TEST(PodMonitor, NormalizePodUidTooShortFails)
{
    auto result = PodMonitorTest::NormalizePodUid("11111111-1111-1111-1111-11111111");
    EXPECT_FALSE(result.has_value());
}

TEST(PodMonitor, NormalizePodUidNonHexCharacterFails)
{
    auto result = PodMonitorTest::NormalizePodUid("1111111g-1111-1111-1111-111111111111");
    EXPECT_FALSE(result.has_value());
}

TEST(PodMonitor, NormalizePodUidBadSeparatorFails)
{
    auto result = PodMonitorTest::NormalizePodUid("11111111*1111-1111-1111-111111111111");
    EXPECT_FALSE(result.has_value());
}

TEST(PodMonitor, MatchPodSliceNameMatches)
{
    auto result = PodMonitorTest::MatchPodSliceName("kubepods-pod11111111_1111_1111_1111_111111111111.slice",
                                                      "kubepods-pod", ".slice");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "11111111_1111_1111_1111_111111111111");
}

TEST(PodMonitor, MatchPodSliceNameWrongPrefixFails)
{
    auto result = PodMonitorTest::MatchPodSliceName("kubepods-burstable-pod22222222_2222_2222_2222_222222222222.slice",
                                                      "kubepods-pod", ".slice");
    EXPECT_FALSE(result.has_value());
}

TEST(PodMonitor, MatchPodSliceNameTooShortFails)
{
    auto result = PodMonitorTest::MatchPodSliceName("kubepods-pod.slice", "kubepods-pod", ".slice");
    EXPECT_FALSE(result.has_value());
}

TEST(PodMonitor, FindAllActivePodsSystemd)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd"};

    auto pods = podMonitor.FindAllActivePods();

    ASSERT_EQ(pods.size(), 3);

    EXPECT_EQ(pods.at("11111111-1111-1111-1111-111111111111"),
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/systemd/kubepods.slice/"
                  "kubepods-pod11111111_1111_1111_1111_111111111111.slice"));

    EXPECT_EQ(pods.at("22222222-2222-2222-2222-222222222222"),
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/systemd/kubepods.slice/kubepods-burstable.slice/"
                  "kubepods-burstable-pod22222222_2222_2222_2222_222222222222.slice"));

    EXPECT_EQ(pods.at("33333333-3333-3333-3333-333333333333"),
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/systemd/kubepods.slice/kubepods-besteffort.slice/"
                  "kubepods-besteffort-pod33333333_3333_3333_3333_333333333333.slice"));
}

TEST(PodMonitor, FindAllActivePodsCgroupfs)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/cgroupfs"};

    auto pods = podMonitor.FindAllActivePods();

    ASSERT_EQ(pods.size(), 3);

    EXPECT_EQ(pods.at("44444444-4444-4444-4444-444444444444"),
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/cgroupfs/kubepods/"
                  "pod44444444-4444-4444-4444-444444444444"));

    EXPECT_EQ(pods.at("55555555-5555-5555-5555-555555555555"),
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/cgroupfs/kubepods/burstable/"
                  "pod55555555-5555-5555-5555-555555555555"));

    EXPECT_EQ(pods.at("66666666-6666-6666-6666-666666666666"),
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/cgroupfs/kubepods/besteffort/"
                  "pod66666666-6666-6666-6666-666666666666"));
}

TEST(PodMonitor, FindAllActivePodsMissingRoot)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/does_not_exist"};

    auto pods = podMonitor.FindAllActivePods();

    EXPECT_TRUE(pods.empty());
}

}  // namespace
