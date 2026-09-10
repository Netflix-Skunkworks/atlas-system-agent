#include "pod_monitor_test_support.h"

#include <lib/collectors/pod_monitor/src/pod_monitor.h>

#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace
{

using namespace atlasagent::pod_monitor_test;

class PodMonitorTest : public atlasagent::PodMonitor
{
   public:
    using PodMonitor::PodMonitor;
    using PodMonitor::TrackedPods;
};

class FakePodCgroupSource final : public atlasagent::PodCgroupSource
{
   public:
    explicit FakePodCgroupSource(atlasagent::CgroupDiscoveryResult result) : result_(std::move(result)) {}

    [[nodiscard]] atlasagent::CgroupDiscoveryResult Discover() const noexcept override
    {
        ++calls_;
        return result_;
    }

    void SetResult(atlasagent::CgroupDiscoveryResult result) { result_ = std::move(result); }

    [[nodiscard]] std::size_t Calls() const noexcept { return calls_; }

   private:
    atlasagent::CgroupDiscoveryResult result_;
    mutable std::size_t calls_ = 0;
};

class FakePodIdentitySource final : public atlasagent::PodIdentitySource
{
   public:
    explicit FakePodIdentitySource(atlasagent::PodIdentityResult result) : result_(std::move(result)) {}

    [[nodiscard]] atlasagent::PodIdentityResult FetchPodIdentities() const noexcept override
    {
        ++calls_;
        return result_;
    }

    void SetResult(atlasagent::PodIdentityResult result) { result_ = std::move(result); }

    [[nodiscard]] std::size_t Calls() const noexcept { return calls_; }

   private:
    atlasagent::PodIdentityResult result_;
    mutable std::size_t calls_ = 0;
};

TEST(PodMonitor, SuccessfulRefreshThenIdentityFailureSuspendsEmission)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    auto* memory_writer = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    atlasagent::CgroupSnapshot cgroups;
    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    cgroups.emplace(
        kPod1Uid,
        atlasagent::CgroupPod{cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')})});
    atlasagent::PodIdentityMap identities;
    identities.emplace(kPod1Uid,
                       atlasagent::PodIdentity{"pod-one", "ns-one",
                                               {{ContainerId('a'), atlasagent::ContainerIdentity{"main", 0.25}}},
                                               {{"netflix.com/app", "myapp"}}, {}});

    auto cgroup_source = std::make_unique<FakePodCgroupSource>(cgroups);
    auto identity_source = std::make_unique<FakePodIdentitySource>(identities);
    auto* cgroup_source_ptr = cgroup_source.get();
    auto* identity_source_ptr = identity_source.get();
    PodMonitorTest pod_monitor{&registry, std::move(cgroup_source), std::move(identity_source), "test-cluster"};

    auto refreshed = pod_monitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kActive);
    ASSERT_EQ(pod_monitor.TrackedPods().at(kPod1Uid).containers.size(), 1);
    EXPECT_EQ(refreshed.active_pods.at(kPod1Uid).containers.size(), 1);
    EXPECT_EQ(cgroup_source_ptr->Calls(), 1);
    EXPECT_EQ(identity_source_ptr->Calls(), 1);

    pod_monitor.CollectMemoryStats();
    EXPECT_EQ(cgroup_source_ptr->Calls(), 1);
    EXPECT_EQ(identity_source_ptr->Calls(), 1);

    identity_source_ptr->SetResult(
        std::unexpected(atlasagent::PodIdentityError{atlasagent::PodIdentityErrorKind::Http, 503}));
    refreshed = pod_monitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kIdentityUnavailable);
    EXPECT_TRUE(pod_monitor.TrackedPods().empty());
    EXPECT_TRUE(refreshed.active_pods.empty());

    memory_writer->Clear();
    pod_monitor.CollectCpuStats(true, true);
    pod_monitor.CollectIOStats();
    pod_monitor.CollectMemoryStats();
    EXPECT_TRUE(memory_writer->GetMessages().empty());

    // A successful empty identity snapshot is authoritative rather than an error: emission is
    // active again, but there are no admitted containers.
    identity_source_ptr->SetResult(atlasagent::PodIdentityMap{});
    refreshed = pod_monitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kActive);
    EXPECT_TRUE(pod_monitor.TrackedPods().empty());
}

TEST(PodMonitor, CgroupFailureSuspendsEmissionBeforeIdentityFetch)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    auto* memory_writer = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    atlasagent::CgroupSnapshot cgroups;
    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    cgroups.emplace(
        kPod1Uid,
        atlasagent::CgroupPod{cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')})});
    atlasagent::PodIdentityMap identities;
    identities.emplace(kPod1Uid,
                       atlasagent::PodIdentity{"pod-one", "ns-one",
                                               {{ContainerId('a'),
                                                 atlasagent::ContainerIdentity{"main", std::nullopt}}},
                                               {{"netflix.com/app", "myapp"}}, {}});

    auto cgroup_source = std::make_unique<FakePodCgroupSource>(cgroups);
    auto identity_source = std::make_unique<FakePodIdentitySource>(identities);
    auto* cgroup_source_ptr = cgroup_source.get();
    auto* identity_source_ptr = identity_source.get();
    PodMonitorTest pod_monitor{&registry, std::move(cgroup_source), std::move(identity_source), ""};

    auto refreshed = pod_monitor.Refresh();
    ASSERT_EQ(refreshed.state, atlasagent::PodMonitorState::kActive);
    ASSERT_EQ(pod_monitor.TrackedPods().at(kPod1Uid).containers.size(), 1);

    const auto missing_path = std::filesystem::path{"/missing/cgroup/root"};
    const auto missing_cause = std::make_error_code(std::errc::no_such_file_or_directory);
    cgroup_source_ptr->SetResult(std::unexpected(atlasagent::CgroupDiscoveryError{
        atlasagent::CgroupDiscoveryErrorKind::kMissingRoot, missing_path, missing_cause}));
    refreshed = pod_monitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kCgroupUnavailable);
    ASSERT_TRUE(refreshed.cgroup_error.has_value());
    EXPECT_EQ(refreshed.cgroup_error->kind, atlasagent::CgroupDiscoveryErrorKind::kMissingRoot);
    EXPECT_EQ(refreshed.cgroup_error->path, missing_path);
    EXPECT_EQ(refreshed.cgroup_error->cause, missing_cause);
    EXPECT_TRUE(pod_monitor.TrackedPods().empty());
    EXPECT_EQ(cgroup_source_ptr->Calls(), 2);
    EXPECT_EQ(identity_source_ptr->Calls(), 1);

    memory_writer->Clear();
    pod_monitor.CollectCpuStats(true, true);
    pod_monitor.CollectIOStats();
    pod_monitor.CollectMemoryStats();
    EXPECT_TRUE(memory_writer->GetMessages().empty());
}

TEST(PodMonitor, MissingInjectedSourcesReturnExplicitFailures)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);

    auto identity_source = std::make_unique<FakePodIdentitySource>(atlasagent::PodIdentityMap{});
    auto* identity_source_ptr = identity_source.get();
    PodMonitorTest missing_cgroup{&registry, std::unique_ptr<atlasagent::PodCgroupSource>{},
                                  std::move(identity_source), ""};

    auto refreshed = missing_cgroup.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kCgroupUnavailable);
    ASSERT_TRUE(refreshed.cgroup_error.has_value());
    EXPECT_EQ(refreshed.cgroup_error->kind, atlasagent::CgroupDiscoveryErrorKind::kUnavailableSource);
    EXPECT_EQ(identity_source_ptr->Calls(), 0);

    auto cgroup_source = std::make_unique<FakePodCgroupSource>(atlasagent::CgroupSnapshot{});
    auto* cgroup_source_ptr = cgroup_source.get();
    PodMonitorTest missing_identity{&registry, std::move(cgroup_source),
                                    std::unique_ptr<atlasagent::PodIdentitySource>{}, ""};

    refreshed = missing_identity.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kIdentityUnavailable);
    ASSERT_TRUE(refreshed.identity_error.has_value());
    EXPECT_EQ(refreshed.identity_error->kind, atlasagent::PodIdentityErrorKind::UnavailableSource);
    EXPECT_EQ(cgroup_source_ptr->Calls(), 1);
}

}  // namespace
