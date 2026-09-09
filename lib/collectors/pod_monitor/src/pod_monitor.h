#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <lib/collectors/pod_monitor/src/util/cgroup_pod_discovery.h>
#include <lib/collectors/pod_monitor/src/util/pod_identity_client.h>
#include <lib/collectors/pod_monitor/src/util/active_pod_builder.h>
#include <lib/collectors/pod_monitor/src/util/tracked_pod_registry.h>

#include <memory>
#include <optional>
#include <string>

namespace atlasagent
{

enum class PodMonitorState
{
    kActive,
    kCgroupUnavailable,
    kIdentityUnavailable,
};

struct PodRefreshResult
{
    PodMonitorState state;
    std::optional<CgroupDiscoveryError> cgroup_error;
    std::optional<PodIdentityError> identity_error;
    ActivePodMap active_pods;
};

// Orchestrates cgroup and kubelet probes, builds the active container snapshot, and applies it to the
// stateful metric registry. Probe failures explicitly suspend all container emission.
class PodMonitor
{
   public:
    explicit PodMonitor(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                         std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // Injection seam for hermetic tests and alternate probe adapters. PodMonitor owns both sources.
    PodMonitor(Registry* registry, std::unique_ptr<PodCgroupSource> cgroup_source,
               std::unique_ptr<PodIdentitySource> identity_source, std::string k8s_cluster) noexcept;

    // Performs both external probes and applies a new active snapshot only when both succeed. Any
    // probe error clears tracked baselines and leaves every Emit* method disabled until a successful
    // refresh. The returned active_pods map is transient; PodMonitor does not retain a second copy.
    [[nodiscard]] PodRefreshResult Refresh() noexcept;

    // Forwards to TrackedPodRegistry::EmitCpuStats(); see there for detail.
    void CollectCpuStats(const bool fiveSecondMetricsEnabled, const bool sixtySecondMetricsEnabled) noexcept
    {
        tracked_registry_.EmitCpuStats(fiveSecondMetricsEnabled, sixtySecondMetricsEnabled);
    }

    // Forwards to TrackedPodRegistry::EmitIOStats(); see there for detail.
    void CollectIOStats() noexcept { tracked_registry_.EmitIOStats(); }

    // Emits from the most recently applied active snapshot without performing network or discovery
    // work; callers schedule Refresh() explicitly.
    void CollectMemoryStats() noexcept { tracked_registry_.EmitMemoryStats(); }

   protected:
    // Read-only view of the tracked pod/container set, for test assertions and debug tooling.
    [[nodiscard]] const PodTrackedMap& TrackedPods() const noexcept { return tracked_registry_.TrackedPods(); }

   private:
    std::unique_ptr<PodCgroupSource> cgroup_source_;
    std::unique_ptr<PodIdentitySource> identity_source_;
    std::string k8s_cluster_;
    TrackedPodRegistry tracked_registry_;
};

}  // namespace atlasagent
