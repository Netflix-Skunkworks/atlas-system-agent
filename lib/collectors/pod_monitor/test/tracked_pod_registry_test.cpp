#include <lib/collectors/pod_monitor/src/util/tracked_pod_registry.h>
#include "pod_monitor_test_support.h"

#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using namespace atlasagent::pod_monitor_test;

// cgroup_path and cgroup_containers supply the cgroup side; identity_containers supplies the
// independent kubelet side. Keeping them separate lets tests exercise matched and cgroup-only IDs.
atlasagent::ActivePodMap OnePodPlan(const std::string& uid, const std::string& cgroup_path,
                                    atlasagent::ContainerCgroupMap cgroup_containers,
                                    std::unordered_map<std::string, std::string> identity_containers,
                                    std::unordered_map<std::string, std::string> annotations,
                                    const std::string& name = "pod-one",
                                    const std::string& pod_namespace = "ns-one",
                                    std::unordered_map<std::string, double> cpu_requests = {})
{
    atlasagent::CgroupSnapshot cgroups;
    cgroups.emplace(uid, atlasagent::CgroupPod{cgroup_path, std::move(cgroup_containers)});

    atlasagent::PodIdentity identity{name, pod_namespace, {}, std::move(annotations), {}};
    for (auto& [container_id, container_name] : identity_containers)
    {
        auto request = cpu_requests.find(container_name);
        std::optional<double> cpu_request =
            request != cpu_requests.end() ? std::optional<double>{request->second} : std::nullopt;
        identity.containers.emplace(std::move(container_id),
                                    atlasagent::ContainerIdentity{std::move(container_name), cpu_request});
    }

    atlasagent::PodIdentityMap identities;
    identities.emplace(uid, std::move(identity));
    return atlasagent::BuildActivePods(cgroups, identities, "");
}

// Emitted lines look like "<sym>:<name>,<k>=<v>,<k>=<v>:<value>\n". ToSpectatorId() builds the tag
// list by iterating an unordered_map, so TAG ORDER IS NOT STABLE -- assert on substrings, never
// whole lines.
bool AnyLineContains(const std::vector<std::string>& messages, const std::string& needle)
{
    return std::any_of(messages.begin(), messages.end(),
                       [&needle](const std::string& line) { return line.find(needle) != std::string::npos; });
}

// For "one line carries all of these at once" -- e.g. a container's line also carrying its pod's
// shared tags.
bool AnyLineContainsAll(const std::vector<std::string>& messages, const std::vector<std::string>& needles)
{
    return std::any_of(messages.begin(), messages.end(), [&needles](const std::string& line) {
        return std::all_of(needles.begin(), needles.end(),
                           [&line](const std::string& needle) { return line.find(needle) != std::string::npos; });
    });
}

// mkdtemp rather than a fixed path under temp_directory_path(): the directory is guaranteed fresh
// and unique, so two test binaries running concurrently cannot share or clobber one tree.
std::filesystem::path CreateTempRoot(const std::string& name)
{
    auto path_template =
        (std::filesystem::temp_directory_path() / ("pod_monitor_test_" + name + "_XXXXXX")).string();
    auto* created = mkdtemp(path_template.data());
    if (created == nullptr)
    {
        throw std::system_error(errno, std::generic_category(), "mkdtemp");
    }
    return std::filesystem::path(created);
}

// A throwaway cgroup tree for the cases that need a file MUTATED between two collection cycles,
// which a checked-in fixture cannot express. Removes itself on destruction.
class TempCgroupTree
{
   public:
    explicit TempCgroupTree(const std::string& name) : root_(CreateTempRoot(name))
    {
        std::filesystem::create_directories(ScopePath());
    }

    ~TempCgroupTree() { std::filesystem::remove_all(root_); }

    TempCgroupTree(const TempCgroupTree&) = delete;
    TempCgroupTree& operator=(const TempCgroupTree&) = delete;

    // The pod-slice directory used by CgroupSnapshot and active-pod fixtures.
    [[nodiscard]] std::string PodPath() const { return root_.string(); }

    [[nodiscard]] std::filesystem::path ScopePath() const
    {
        return root_ / ("cri-containerd-" + ContainerId('a') + ".scope");
    }

    void WriteScopeFile(const char* filename, const std::string& contents) const
    {
        std::ofstream out(ScopePath() / filename, std::ios::trunc);
        out << contents;
    }

   private:
    std::filesystem::path root_;
};

// ---------------------------------------------------------------------------------------------
// TrackedPodRegistry consumes active pod maps. OnePodPlan builds those maps from fixture cgroups and
// fabricated identities, keeping filesystem/identity admission separate from mutable CGroup state.
//
// WRITER DISCIPLINE, load-bearing: WriterTestHelper::GetImpl() returns a process-wide singleton
// shared by every test in this binary, so each emission-focused test in this section Clear()s it
// immediately before the Emit* call it asserts on rather than relying on it starting empty.
// The fixture's declaration order fetches GetImpl() only AFTER constructing the Registry, matching
// cgroup_test.cpp's convention; fresh tracking state uses another TrackedPodRegistry sharing that
// Registry.
// ---------------------------------------------------------------------------------------------

class TrackedPodRegistryTest : public testing::Test
{
   protected:
    Config config{WriterConfig(WriterTypes::Memory)};
    Registry r{config};
    MemoryWriter* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
};

TEST_F(TrackedPodRegistryTest, TagGateEvictsTrackedPod)
{
    atlasagent::TrackedPodRegistry registry{&r};

    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const auto container_id = ContainerId('a');

    // Cycle 1: netflix.com/app resolves, so Gating passes and the container is tracked.
    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {container_id}),
                                  {{container_id, "main"}}, {{"netflix.com/app", "myapp"}}));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    // Cycle 2: removing the annotation fails the plan's pod-level tag gate. The pod and all of its
    // containers leave the active plan, so reconciliation evicts their metric state.
    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {container_id}),
                                  {{container_id, "main"}}, {}));
    EXPECT_FALSE(registry.TrackedPods().contains(kPod1Uid));
}

TEST_F(TrackedPodRegistryTest, EvictsPodWhenAllCgroupScopesDisappear)
{
    atlasagent::TrackedPodRegistry registry{&r};

    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};
    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};

    // Cycle 1: the pod's cgroup_path has a real container scope, so it gets tracked.
    const auto first_path = Pod1SlicePath("systemd_pod_with_containers");
    registry.Reconcile(OnePodPlan(kPod1Uid, first_path, ContainerScopes(first_path, {ContainerId('a')}), containers,
                                  annotations));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    // Cycle 2: same uid and kubelet-reported container, but cgroup_path now points at a pod slice
    // with NO container scopes -- the scope directory vanished. The active-pod builder omits the
    // pod because it has no admitted containers, so reconciliation evicts the whole pod.
    registry.Reconcile(OnePodPlan(kPod1Uid, Pod1SlicePath("systemd"), {}, containers, annotations));
    EXPECT_FALSE(registry.TrackedPods().contains(kPod1Uid));
}

TEST_F(TrackedPodRegistryTest, OmitsPodUntilContainerIdentityArrives)
{
    atlasagent::TrackedPodRegistry registry{&r};

    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // The container's cgroup scope exists on disk but kubelet hasn't reported it yet (empty
    // container list) -- the documented transient race. With no admitted containers, the pod is
    // absent from the active map and is not tracked...
    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}), {},
                                  annotations));
    EXPECT_FALSE(registry.TrackedPods().contains(kPod1Uid));

    // ...and once kubelet does report it, the next cycle picks it up with no intervening restart.
    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                  {{ContainerId('a'), "main"}}, annotations));
    EXPECT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
}

TEST_F(TrackedPodRegistryTest, MissingCurrentContainerIdentityEvictsPod)
{
    atlasagent::TrackedPodRegistry registry{&r};
    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                  {{ContainerId('a'), "main"}}, annotations));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}), {},
                                  annotations));
    EXPECT_FALSE(registry.TrackedPods().contains(kPod1Uid));
}

// Both container scopes in this fixture carry real memory.current/max/stat/events/swap files ON
// PURPOSE -- do not strip them. CGroup's memory readers use unordered_map::operator[], which
// INSERTS a zero for absent memory.stat/memory.events keys, so a scope with no memory files still
// emits several fabricated zero-valued metrics. This test would "pass" on an empty fixture only by
// leaning on that behavior; real values keep it independent of it.
TEST_F(TrackedPodRegistryTest, PerContainerTagsReachEmittedLinesWithSharedPodTags)
{
    atlasagent::TrackedPodRegistry registry{&r};

    registry.Reconcile(OnePodPlan(kPod1Uid, Pod1SlicePath("systemd_pod_with_two_containers"),
                                    ContainerScopes(Pod1SlicePath("systemd_pod_with_two_containers"),
                                                    {ContainerId('a'), ContainerId('b')}),
                                    {{ContainerId('a'), "main"}, {ContainerId('b'), "sidecar"}},
                                    {{"netflix.com/app", "myapp"}, {"netflix.com/stack", "mystack"}}));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 2);

    memoryWriter->Clear();
    registry.EmitMemoryStats();
    auto messages = memoryWriter->GetMessages();
    ASSERT_FALSE(messages.empty());

    // Each container is disambiguated by its own nf.process...
    EXPECT_TRUE(AnyLineContains(messages, "nf.process=main"));
    EXPECT_TRUE(AnyLineContains(messages, "nf.process=sidecar"));

    // ...while BOTH carry the pod-level tags -- the assertion that matters. ReconcileContainers
    // copies active.tags per iteration and std::move()s that copy into SetExtraTags: hoisting it
    // out of the loop as an "optimization" would leave every container after the first with an
    // empty tag map -- metrics that still publish, but silently unattributable to any app.
    EXPECT_TRUE(AnyLineContainsAll(messages, {"nf.process=main", "nf.app=myapp", "nf.stack=mystack"}));
    EXPECT_TRUE(AnyLineContainsAll(messages, {"nf.process=sidecar", "nf.app=myapp", "nf.stack=mystack"}));
}

TEST_F(TrackedPodRegistryTest, InjectsNamespaceAndRejectsMissingPodIdentity)
{
    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // The active-pod builder adds a known namespace after ResolvePodTags succeeds.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                      containers, annotations, "pod-one", "ns-one"));
        memoryWriter->Clear();
        registry.EmitMemoryStats();
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContains(messages, "k8s.namespace.name=ns-one"));
    }

    // Missing required pod identity rejects the entire pod rather than emitting partially
    // attributed container metrics.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                      containers, annotations, "", ""));
        memoryWriter->Clear();
        registry.EmitMemoryStats();
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(messages.empty());
        EXPECT_FALSE(registry.TrackedPods().contains(kPod1Uid));
    }

    // A later invalid identity is authoritative for the new plan; reconciliation must not preserve
    // the previously valid namespace or continue emitting the old container.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                      containers, annotations, "pod-one", "ns-one"));
        registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                      containers, annotations, "", ""));
        memoryWriter->Clear();
        registry.EmitMemoryStats();
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(messages.empty());
        EXPECT_FALSE(registry.TrackedPods().contains(kPod1Uid));
    }
}

TEST_F(TrackedPodRegistryTest, ResolvesCpuCountFromLimitedQuotaAndUnlimitedFallback)
{
    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // cpu.max "50000 100000" -> quota/period == 0.5. When the 60-second CPU branch runs,
    // sys.cpu.numProcessors is written as a Gauge (unlike a Counter, it has no zero-delta gate) and
    // carries the resolved count.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        const auto quota_path = Pod1SlicePath("systemd_single_pod_with_quota");
        registry.Reconcile(
            OnePodPlan(kPod1Uid, quota_path, ContainerScopes(quota_path, {ContainerId('a')}), containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
    }

    // cpu.max "max 100000" is the explicit unlimited case: the tri-state quota result falls back to
    // the online CPU count from sysconf, computed here the same way ResolveCpuCount does.
    {
        const auto expected = ":" + std::to_string(static_cast<double>(sysconf(_SC_NPROCESSORS_ONLN)));
        atlasagent::TrackedPodRegistry registry{&r};
        const auto unlimited_path = Pod1SlicePath("systemd_pod_cpu_unlimited");
        registry.Reconcile(
            OnePodPlan(kPod1Uid, unlimited_path, ContainerScopes(unlimited_path, {ContainerId('a')}), containers,
                       annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", expected}));
    }
}

// k8s.cpu.requested must carry the DECLARED request, not the cpu.max limit -- before the request
// plumbing both gauges published the same limit-derived number, so a container's request was
// indistinguishable from its limit. The fixture's cpu.max is "50000 100000" (0.5 cores) against a
// declared 250m request, so the two gauges MUST disagree: if they ever print the same value again,
// the request has stopped being threaded through.
TEST_F(TrackedPodRegistryTest, RequestedGaugeReportsDeclaredRequestNotTheLimit)
{
    atlasagent::TrackedPodRegistry registry{&r};

    const auto quota_path = Pod1SlicePath("systemd_single_pod_with_quota");
    registry.Reconcile(OnePodPlan(kPod1Uid, quota_path, ContainerScopes(quota_path, {ContainerId('a')}),
                                  {{ContainerId('a'), "main"}}, {{"netflix.com/app", "myapp"}}, "pod-one", "ns-one",
                                  {{"main", 0.25}}));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    auto messages = memoryWriter->GetMessages();

    // The request, from the pod spec, under the pod-scoped name.
    EXPECT_TRUE(AnyLineContainsAll(messages, {"k8s.cpu.requested", ":0.250000"}));
    // Capacity still comes from cpu.max, and must NOT have been overwritten by the request.
    EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
    EXPECT_FALSE(AnyLineContainsAll(messages, {"k8s.cpu.requested", ":0.500000"}));
    // The Titus name is reserved for the Titus path (a fixed allocation) and must not appear here.
    EXPECT_FALSE(AnyLineContains(messages, "titus.cpu.requested"));
}

// A container with no parsed CPU request (for example, a BestEffort or limits-only container) must
// publish NO k8s.cpu.requested at all -- not the limit, and not a zero that would turn every
// utilization/requested division in a dashboard into inf. The capacity gauge is unaffected.
TEST_F(TrackedPodRegistryTest, RequestedGaugeOmittedForContainerWithNoCpuRequest)
{
    atlasagent::TrackedPodRegistry registry{&r};

    // Same fixture and container as above, but cpu_requests is empty.
    const auto quota_path = Pod1SlicePath("systemd_single_pod_with_quota");
    registry.Reconcile(OnePodPlan(kPod1Uid, quota_path, ContainerScopes(quota_path, {ContainerId('a')}),
                                  {{ContainerId('a'), "main"}}, {{"netflix.com/app", "myapp"}}));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    auto messages = memoryWriter->GetMessages();

    EXPECT_FALSE(AnyLineContains(messages, "k8s.cpu.requested"));
    EXPECT_FALSE(AnyLineContains(messages, "titus.cpu.requested"));
    EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
}

TEST_F(TrackedPodRegistryTest, ReReadsCpuCountBeforeEveryCpuEmission)
{
    atlasagent::TrackedPodRegistry registry{&r};

    // An ALREADY-TRACKED container must pick up a changed cpu.max. try_emplace won't rebuild the
    // CGroup (its path is fixed at first insertion), so the quota has to be rewritten underneath
    // the same path -- what an in-place vertical resize does on a live node, and what a checked-in
    // fixture cannot express.
    TempCgroupTree tree{"cpu_resize"};
    tree.WriteScopeFile("cpu.max", "50000 100000\n");

    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    registry.Reconcile(OnePodPlan(kPod1Uid, tree.PodPath(), ContainerScopes(tree.PodPath(), {ContainerId('a')}),
                                  containers, annotations));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    auto messages = memoryWriter->GetMessages();
    ASSERT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));

    // Resize to 2 CPUs without reconciling again. Reading only at insertion or refresh would keep
    // publishing 0.5 here until the next minute.
    tree.WriteScopeFile("cpu.max", "200000 100000\n");
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    messages = memoryWriter->GetMessages();
    EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":2.000000"}));
    EXPECT_FALSE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
}

TEST_F(TrackedPodRegistryTest, RecreatesContainerWhenCgroupPathChanges)
{
    atlasagent::TrackedPodRegistry registry{&r};
    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    const auto first_cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    registry.Reconcile(OnePodPlan(kPod1Uid, first_cgroup_path,
                                  ContainerScopes(first_cgroup_path, {ContainerId('a')}), containers,
                                  annotations));
    const auto first_path = registry.TrackedPods().at(kPod1Uid).containers.at(ContainerId('a')).cgroup_path;

    const auto second_cgroup_path = Pod1SlicePath("systemd_single_pod_with_quota");
    registry.Reconcile(OnePodPlan(kPod1Uid, second_cgroup_path,
                                  ContainerScopes(second_cgroup_path, {ContainerId('a')}), containers,
                                  annotations));
    const auto second_path = registry.TrackedPods().at(kPod1Uid).containers.at(ContainerId('a')).cgroup_path;

    EXPECT_NE(first_path, second_path);
    EXPECT_EQ(second_path,
              std::filesystem::path(Pod1SlicePath("systemd_single_pod_with_quota")) /
                  ("cri-containerd-" + ContainerId('a') + ".scope"));
}

// Regression guard for the noexcept/out-of-bounds defects fixed in cgroup.cpp: CpuUtilizationV2 and
// CpuPeakUtilizationV2 read cpu.stat keys, GetAvailCpuTime indexes cpu.max's parsed fields, and both
// are noexcept. They are reachable when a discovered scope directory exists but either file is
// missing or partial; the scope can also disappear after the emit loop's directory check.
//
// Deliberately NOT a death test: before the fix the cpu.max path was an out-of-bounds read (UB),
// not a clean throw, and pinning a regression test to UB is not meaningful. This asserts the
// post-fix contract -- return cleanly, emit nothing that depends on the missing data.
TEST_F(TrackedPodRegistryTest, EmitCpuStatsSurvivesMissingAndPartialCpuStat)
{
    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // Case A: cpu.max is readable but cpu.stat is absent. This isolates the cpu.stat guard from
    // the tri-state quota's unreadable path.
    {
        TempCgroupTree tree{"missing_cpu_stat"};
        tree.WriteScopeFile("cpu.max", "50000 100000\n");
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Reconcile(OnePodPlan(kPod1Uid, tree.PodPath(), ContainerScopes(tree.PodPath(), {ContainerId('a')}),
                                      containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);

        auto messages = memoryWriter->GetMessages();
        // Non-vacuity: without this, every absence assertion below would also be satisfied by
        // "nothing ran at all", and a regression hoisting the cpu.stat guard above this gauge
        // (silently killing it for every pod) would go unnoticed. It depends only on the resolved
        // CPU count, not on cpu.stat, so it survives here by design.
        EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.numProcessors"));
        // No "requested" gauge at all: these plans declare no CPU request, and a container
        // without one omits it rather than publishing the limit (or the node's core count) as
        // though it were the request. titus.cpu.requested belongs to the Titus path and must never
        // appear on pod metrics.
        EXPECT_FALSE(AnyLineContains(messages, "k8s.cpu.requested"));
        EXPECT_FALSE(AnyLineContains(messages, "titus.cpu.requested"));
        // The cpu.stat-derived metrics must be absent rather than garbage.
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.utilization"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.peakUtilization"));
        EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.usageTime"));
        EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.processingTime"));
    }

    // Case B: cpu.stat EXISTS but is missing the keys these functions read -- a present-but-partial
    // file, which a whole-file existence check would have let through. cpu.max remains readable.
    {
        TempCgroupTree tree{"partial_cpu_stat"};
        tree.WriteScopeFile("cpu.max", "50000 100000\n");
        tree.WriteScopeFile("cpu.stat", "usage_usec 1000\n");

        atlasagent::TrackedPodRegistry registry{&r};
        registry.Reconcile(OnePodPlan(kPod1Uid, tree.PodPath(), ContainerScopes(tree.PodPath(), {ContainerId('a')}),
                                      containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);

        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.numProcessors"));
        // Same "requested"/titus reasoning as Case A.
        EXPECT_FALSE(AnyLineContains(messages, "k8s.cpu.requested"));
        EXPECT_FALSE(AnyLineContains(messages, "titus.cpu.requested"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.utilization"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.peakUtilization"));
        EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.usageTime"));
    }

}

TEST_F(TrackedPodRegistryTest, SuppressesCpuMetricsOnUnreadableQuotaAndRecoversWithoutBridgingDelta)
{
    atlasagent::TrackedPodRegistry registry{&r};
    TempCgroupTree tree{"cpu_quota_recovery"};
    tree.WriteScopeFile("cpu.max", "50000 100000\n");
    tree.WriteScopeFile("cpu.stat", "usage_usec 1000\nuser_usec 400\nsystem_usec 600\n");
    tree.WriteScopeFile("memory.current", "1048576\n");

    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};
    registry.Reconcile(OnePodPlan(kPod1Uid, tree.PodPath(), ContainerScopes(tree.PodPath(), {ContainerId('a')}),
                                  containers, annotations));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    // Establish a readable CPU baseline before the quota becomes unreadable.
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    EXPECT_TRUE(AnyLineContains(memoryWriter->GetMessages(), "sys.cpu.numProcessors"));

    ASSERT_TRUE(std::filesystem::remove(tree.ScopePath() / "cpu.max"));

    // CPU is suppressed as soon as cpu.max is unreadable, without waiting for reconciliation, while
    // memory continues to emit. The CPU baseline is reset so the large usage jump below cannot
    // become a bridged delta.
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    registry.EmitMemoryStats();
    auto messages = memoryWriter->GetMessages();
    EXPECT_FALSE(AnyLineContains(messages, "sys.cpu."));
    EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu."));
    EXPECT_TRUE(AnyLineContains(messages, "cgroup.mem.used"));

    // Once cpu.max is readable again, CPU emission resumes with a fresh baseline.
    tree.WriteScopeFile("cpu.max", "50000 100000\n");
    tree.WriteScopeFile("cpu.stat", "usage_usec 100000\nuser_usec 40000\nsystem_usec 60000\n");
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    messages = memoryWriter->GetMessages();
    EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.numProcessors"));
    EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.processingTime"));
    EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.usageTime"));
    EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.utilization"));
    EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.peakUtilization"));

    // A subsequent readable sample now produces a normal delta.
    tree.WriteScopeFile("cpu.stat", "usage_usec 102000\nuser_usec 40800\nsystem_usec 61200\n");
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    messages = memoryWriter->GetMessages();
    EXPECT_TRUE(AnyLineContains(messages, "cgroup.cpu.processingTime"));
    EXPECT_TRUE(AnyLineContains(messages, "cgroup.cpu.usageTime"));
    EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.utilization"));
    EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.peakUtilization"));
}

// The complement of EmitCpuStatsSurvivesMissingAndPartialCpuStat above: there the scope directory
// exists with files missing, here it is gone entirely -- a container that terminated since the last
// Refresh(). In the shipped k8s-agent, Refresh() normally runs on the 60-second memory cadence while
// EmitCpuStats runs every second, so the entry can stay tracked until the next refresh.
//
// Emitting for it is not harmless: nf.app/nf.cluster are POD-level, so a dead container's output
// lands under the live app's tags. cgroup.cpu.processingCapacity is the real damage -- it reads no
// files at all (pure delta_t * cpuCount), so nothing about the vanished cgroup stops it, and being
// a Counter it permanently accumulates ~60s of phantom capacity into the pod's own utilization
// denominator. sys.cpu.numProcessors and k8s.cpu.requested leak the same way, because
// CpuUtilizationV2 emits both BEFORE its cpu.stat guard.
//
// Asserting emission BEFORE the removal keeps this honest: without that baseline every absence
// assertion below would also be satisfied by "nothing ran at all".
TEST_F(TrackedPodRegistryTest, SkipsEmissionForContainerWhoseCgroupVanishedSinceRefresh)
{
    atlasagent::TrackedPodRegistry registry{&r};

    TempCgroupTree tree{"vanished_scope"};
    tree.WriteScopeFile("cpu.max", "50000 100000\n");
    tree.WriteScopeFile("memory.current", "1048576\n");

    registry.Reconcile(OnePodPlan(kPod1Uid, tree.PodPath(), ContainerScopes(tree.PodPath(), {ContainerId('a')}),
                                  {{ContainerId('a'), "main"}}, {{"netflix.com/app", "myapp"}}, "pod-one", "ns-one",
                                  {{"main", 0.25}}));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    // Baseline: while the scope exists, every leak-prone metric really is published.
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    registry.EmitMemoryStats();
    {
        auto messages = memoryWriter->GetMessages();
        ASSERT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
        ASSERT_TRUE(AnyLineContainsAll(messages, {"k8s.cpu.requested", ":0.250000"}));
        ASSERT_TRUE(AnyLineContains(messages, "cgroup.cpu.processingCapacity"));
        ASSERT_TRUE(AnyLineContains(messages, "cgroup.mem.used"));
    }

    // The container terminates: containerd removes the scope directory. It stays TRACKED because no
    // new plan has been reconciled, so this exercises the liveness skip rather than eviction.
    std::filesystem::remove_all(tree.ScopePath());
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    registry.EmitIOStats();
    registry.EmitMemoryStats();

    auto messages = memoryWriter->GetMessages();
    // Named individually as well as via the emptiness check below, so a regression reports WHICH
    // metric started leaking rather than just a count.
    EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.processingCapacity"));
    EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.numProcessors"));
    EXPECT_FALSE(AnyLineContains(messages, "k8s.cpu.requested"));
    // Memory would otherwise publish fabricated zeros from the now-absent files: cgroup.cpp reads
    // memory.stat/memory.events keys with operator[] rather than find().
    //
    // DO NOT trim the CPU assertions above on the grounds that these memory ones cover it. Once the
    // operator[] reads are guarded, these two hold whether or not the liveness skip exists -- the
    // files are gone either way -- so they stop being evidence. Only cgroup.cpu.processingCapacity
    // can prove the skip: it reads NO files at all, so its absence is attributable to nothing but
    // the guard.
    EXPECT_FALSE(AnyLineContains(messages, "cgroup.mem."));
    EXPECT_FALSE(AnyLineContains(messages, "mem.cached"));
    EXPECT_TRUE(messages.empty());

    // NOTE: EmitIOStats' guard is NOT independently covered. Before removal this scope has no
    // io.stat, and after removal IOStats would likewise parse no lines even without the liveness
    // check. Its contribution to the emptiness assertion therefore holds either way. Proving the
    // guard needs an io.stat fixture that would otherwise emit.
}

TEST_F(TrackedPodRegistryTest, EmitMethodsAreNoOpsWithNothingTracked)
{
    atlasagent::TrackedPodRegistry registry{&r};

    // Every Emit* is called on the agent's normal cadence regardless of whether anything is
    // tracked yet -- k8s-agent.cpp drives CollectCpuStats every second from process start.
    memoryWriter->Clear();
    EXPECT_NO_FATAL_FAILURE(registry.EmitCpuStats(true, true));
    EXPECT_NO_FATAL_FAILURE(registry.EmitIOStats());
    EXPECT_NO_FATAL_FAILURE(registry.EmitMemoryStats());
    EXPECT_TRUE(memoryWriter->GetMessages().empty());
}

}  // namespace
