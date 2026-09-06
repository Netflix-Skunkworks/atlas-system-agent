#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/collectors/pod_monitor/src/util/pod_identity_client.h>
#include <lib/collectors/pod_monitor/src/util/cgroup_pod_discovery.h>
#include <lib/collectors/pod_monitor/src/util/pod_tag_resolver.h>
#include <lib/collectors/pod_monitor/src/util/cpu_quantity.h>
#include <lib/collectors/pod_monitor/src/util/tracked_pod_registry.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

class PodMonitorTest : public atlasagent::PodMonitor
{
   public:
    // Points at a reserved, virtually-guaranteed-unreachable local port (not a real path or
    // file) -- FetchPodIdentities()'s HTTP GET fails fast (connection refused) against it, so
    // identity resolution always returns nullopt and no real network access is ever made. This
    // is what makes every test in this file hermetic.
    explicit PodMonitorTest(Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
                             std::string kubelet_url = "http://127.0.0.1:1") noexcept
        : PodMonitor(registry, std::move(path_prefix), std::move(kubelet_url))
    {
    }

    // Expose protected members and methods for testing
    using PodMonitor::JoinCgroupAndIdentity;
    using PodMonitor::RefreshTrackedPods;
    using PodMonitor::TrackedPods;
};

// CgroupPodDiscovery keeps ScanPodSliceDirectory/MatchPodSliceName/NormalizePodUid protected --
// same thin-subclass-with-`using` convention as PodMonitorTest above. FindActivePodCgroups()/
// FindContainersInPod() are public on CgroupPodDiscovery itself, so tests exercising those call
// atlasagent::CgroupPodDiscovery directly without needing this shim at all.
class CgroupPodDiscoveryTest : public atlasagent::CgroupPodDiscovery
{
   public:
    using CgroupPodDiscovery::ScanPodSliceDirectory;
    using CgroupPodDiscovery::MatchPodSliceName;
    using CgroupPodDiscovery::NormalizePodUid;
};

class PodIdentityClientTest : public atlasagent::PodIdentityClient
{
   public:
    explicit PodIdentityClientTest(Registry* registry, std::string kubelet_url = "http://127.0.0.1:1") noexcept
        : PodIdentityClient(registry, std::move(kubelet_url))
    {
    }

    // Expose protected methods for testing
    using PodIdentityClient::ParsePodList;
};

namespace
{

// Fixture roots, so a path typo is a compile-visible constant rather than a silently-empty scan.
constexpr auto kResources = "lib/collectors/pod_monitor/test/resources";

// The pod uid every single-pod fixture tree uses, in both its cgroup-directory (underscore) and
// canonical (dashed) spellings.
constexpr auto kPod1Dir = "kubepods-pod11111111_1111_1111_1111_111111111111.slice";
constexpr auto kPod1Uid = "11111111-1111-1111-1111-111111111111";

std::string ContainerId(char c) { return std::string(64, c); }

std::string Pod1SlicePath(const std::string& tree)
{
    return std::string(kResources) + "/" + tree + "/kubepods.slice/" + kPod1Dir;
}

// Builds a one-pod PodInfoMap for driving TrackedPodRegistry::Refresh() DIRECTLY. This is the
// seam that makes the gated-in path testable at all: Refresh() takes its input as a plain
// parameter, so unlike going through PodMonitor (whose FindActivePodInfo() always fails its
// kubelet lookup in this harness) a test can supply resolved annotations and a non-empty
// container list. `cgroup_path` must still point at a real fixture tree, because Refresh()
// discovers the pod's container scopes from the filesystem.
atlasagent::PodInfoMap OnePodInfoMap(const std::string& uid, const std::string& cgroup_path,
                                      std::unordered_map<std::string, std::string> containers,
                                      std::unordered_map<std::string, std::string> annotations,
                                      const std::string& name = "pod-one",
                                      const std::string& pod_namespace = "ns-one",
                                      std::unordered_map<std::string, double> cpu_requests = {})
{
    atlasagent::PodInfoMap pods;
    // Positional aggregate init -- keep in sync with PodInfo's declaration order (uid,
    // cgroup_path, name, pod_namespace, containers, annotations, labels, cpu_requests).
    pods.emplace(uid, atlasagent::PodInfo{uid, cgroup_path, name, pod_namespace, std::move(containers),
                                           std::move(annotations), {}, std::move(cpu_requests)});
    return pods;
}

// Emitted lines look like "<sym>:<name>,<k>=<v>,<k>=<v>:<value>\n". ToSpectatorId() builds the
// tag list by iterating an unordered_map, so TAG ORDER IS NOT STABLE -- assertions on tagged
// metrics must match substrings, never whole lines.
bool AnyLineContains(const std::vector<std::string>& messages, const std::string& needle)
{
    return std::any_of(messages.begin(), messages.end(),
                       [&needle](const std::string& line) { return line.find(needle) != std::string::npos; });
}

// For "this one line carries all of these at once" -- e.g. that a specific container's line also
// carries its pod's shared tags.
bool AnyLineContainsAll(const std::vector<std::string>& messages, const std::vector<std::string>& needles)
{
    return std::any_of(messages.begin(), messages.end(), [&needles](const std::string& line) {
        return std::all_of(needles.begin(), needles.end(),
                           [&line](const std::string& needle) { return line.find(needle) != std::string::npos; });
    });
}

// A throwaway cgroup tree for the cases that need a file MUTATED between two collection cycles,
// which a checked-in fixture cannot express. Removes itself on destruction.
class TempCgroupTree
{
   public:
    explicit TempCgroupTree(const std::string& name)
        : root_(std::filesystem::temp_directory_path() / ("pod_monitor_test_" + name))
    {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(ScopePath());
    }

    ~TempCgroupTree() { std::filesystem::remove_all(root_); }

    TempCgroupTree(const TempCgroupTree&) = delete;
    TempCgroupTree& operator=(const TempCgroupTree&) = delete;

    // The pod-slice directory to hand to PodInfo::cgroup_path.
    [[nodiscard]] std::string PodPath() const { return root_.string(); }
    [[nodiscard]] std::filesystem::path ScopePath() const { return root_ / ("cri-containerd-" + ContainerId('a') + ".scope"); }

    void WriteScopeFile(const char* filename, const std::string& contents) const
    {
        std::ofstream out(ScopePath() / filename, std::ios::trunc);
        out << contents;
    }

   private:
    std::filesystem::path root_;
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

TEST(CgroupPodDiscovery, FindActivePodCgroupsSystemd)
{
    atlasagent::CgroupPodDiscovery discovery{"lib/collectors/pod_monitor/test/resources/systemd"};

    auto pods = discovery.FindActivePodCgroups();

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

TEST(CgroupPodDiscovery, FindActivePodCgroupsCgroupfs)
{
    atlasagent::CgroupPodDiscovery discovery{"lib/collectors/pod_monitor/test/resources/cgroupfs"};

    auto pods = discovery.FindActivePodCgroups();

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

TEST(CgroupPodDiscovery, FindActivePodCgroupsMissingRoot)
{
    atlasagent::CgroupPodDiscovery discovery{"lib/collectors/pod_monitor/test/resources/does_not_exist"};

    auto pods = discovery.FindActivePodCgroups();

    EXPECT_TRUE(pods.empty());
}

TEST(PodMonitor, RefreshTrackedPodsPartialAddAndEvict)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    // Uses the identity-client's default kubelet URL, which points at nothing listening (see
    // PodMonitorTest's default above), so FindActivePodInfo()'s identity lookup always fails
    // closed and no real network call is ever made -- this test is hermetic.
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd"};

    podMonitor.RefreshTrackedPods();
    ASSERT_EQ(podMonitor.TrackedPods().size(), 3);
    EXPECT_TRUE(podMonitor.TrackedPods().contains("11111111-1111-1111-1111-111111111111"));
    EXPECT_TRUE(podMonitor.TrackedPods().contains("22222222-2222-2222-2222-222222222222"));
    EXPECT_TRUE(podMonitor.TrackedPods().contains("33333333-3333-3333-3333-333333333333"));

    // "systemd_partial" reuses UID 11111111... (already tracked above) and introduces a
    // brand-new UID 77777777...; UIDs 22222222... and 33333333... are no longer discovered.
    podMonitor.SetPrefix("lib/collectors/pod_monitor/test/resources/systemd_partial");
    podMonitor.RefreshTrackedPods();

    const auto& tracked = podMonitor.TrackedPods();
    ASSERT_EQ(tracked.size(), 2);
    EXPECT_TRUE(tracked.contains("11111111-1111-1111-1111-111111111111"));
    EXPECT_TRUE(tracked.contains("77777777-7777-7777-7777-777777777777"));
    EXPECT_FALSE(tracked.contains("22222222-2222-2222-2222-222222222222"));
    EXPECT_FALSE(tracked.contains("33333333-3333-3333-3333-333333333333"));
}

// NOTE on test coverage for the tagging/Gating redesign (annotations/labels via kubelet's local
// API, replacing per-container /proc/<pid>/environ reads): identity resolution and Gating are
// now the same data source (one HTTP call to kubelet), and nothing in this codebase mocks that
// call -- so the *successful* Gating path (a pod whose annotations/labels actually resolve,
// tracked with the right tags) is covered by ResolvePodTags's own direct unit tests and
// JoinCgroupAndIdentity's wiring tests below, not by an end-to-end RefreshTrackedPods test.
// The Gating-*failure* path is likewise NOT covered end-to-end here. See
// RefreshTrackedPodsContainerNotTrackedWhenPodIdentityUnresolved below for why: because identity
// resolution always fails in this harness, a pod's tracked container map is empty from the moment
// it is created and nothing can ever make it non-empty, so an assertion that it is empty holds
// regardless of whether the Gating logic works, is deleted, or is inverted.
//
// Both paths ARE reachable without any HTTP mocking, just not through PodMonitor:
// TrackedPodRegistry's constructor, Refresh(const PodInfoMap&) and TrackedPods() are all public,
// so a test can hand it a fabricated PodInfoMap with populated annotations/labels/containers
// whose cgroup_path points at a real fixture tree. No such test exists yet.

// FindContainersInPod tests (parallel to the ScanPodSliceDirectory/MatchPodSliceName coverage
// above -- structural directory-name matching only, no PID/environ I/O involved). Public on
// CgroupPodDiscovery, so called directly -- no test shim needed.
TEST(CgroupPodDiscovery, FindContainersInPodMatchesCriContainerdScopes)
{
    auto containers = atlasagent::CgroupPodDiscovery::FindContainersInPod(
        "lib/collectors/pod_monitor/test/resources/systemd_pod_with_containers/kubepods.slice/"
        "kubepods-pod11111111_1111_1111_1111_111111111111.slice");

    ASSERT_EQ(containers.size(), 1);
    EXPECT_TRUE(containers.contains("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    EXPECT_EQ(containers.at("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/systemd_pod_with_containers/kubepods.slice/"
                  "kubepods-pod11111111_1111_1111_1111_111111111111.slice/"
                  "cri-containerd-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.scope"));
}

TEST(CgroupPodDiscovery, FindContainersInPodIgnoresNonMatchingEntries)
{
    // The fixture directory also contains a plain file (cgroup.procs) and a subdirectory
    // (not-a-container-scope-dir) that doesn't carry the cri-containerd-*.scope shape.
    //
    // Scope of what this actually proves: BOTH decoys are rejected purely by NAME, at
    // MatchPodSliceName's size guard / prefix check -- not by the is_directory() type guard in
    // FindContainersInPod. "cgroup.procs" is 12 chars, below the 21-char prefix+suffix floor, so
    // it is rejected identically whether or not that type guard exists. So this test does NOT
    // cover the type guard; a regular file shaped like cri-containerd-<64 hex>.scope would be
    // needed for that, and no fixture has one.
    auto containers = atlasagent::CgroupPodDiscovery::FindContainersInPod(
        "lib/collectors/pod_monitor/test/resources/systemd_pod_with_containers/kubepods.slice/"
        "kubepods-pod11111111_1111_1111_1111_111111111111.slice");

    // Note this loop only catches a SPURIOUS entry with an unexpected id; it is blind to a
    // MISSING entry (an empty map passes vacuously). The size()==1 assertion in
    // FindContainersInPodMatchesCriContainerdScopes above is what pins the count.
    for (const auto& [id, path] : containers)
    {
        EXPECT_EQ(id, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
}

// Accumulation: every existing fixture directory yields at most ONE match, so a stray break or
// early return after either emplace would leave all of them passing. That matters most for
// containers -- a real pod almost always has at least two scopes (pause plus application), and
// dropping all but one happens before ReconcileContainers, so there is no log line to notice:
// the metrics simply never appear.
TEST(CgroupPodDiscovery, FindContainersInPodReturnsAllMatchingScopes)
{
    auto containers = atlasagent::CgroupPodDiscovery::FindContainersInPod(Pod1SlicePath("systemd_pod_with_two_containers"));

    ASSERT_EQ(containers.size(), 2);
    EXPECT_TRUE(containers.contains(ContainerId('a')));
    EXPECT_TRUE(containers.contains(ContainerId('b')));
}

TEST(CgroupPodDiscovery, FindActivePodCgroupsReturnsAllPodsInOneDirectory)
{
    atlasagent::CgroupPodDiscovery discovery{std::string(kResources) + "/systemd_two_pods"};

    auto pods = discovery.FindActivePodCgroups();

    ASSERT_EQ(pods.size(), 2);
    EXPECT_TRUE(pods.contains("22222222-2222-2222-2222-222222222222"));
    EXPECT_TRUE(pods.contains("33333333-3333-3333-3333-333333333333"));
}

TEST(CgroupPodDiscovery, FindContainersInPodMissingDirReturnsEmpty)
{
    auto containers =
        atlasagent::CgroupPodDiscovery::FindContainersInPod("lib/collectors/pod_monitor/test/resources/does_not_exist");
    EXPECT_TRUE(containers.empty());
}

// ResolvePodTags: the pure fallback-chain tag resolution, covering the primary (netflix.com/*
// annotation) tier, the app.kubernetes.io/*/k8s-app/app label fallback tier, nf.cluster's
// asymmetric primary-only gate, the redefined all-absent Gating case, and nf.node's now-purely-
// structural sourcing.
TEST(PodTagResolver, ResolvePodTagsPrimaryTierOnly)
{
    std::unordered_map<std::string, std::string> annotations{
        {"netflix.com/app", "myapp"},
        {"netflix.com/stack", "mystack"},
        {"netflix.com/detail", "mydetail"},
    };

    auto result = atlasagent::ResolvePodTags(annotations, {}, "my-pod-abc123", "");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->at("nf.app"), "myapp");
    EXPECT_EQ(result->at("nf.stack"), "mystack");
    // nf.detail's tag-emplace is currently disabled in ResolvePodTags (see its own "todo
    // uncomment later" comment) -- nf.cluster's "-mydetail" suffix still comes through since
    // BuildNfCluster reads the annotation directly, independent of this tag.
    EXPECT_FALSE(result->contains("nf.detail"));
    EXPECT_EQ(result->at("nf.cluster"), "myapp-mystack-mydetail");
    EXPECT_EQ(result->at("nf.node"), "my-pod-abc123");
    // nf.platform's tag-emplace is likewise currently disabled -- see the same comment.
    EXPECT_FALSE(result->contains("nf.platform"));
    EXPECT_FALSE(result->contains("k8s.cluster.name"));
    // nf.process is per-container, applied by the caller (TrackedPodRegistry) -- never set here.
    EXPECT_FALSE(result->contains("nf.process"));
}

TEST(PodTagResolver, ResolvePodTagsPrimaryAppOnlyClusterHasNoStackOrDetailSuffix)
{
    std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    auto result = atlasagent::ResolvePodTags(annotations, {}, "", "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.cluster"), "myapp");
    EXPECT_FALSE(result->contains("nf.stack"));
    EXPECT_FALSE(result->contains("nf.detail"));
}

TEST(PodTagResolver, ResolvePodTagsLabelFallbackTierOnly)
{
    std::unordered_map<std::string, std::string> labels{
        {"app.kubernetes.io/name", "labelapp"},
        {"app.kubernetes.io/instance", "labelstack"},
        {"app.kubernetes.io/component", "labeldetail"},
    };

    auto result = atlasagent::ResolvePodTags({}, labels, "", "");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->at("nf.app"), "labelapp");
    EXPECT_EQ(result->at("nf.stack"), "labelstack");
    // nf.detail/nf.platform's tag-emplaces are currently disabled in ResolvePodTags (see its own
    // "todo uncomment later" comment).
    EXPECT_FALSE(result->contains("nf.detail"));
    EXPECT_FALSE(result->contains("nf.platform"));
    // Asymmetric gate: nf.app resolved (via the label fallback tier, not the netflix.com/app
    // annotation), so nf.cluster must NOT be set even though nf.app is.
    EXPECT_FALSE(result->contains("nf.cluster"));
}

// Pins only that app.kubernetes.io/name wins when all three nf.app fallback labels are present.
// It deliberately does NOT pin the relative order of k8s-app vs app: with all three seeded, only
// the overall winner is observable, so swapping the last two entries of ResolvePodTags's fallback
// list (or dropping "app" entirely) would still pass this. Pinning those needs one test per
// label, each seeding that label alone.
TEST(PodTagResolver, ResolvePodTagsLabelFallbackPrefersAppNameWhenAllPresent)
{
    std::unordered_map<std::string, std::string> labels{
        {"app", "third"},
        {"k8s-app", "second"},
        {"app.kubernetes.io/name", "first"},
    };

    auto result = atlasagent::ResolvePodTags({}, labels, "", "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.app"), "first");
}

TEST(PodTagResolver, ResolvePodTagsEmptyAnnotationFallsThroughToLabelTier)
{
    // netflix.com/app present but empty must be treated as unset, per "present and non-empty".
    std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", ""}};
    std::unordered_map<std::string, std::string> labels{{"k8s-app", "fallback-app"}};

    auto result = atlasagent::ResolvePodTags(annotations, labels, "", "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.app"), "fallback-app");
    // netflix.com/app was present-but-empty, not absent -- but primary_app is still unset per
    // the "present and non-empty" rule, so nf.cluster (primary-only) must stay unset too.
    EXPECT_FALSE(result->contains("nf.cluster"));
}

TEST(PodTagResolver, ResolvePodTagsAllAbsentReturnsNullopt)
{
    EXPECT_FALSE(atlasagent::ResolvePodTags({}, {}, "", "").has_value());
    EXPECT_FALSE(atlasagent::ResolvePodTags({}, {}, "", "some-cluster").has_value());
}

// The single most important behavior change from the superseded environ-based design: nf.node
// is now always structurally available once a pod's identity resolves at all (it's the pod's own
// name, not read from environ/annotations), so it's deliberately excluded from the Gating
// decision -- including it would make Gating vacuous (every pod would always pass). A pod with a
// resolvable name but no matching netflix.com/*/app.kubernetes.io/* signal must still gate out.
TEST(PodTagResolver, ResolvePodTagsPodNameAloneStillGatesOut)
{
    auto result = atlasagent::ResolvePodTags({}, {}, "my-pod-abc123", "");
    EXPECT_FALSE(result.has_value());
}

TEST(PodTagResolver, ResolvePodTagsSetsK8sClusterNameOnlyWhenNonEmpty)
{
    std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    auto withCluster = atlasagent::ResolvePodTags(annotations, {}, "", "my-cluster");
    ASSERT_TRUE(withCluster.has_value());
    EXPECT_EQ(withCluster->at("k8s.cluster.name"), "my-cluster");

    auto withoutCluster = atlasagent::ResolvePodTags(annotations, {}, "", "");
    ASSERT_TRUE(withoutCluster.has_value());
    EXPECT_FALSE(withoutCluster->contains("k8s.cluster.name"));
}

// The unit's headline rule, and previously unpinned: no other test supplies a non-empty primary
// annotation AND a competing label for the same key, so inverting any of the three
// primary-else-fallback selections would leave every other test in this file green.
TEST(PodTagResolver, ResolvePodTagsPrimaryAnnotationsBeatCompetingLabelsForSameKey)
{
    std::unordered_map<std::string, std::string> annotations{
        {"netflix.com/app", "annapp"},
        {"netflix.com/stack", "annstack"},
        {"netflix.com/detail", "anndetail"},
    };
    std::unordered_map<std::string, std::string> labels{
        {"app.kubernetes.io/name", "labelapp"},
        {"k8s-app", "labelapp2"},
        {"app", "labelapp3"},
        {"app.kubernetes.io/instance", "labelstack"},
        {"app.kubernetes.io/component", "labeldetail"},
    };

    auto result = atlasagent::ResolvePodTags(annotations, labels, "", "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.app"), "annapp");
    EXPECT_EQ(result->at("nf.stack"), "annstack");
    // nf.detail's own tag is currently disabled, so nf.cluster is what proves the DETAIL
    // annotation also won its contest -- a label-resolved detail would be absent from the suffix.
    EXPECT_EQ(result->at("nf.cluster"), "annapp-annstack-anndetail");
}

// nf.cluster is built only from the PRIMARY netflix.com/{app,stack,detail} annotations, never
// from label-fallback values -- the deliberate asymmetry the header documents at length. Every
// pre-existing test has either empty labels or an unset primary app, so none of them has a
// primary app engaged alongside a contributing label, which is the only shape that can catch a
// change from the primary-only values to the post-fallback ones.
TEST(PodTagResolver, ResolvePodTagsNfClusterOmitsLabelFallbackSuffixes)
{
    // Primary app only; stack and detail resolve solely via label fallback.
    auto fallbackSuffixes = atlasagent::ResolvePodTags(
        {{"netflix.com/app", "myapp"}},
        {{"app.kubernetes.io/instance", "labelstack"}, {"app.kubernetes.io/component", "labeldetail"}}, "", "");
    ASSERT_TRUE(fallbackSuffixes.has_value());
    EXPECT_EQ(fallbackSuffixes->at("nf.app"), "myapp");
    EXPECT_EQ(fallbackSuffixes->at("nf.stack"), "labelstack");
    // EXACT equality: the whole point is what must be ABSENT from this string.
    EXPECT_EQ(fallbackSuffixes->at("nf.cluster"), "myapp");

    // No primary app at all: nf.app resolves via label fallback, so there must be no nf.cluster
    // even though nf.app itself is set.
    auto fallbackApp = atlasagent::ResolvePodTags({{"netflix.com/stack", "mystack"}},
                                                   {{"app.kubernetes.io/name", "labelapp"}}, "", "");
    ASSERT_TRUE(fallbackApp.has_value());
    EXPECT_EQ(fallbackApp->at("nf.app"), "labelapp");
    EXPECT_EQ(fallbackApp->at("nf.stack"), "mystack");
    EXPECT_FALSE(fallbackApp->contains("nf.cluster"));
}

// Gating passes on ANY ONE of nf.app/nf.stack/nf.detail. Only the app case was covered, so
// narrowing the check to app-only would still pass every other test here -- while causing a total
// metric blackout for any pod identified by stack or detail alone.
TEST(PodTagResolver, ResolvePodTagsStackAloneOrDetailAlonePassesGating)
{
    // Stack alone, via its primary annotation.
    auto stackOnly = atlasagent::ResolvePodTags({{"netflix.com/stack", "mystack"}}, {}, "", "");
    ASSERT_TRUE(stackOnly.has_value());
    EXPECT_EQ(stackOnly->at("nf.stack"), "mystack");
    EXPECT_FALSE(stackOnly->contains("nf.app"));
    EXPECT_FALSE(stackOnly->contains("nf.cluster"));

    // Detail alone. nf.detail's own tag is disabled, so the observable result is just that the pod
    // is NOT gated out and still gets its structural nf.node.
    auto detailOnly = atlasagent::ResolvePodTags({{"netflix.com/detail", "mydetail"}}, {}, "my-pod", "");
    ASSERT_TRUE(detailOnly.has_value());
    EXPECT_EQ(detailOnly->at("nf.node"), "my-pod");

    // The app.kubernetes.io/component fallback has no other observable effect anywhere, so this
    // is the only test that makes it load-bearing: it is the sole reason this pod is not gated
    // out, and the returned map is legitimately EMPTY (no app/stack, nf.detail disabled, no
    // primary app so no cluster, no pod name, no K8S_CLUSTER).
    auto componentLabelOnly = atlasagent::ResolvePodTags({}, {{"app.kubernetes.io/component", "labeldetail"}}, "", "");
    ASSERT_TRUE(componentLabelOnly.has_value());
    EXPECT_TRUE(componentLabelOnly->empty());
}

// End-to-end wiring test through RefreshTrackedPods(): the pod in systemd_pod_with_containers is
// discovered structurally AND so is its container scope, but this file's hermetic setup means
// FindActivePodInfo()'s identity lookup always fails closed -- so info.annotations/info.labels
// are empty, ResolvePodTags returns nullopt, and the container is never tracked.
//
// WHAT THIS DOES NOT PROVE: it is not a test of the Gating logic. Two independent causes produce
// this same empty container map here -- Gating rejecting the pod, and the container simply not
// being in the (always-empty) kubelet-reported info.containers -- and this assertion cannot tell
// them apart. It passes with Gating's container-clearing statement working, deleted, or inverted.
// It pins only the outer wiring: that a pod with an unresolved identity is still TRACKED as a pod
// while emitting nothing for any container. See the NOTE above this test group for how to
// actually cover Gating (drive TrackedPodRegistry::Refresh directly with a fabricated PodInfoMap).
TEST(PodMonitor, RefreshTrackedPodsContainerNotTrackedWhenPodIdentityUnresolved)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd_pod_with_containers"};

    podMonitor.RefreshTrackedPods();

    ASSERT_TRUE(podMonitor.TrackedPods().contains("11111111-1111-1111-1111-111111111111"));
    EXPECT_TRUE(podMonitor.TrackedPods().at("11111111-1111-1111-1111-111111111111").containers.empty());
}

// ParseCpuQuantity: the CPU subset of a Kubernetes resource Quantity. Only source for a
// container's CPU REQUEST -- the cgroup filesystem exposes cpu.max (the limit), never the request.
TEST(CpuQuantity, ParseCpuQuantityAcceptsMillicpuAndDecimalCores)
{
    // EXPECT_DOUBLE_EQ rather than EXPECT_EQ: the millicpu path divides by 1000, and whether e.g.
    // 100.0/1000.0 is bit-identical to the literal 0.1 is a subtlety no test should rest on.
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

// These two are the reason ParseCpuQuantity checks that from_chars consumed the WHOLE input.
// from_chars stops at the first character it cannot use instead of failing, so without that check
// these parse as 0.5 and 5 -- wrong by 10x and 10x respectively, silently. A full-consumption
// check is not used anywhere else in this repo, so it is easy to drop in a refactor; this test
// exists to make that break loudly.
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

// ---------------------------------------------------------------------------------------------
// TrackedPodRegistry: the gated-IN path. Everything below drives Refresh(const PodInfoMap&)
// directly with a fabricated PodInfoMap, which is what lets these tests exercise
// ReconcileContainers / EvictUntrackedContainers / the Gating clear / SetExtraTags /
// SetCpuCountOverride -- none of which any PodMonitor-routed test can reach, because
// FindActivePodInfo()'s kubelet lookup always fails closed here (see the NOTE further up).
//
// WRITER DISCIPLINE, load-bearing: the writer reached by WriterTestHelper::GetImpl() is a
// process-wide singleton shared by every test in this binary, so each test below Clear()s it
// immediately before the Emit* call it asserts on -- never relying on it starting empty. GetImpl()
// is also fetched only AFTER the Registry is constructed, matching cgroup_test.cpp's convention,
// and these tests use ONE Registry each: where fresh tracking state is needed they construct a
// second TrackedPodRegistry sharing that Registry rather than a second Registry.
// ---------------------------------------------------------------------------------------------

TEST(TrackedPodRegistry, GatingClearsTrackedContainersWhenIdentityLost)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::TrackedPodRegistry registry{&r};

    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const auto container_id = ContainerId('a');

    // Cycle 1: netflix.com/app resolves, so Gating passes and the container is tracked.
    registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, {{container_id, "main"}}, {{"netflix.com/app", "myapp"}}));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    // Cycle 2: byte-identical input except the annotation is gone (a relabel/rollout), so
    // ResolvePodTags returns nullopt. The pod must stay tracked while every container it was
    // emitting for is evicted -- otherwise those containers keep publishing under the stale
    // nf.app/nf.cluster tags SetExtraTags gave them, since SetExtraTags is only reached on the
    // gated-in path. Deleting the container-clearing statement makes this assertion fail.
    registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, {{container_id, "main"}}, {}));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    EXPECT_TRUE(registry.TrackedPods().at(kPod1Uid).containers.empty());
}

TEST(TrackedPodRegistry, EvictsContainerWhoseCgroupScopeDisappears)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::TrackedPodRegistry registry{&r};

    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};
    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};

    // Cycle 1: the pod's cgroup_path has a real container scope, so it gets tracked.
    registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd_pod_with_containers"), containers, annotations));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    // Cycle 2: same uid and same kubelet-reported container, but cgroup_path now points at a pod
    // slice with NO container scopes at all -- i.e. the scope directory vanished. Gating still
    // passes, so this isolates EvictUntrackedContainers rather than the Gating clear.
    registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd"), containers, annotations));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    EXPECT_TRUE(registry.TrackedPods().at(kPod1Uid).containers.empty());
}

TEST(TrackedPodRegistry, SkipsWithoutEvictingContainerNotYetReportedByKubelet)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    atlasagent::TrackedPodRegistry registry{&r};

    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // The container's cgroup scope exists on disk but kubelet hasn't reported it yet (empty
    // container list) -- the documented transient race. It must not be tracked...
    registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, {}, annotations));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    EXPECT_TRUE(registry.TrackedPods().at(kPod1Uid).containers.empty());

    // ...and once kubelet does report it, the next cycle picks it up with no intervening restart.
    registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, {{ContainerId('a'), "main"}}, annotations));
    EXPECT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
}

// Both container scopes in this fixture carry real memory.current/max/stat/events/swap files ON
// PURPOSE -- do not strip them. CGroup's memory readers use unordered_map::operator[], which
// INSERTS a zero for any absent key, so a scope with no memory files still emits a full set of
// fabricated zeroes. This test would therefore still "pass" with an empty fixture, but only by
// leaning on that behavior; real values keep it honest and independent of it.
TEST(TrackedPodRegistry, PerContainerTagsReachEmittedLinesWithSharedPodTags)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    atlasagent::TrackedPodRegistry registry{&r};

    registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd_pod_with_two_containers"),
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

    // ...while BOTH carry the pod-level tags. This is the assertion that matters: ReconcileContainers
    // copies pod_tags per iteration and std::move()s that copy into SetExtraTags, so hoisting the
    // copy out of the loop as an "optimization" would leave every container after the first with an
    // empty tag map -- metrics that still publish, but silently unattributable to any app.
    EXPECT_TRUE(AnyLineContainsAll(messages, {"nf.process=main", "nf.app=myapp", "nf.stack=mystack"}));
    EXPECT_TRUE(AnyLineContainsAll(messages, {"nf.process=sidecar", "nf.app=myapp", "nf.stack=mystack"}));
}

TEST(TrackedPodRegistry, InjectsK8sNamespaceNameOnlyWhenNamespaceKnown)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // A known namespace becomes the k8s.namespace.name tag. ResolvePodTags never sets this tag --
    // TrackedPodRegistry injects it -- so no PodTagResolver test can cover it.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, containers, annotations, "pod-one", "ns-one"));
        memoryWriter->Clear();
        registry.EmitMemoryStats();
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContains(messages, "k8s.namespace.name=ns-one"));
    }

    // An unknown namespace must omit the tag entirely rather than emit it empty. Matching the
    // exact key (not a loose "k8s.") matters -- k8s.cluster.name would otherwise match too.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, containers, annotations, "", ""));
        memoryWriter->Clear();
        registry.EmitMemoryStats();
        auto messages = memoryWriter->GetMessages();
        ASSERT_FALSE(messages.empty());
        EXPECT_FALSE(AnyLineContains(messages, "k8s.namespace.name"));
    }

    // The tag reads the SELF-HEALED identity, not this cycle's raw input: a cycle whose kubelet
    // lookup failed (blank name/namespace) must keep publishing the namespace learned earlier
    // rather than silently dropping the tag for a minute.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, containers, annotations, "pod-one", "ns-one"));
        registry.Refresh(OnePodInfoMap(kPod1Uid, cgroup_path, containers, annotations, "", ""));
        memoryWriter->Clear();
        registry.EmitMemoryStats();
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContains(messages, "k8s.namespace.name=ns-one"));
    }
}

TEST(TrackedPodRegistry, ResolvesCpuCountFromQuotaThenFallsBackToSysconf)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // cpu.max "50000 100000" -> quota/period == 0.5. sys.cpu.numProcessors is a Gauge (always
    // written, unlike a Counter -- no zero-delta trap), and carries the resolved count.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd_single_pod_with_quota"), containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
    }

    // cpu.max "max 100000" is the unlimited case: QuotaCpuCount returns nullopt and the count
    // falls back to the node's online CPU count (an unquotaed container can burst across all of
    // them). Computed here the same way ResolveCpuCountForPod does.
    {
        const auto expected = ":" + std::to_string(static_cast<double>(sysconf(_SC_NPROCESSORS_ONLN)));
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd_pod_cpu_unlimited"), containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);
        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", expected}));
    }
}

// k8s.cpu.requested must carry the DECLARED request, not the cpu.max limit. This is the whole
// point of the request plumbing: before it, both gauges published the same limit-derived number,
// so a container's request was indistinguishable from its limit. The fixture's cpu.max is
// "50000 100000" (0.5 cores) while the declared request here is
// 250m, so the two gauges MUST disagree -- if they ever print the same value again, the request
// has stopped being threaded through.
TEST(TrackedPodRegistry, RequestedGaugeReportsDeclaredRequestNotTheLimit)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    atlasagent::TrackedPodRegistry registry{&r};

    registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd_single_pod_with_quota"),
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

// A container the pod spec gives no CPU request (BestEffort) must publish NO k8s.cpu.requested
// at all -- not the limit, and not a zero that would turn every utilization/requested division in
// a dashboard into inf. The capacity gauge is unaffected.
TEST(TrackedPodRegistry, RequestedGaugeOmittedForContainerWithNoCpuRequest)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    atlasagent::TrackedPodRegistry registry{&r};

    // Same fixture and container as above, but cpu_requests is empty.
    registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd_single_pod_with_quota"),
                                    {{ContainerId('a'), "main"}}, {{"netflix.com/app", "myapp"}}));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    auto messages = memoryWriter->GetMessages();

    EXPECT_FALSE(AnyLineContains(messages, "k8s.cpu.requested"));
    EXPECT_FALSE(AnyLineContains(messages, "titus.cpu.requested"));
    EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
}

TEST(TrackedPodRegistry, ReResolvesCpuCountEveryRefreshCycle)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    atlasagent::TrackedPodRegistry registry{&r};

    // A checked-in fixture cannot express this: the point is that an ALREADY-TRACKED container
    // picks up a changed cpu.max. try_emplace won't rebuild the CGroup (its path is fixed at first
    // insertion), so the quota has to be rewritten underneath the same path -- exactly what an
    // in-place vertical resize does on a live node.
    TempCgroupTree tree{"cpu_resize"};
    tree.WriteScopeFile("cpu.max", "50000 100000\n");

    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    registry.Refresh(OnePodInfoMap(kPod1Uid, tree.PodPath(), containers, annotations));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    auto messages = memoryWriter->GetMessages();
    ASSERT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));

    // Resize to 2 CPUs and refresh again. Re-resolving only at first insertion would keep
    // publishing 0.5 here, understating the container's capacity for the rest of the process.
    tree.WriteScopeFile("cpu.max", "200000 100000\n");
    registry.Refresh(OnePodInfoMap(kPod1Uid, tree.PodPath(), containers, annotations));
    memoryWriter->Clear();
    registry.EmitCpuStats(true, true);
    messages = memoryWriter->GetMessages();
    EXPECT_TRUE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":2.000000"}));
    EXPECT_FALSE(AnyLineContainsAll(messages, {"sys.cpu.numProcessors", ":0.500000"}));
}

// Regression guard for the noexcept/out-of-bounds defects fixed in cgroup.cpp: CpuUtilizationV2
// and CpuPeakUtilizationV2 read cpu.stat keys, and GetAvailCpuTime indexes cpu.max's parsed
// fields. Both are noexcept, and both are reachable with those files absent -- CollectCpuStats
// runs every second while the tracked set is only refreshed every 60s, so a container that exits
// stays tracked (with a vanished cgroup directory) for up to a minute.
//
// Deliberately NOT a death test: before the fix the cpu.max path was an out-of-bounds read (UB),
// not a clean throw, and pinning a regression test to UB is not meaningful. This asserts the
// post-fix contract instead -- return cleanly, and emit nothing that depends on the missing data.
TEST(TrackedPodRegistry, EmitCpuStatsSurvivesMissingAndPartialCpuStat)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

    const std::unordered_map<std::string, std::string> containers{{ContainerId('a'), "main"}};
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // Case A: the container scope has no cpu.stat and no cpu.max at all.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, Pod1SlicePath("systemd_pod_with_containers"), containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);

        auto messages = memoryWriter->GetMessages();
        // Emission still happened -- without these the absence assertions below would all be
        // satisfied by "nothing ran at all", and a regression that hoisted the cpu.stat guard
        // above these two gauges (silently killing them for every pod) would go unnoticed. They
        // depend only on the resolved CPU count, not on cpu.stat, so they survive here by design.
        EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.numProcessors"));
        // No "requested" gauge at all: these PodInfoMaps declare no CPU request, and a pod
        // container without one omits it rather than publishing the limit (or the node's core
        // count) as though it were the request. titus.cpu.requested is asserted absent too --
        // that name belongs to the Titus path and must never appear on pod metrics.
        EXPECT_FALSE(AnyLineContains(messages, "k8s.cpu.requested"));
        EXPECT_FALSE(AnyLineContains(messages, "titus.cpu.requested"));
        // The cpu.stat-derived metrics must be absent rather than garbage.
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.utilization"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.peakUtilization"));
        EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.usageTime"));
        EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.processingTime"));
    }

    // Case B: cpu.stat EXISTS but is missing the keys these functions read -- a present-but-partial
    // file, which a whole-file existence check would have let through.
    {
        TempCgroupTree tree{"partial_cpu_stat"};
        tree.WriteScopeFile("cpu.stat", "usage_usec 1000\n");

        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, tree.PodPath(), containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);

        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.numProcessors"));
        // No "requested" gauge at all: these PodInfoMaps declare no CPU request, and a pod
        // container without one omits it rather than publishing the limit (or the node's core
        // count) as though it were the request. titus.cpu.requested is asserted absent too --
        // that name belongs to the Titus path and must never appear on pod metrics.
        EXPECT_FALSE(AnyLineContains(messages, "k8s.cpu.requested"));
        EXPECT_FALSE(AnyLineContains(messages, "titus.cpu.requested"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.utilization"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.peakUtilization"));
        EXPECT_FALSE(AnyLineContains(messages, "cgroup.cpu.usageTime"));
    }

    // Case C: cpu.stat is COMPLETE but cpu.max is absent, so GetAvailCpuTime returns 0. Cases A
    // and B both bail out at the cpu.stat key guard and never reach the divide-by-zero gate, so
    // without this case that third part of the fix has no coverage at all.
    //
    // TWO cycles are required: on the first, the prev_* baselines are still -1, so the gauges are
    // skipped for an unrelated reason. Only on the second -- with a real delta available -- is the
    // avail_cpu_time gate the deciding factor, and removing it would publish secs/0.
    {
        TempCgroupTree tree{"complete_cpu_stat_no_cpu_max"};
        tree.WriteScopeFile("cpu.stat", "usage_usec 1000\nuser_usec 400\nsystem_usec 600\n");

        atlasagent::TrackedPodRegistry registry{&r};
        registry.Refresh(OnePodInfoMap(kPod1Uid, tree.PodPath(), containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
        registry.EmitCpuStats(true, true);  // seeds the prev_* baselines; asserted on below

        // Advance the counters so the second cycle has a non-zero delta to divide.
        tree.WriteScopeFile("cpu.stat", "usage_usec 5000\nuser_usec 2400\nsystem_usec 2600\n");
        memoryWriter->Clear();
        registry.EmitCpuStats(true, true);

        auto messages = memoryWriter->GetMessages();
        EXPECT_TRUE(AnyLineContains(messages, "sys.cpu.numProcessors"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.utilization"));
        EXPECT_FALSE(AnyLineContains(messages, "sys.cpu.peakUtilization"));
    }
}

// The complement of EmitCpuStatsSurvivesMissingAndPartialCpuStat above: there the scope directory
// exists with files missing; here the whole directory is gone, i.e. a container that terminated
// since the last Refresh(). Refresh() does evict it (EvictUntrackedContainers), but Refresh() runs
// on the 60s cadence while EmitCpuStats runs every second, so it stays tracked for up to a minute.
//
// Emitting for it is not harmless. nf.app/nf.cluster are POD-level, so a dead container's output
// lands under the live app's tags -- and cgroup.cpu.processingCapacity is the real damage: it reads
// no files at all (pure delta_t * cpuCount), so nothing about the vanished cgroup stops it, and it
// is a Counter, so ~60s of phantom capacity is permanently accumulated into the pod's own
// utilization denominator. sys.cpu.numProcessors and k8s.cpu.requested leak the same way, because
// CpuUtilizationV2 emits both BEFORE its cpu.stat guard.
//
// Asserting emission BEFORE the removal is what keeps this honest: without that baseline, every
// absence assertion below would also be satisfied by "nothing ran at all".
TEST(TrackedPodRegistry, SkipsEmissionForContainerWhoseCgroupVanishedSinceRefresh)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    atlasagent::TrackedPodRegistry registry{&r};

    TempCgroupTree tree{"vanished_scope"};
    tree.WriteScopeFile("cpu.max", "50000 100000\n");
    tree.WriteScopeFile("memory.current", "1048576\n");

    registry.Refresh(OnePodInfoMap(kPod1Uid, tree.PodPath(), {{ContainerId('a'), "main"}},
                                    {{"netflix.com/app", "myapp"}}, "pod-one", "ns-one", {{"main", 0.25}}));
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

    // The container terminates: containerd removes the entire scope directory. It stays TRACKED,
    // since only Refresh() changes membership and it has not run again -- so this exercises the
    // liveness skip, not eviction.
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
    // Memory would otherwise publish fabricated zeros from the now-absent files, since cgroup.cpp
    // reads memory.stat/memory.events keys with operator[] rather than find().
    //
    // DO NOT trim the CPU assertions above on the grounds that these memory ones cover it. Once
    // the operator[] reads are guarded, these two hold whether or not the liveness skip exists --
    // the files are gone either way -- so they stop being evidence of anything. Only
    // cgroup.cpu.processingCapacity can prove the skip: it reads NO files at all, so its absence
    // is attributable to nothing but the guard.
    EXPECT_FALSE(AnyLineContains(messages, "cgroup.mem."));
    EXPECT_FALSE(AnyLineContains(messages, "mem.cached"));
    EXPECT_TRUE(messages.empty());

    // NOTE: this does not independently cover EmitIOStats' guard. IOStats() emits nothing for a
    // LIVE container here either (no io.stat in the tree, so ParseIOLines yields nothing), so its
    // contribution to the emptiness assertion above would hold with or without the skip. The guard
    // is there for the same reason as the other two; proving it needs an io.stat fixture.
}

TEST(TrackedPodRegistry, EmitMethodsAreNoOpsWithNothingTracked)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
    atlasagent::TrackedPodRegistry registry{&r};

    // Every Emit* is called on the agent's normal cadence regardless of whether anything is
    // tracked yet -- k8s-agent.cpp drives CollectCpuStats every second from process start.
    memoryWriter->Clear();
    EXPECT_NO_FATAL_FAILURE(registry.EmitCpuStats(true, true));
    EXPECT_NO_FATAL_FAILURE(registry.EmitIOStats());
    EXPECT_NO_FATAL_FAILURE(registry.EmitMemoryStats());
    EXPECT_TRUE(memoryWriter->GetMessages().empty());
}

TEST(PodMonitor, RefreshTrackedPodsEvictsAllWhenRootDisappears)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd"};

    podMonitor.RefreshTrackedPods();
    ASSERT_EQ(podMonitor.TrackedPods().size(), 3);

    // Point at a nonexistent root, so FindActivePodInfo() discovers nothing.
    podMonitor.SetPrefix("lib/collectors/pod_monitor/test/resources/does_not_exist");
    podMonitor.RefreshTrackedPods();

    EXPECT_TRUE(podMonitor.TrackedPods().empty());
}

TEST(PodIdentityClient, ParsePodListWellFormed)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "namespace-one"
      }
    },
    {
      "metadata": {
        "uid": "22222222-2222-2222-2222-222222222222",
        "name": "pod-two",
        "namespace": "namespace-two"
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2);

    EXPECT_EQ(result->at("11111111-1111-1111-1111-111111111111").name, "pod-one");
    EXPECT_EQ(result->at("11111111-1111-1111-1111-111111111111").pod_namespace, "namespace-one");

    EXPECT_EQ(result->at("22222222-2222-2222-2222-222222222222").name, "pod-two");
    EXPECT_EQ(result->at("22222222-2222-2222-2222-222222222222").pod_namespace, "namespace-two");
}

TEST(PodIdentityClient, ParsePodListMalformedJsonFails)
{
    auto result = PodIdentityClientTest::ParsePodList("not json{{{");
    EXPECT_FALSE(result.has_value());
}

TEST(PodIdentityClient, ParsePodListMissingItemsFails)
{
    auto result = PodIdentityClientTest::ParsePodList(R"json({"kind":"PodList"})json");
    EXPECT_FALSE(result.has_value());
}

TEST(PodIdentityClient, ParsePodListEmptyItemsSucceeds)
{
    auto result = PodIdentityClientTest::ParsePodList(R"json({"kind":"PodList","items":[]})json");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(PodIdentityClient, ParsePodListAllItemsMalformedSucceeds)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    { "metadata": { "uid": "11111111-1111-1111-1111-111111111111" } },
    "not-an-object"
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(PodIdentityClient, ParsePodListSkipsMalformedEntry)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "namespace-one"
      }
    },
    {
      "metadata": {
        "uid": "22222222-2222-2222-2222-222222222222",
        "name": "pod-two"
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);

    EXPECT_EQ(result->at("11111111-1111-1111-1111-111111111111").name, "pod-one");
    EXPECT_EQ(result->at("11111111-1111-1111-1111-111111111111").pod_namespace, "namespace-one");
}

TEST(PodIdentityClient, ParsePodListParsesAnnotationsAndLabels)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "namespace-one",
        "annotations": { "netflix.com/app": "myapp", "netflix.com/stack": "mystack" },
        "labels": { "app.kubernetes.io/name": "myapp", "k8s-app": "legacy-name" }
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);

    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");
    EXPECT_EQ(identity.annotations.at("netflix.com/app"), "myapp");
    EXPECT_EQ(identity.annotations.at("netflix.com/stack"), "mystack");
    EXPECT_EQ(identity.labels.at("app.kubernetes.io/name"), "myapp");
    EXPECT_EQ(identity.labels.at("k8s-app"), "legacy-name");
}

TEST(PodIdentityClient, ParsePodListMissingAnnotationsOrLabelsLeavesMapsEmpty)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "namespace-one"
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);

    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");
    EXPECT_TRUE(identity.annotations.empty());
    EXPECT_TRUE(identity.labels.empty());
}

TEST(PodIdentityClient, ParsePodListSkipsNonStringAnnotationValues)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "namespace-one",
        "annotations": { "netflix.com/app": "myapp", "netflix.com/weird": 123 }
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");
    EXPECT_EQ(identity.annotations.at("netflix.com/app"), "myapp");
    EXPECT_FALSE(identity.annotations.contains("netflix.com/weird"));
}

// status.containerStatuses parsing had NO coverage at all -- "containerStatuses" appeared nowhere
// in this file and no test asserted on PodIdentity::containers. That map is what
// ReconcileContainers matches cgroup-discovered ids against, so a parsing regression here gates
// out every container on the node while leaving pods tracked and every other test green.
TEST(PodIdentityClient, ParsePodListParsesContainerStatusesAndStripsIdScheme)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "ns-one"
      },
      "status": {
        "containerStatuses": [
          {"name": "main", "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
          {"name": "sidecar", "containerID": "cri-o://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
          {"name": "double-scheme", "containerID": "docker://x://cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"},
          {"name": "bare", "containerID": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"}
        ]
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");
    ASSERT_EQ(identity.containers.size(), 4);
    // The runtime scheme prefix must be stripped, so the key matches the bare hex id the cgroup
    // scope directory name carries.
    EXPECT_EQ(identity.containers.at(ContainerId('a')), "main");
    EXPECT_EQ(identity.containers.at(ContainerId('b')), "sidecar");
    // Strips at the FIRST "://", not the last -- this entry is what distinguishes the two, and
    // a switch to find-last would key this container as the bare c's instead.
    EXPECT_EQ(identity.containers.at("x://" + std::string(60, 'c')), "double-scheme");
    // No scheme at all: passed through unchanged rather than mangled.
    EXPECT_EQ(identity.containers.at(ContainerId('d')), "bare");
}

// A NATIVE SIDECAR -- an initContainer with restartPolicy=Always (k8s 1.28+) -- is reported in
// status.initContainerStatuses, NOT status.containerStatuses, and runs for the pod's whole
// lifetime with its own cgroup scope. While that array went unparsed, such a container was
// discovered on disk every cycle and skipped every cycle: forever, and silently. Zero CPU, memory
// and IO metrics for the entire mesh-proxy / log-forwarder tier, while the pod and its main
// container looked perfectly healthy.
//
// Parsing spec.initContainers[] (which ParsePodListParsesCpuRequestsFromSpec covers) is NOT the
// same fix and does not substitute for this one: spec carries container names but no ids, because
// an id is assigned when the runtime creates the container. status is the only source of ids, and
// ReconcileContainers resolves cgroup-discovered scopes by id.
TEST(PodIdentityClient, ParsePodListParsesInitContainerStatusesForNativeSidecars)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "ns-one"
      },
      "spec": {
        "initContainers": [
          {"name": "envoy", "restartPolicy": "Always", "resources": {"requests": {"cpu": "50m"}}}
        ],
        "containers": [
          {"name": "app", "resources": {"requests": {"cpu": "500m"}}}
        ]
      },
      "status": {
        "initContainerStatuses": [
          {"name": "envoy", "containerID": "containerd://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}
        ],
        "containerStatuses": [
          {"name": "app", "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}
        ]
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");

    // Both status arrays land in the SAME map, keyed by id -- ids are unique per container, so
    // merging them cannot collide. Dropping the initContainerStatuses call makes this size 1.
    ASSERT_EQ(identity.containers.size(), 2);
    EXPECT_EQ(identity.containers.at(ContainerId('a')), "app");
    EXPECT_EQ(identity.containers.at(ContainerId('b')), "envoy");

    // The sidecar's CPU request is now REACHABLE. It was already parsed from spec.initContainers[]
    // before this fix, but ReconcileContainers looks cpu_requests up by NAME and obtains that name
    // only via the id lookup above -- so a correctly-parsed 50m sat permanently unread.
    EXPECT_DOUBLE_EQ(identity.cpu_requests.at("envoy"), 0.05);
    EXPECT_DOUBLE_EQ(identity.cpu_requests.at("app"), 0.5);
}

// A container still in `waiting` (ImagePullBackOff, CreateContainerError) is reported with an
// EMPTY containerID rather than none at all, so it passes the IsString() check. Keying the map on
// "" would break its documented contract of holding bare hex ids, and -- worse -- every
// not-yet-started container in the pod would contend for that single "" entry.
TEST(PodIdentityClient, ParsePodListSkipsContainerStatusWithEmptyContainerId)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "ns-one"
      },
      "status": {
        "containerStatuses": [
          {"name": "pending", "containerID": ""},
          {"name": "running", "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}
        ],
        "initContainerStatuses": [
          {"name": "init-pending", "containerID": ""}
        ]
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");

    // Only the started container survives. TWO entries carry an empty containerID on purpose, one
    // per status array: without the guard both would target the same "" key, emplace would keep
    // whichever arrived first, and this map would be size 2 with a junk entry.
    ASSERT_EQ(identity.containers.size(), 1);
    EXPECT_EQ(identity.containers.at(ContainerId('a')), "running");
    EXPECT_FALSE(identity.containers.contains(""));
}

// spec.containers[].resources.requests.cpu is the only source for a container's CPU request --
// the cgroup filesystem carries cpu.max (the limit) instead. spec was fetched but never parsed
// before this, so nothing here had coverage.
//
// Also covers initContainers[]: a native sidecar (restartPolicy=Always) runs for the pod's whole
// lifetime and gets its own cgroup scope, so leaving that array unparsed would make exactly the
// mesh-proxy / log-forwarder tier permanently unattributed.
TEST(PodIdentityClient, ParsePodListParsesCpuRequestsFromSpec)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "ns-one"
      },
      "spec": {
        "initContainers": [
          {"name": "sidecar", "resources": {"requests": {"cpu": "50m", "memory": "64Mi"}}}
        ],
        "containers": [
          {"name": "main", "resources": {"requests": {"cpu": "500m"}, "limits": {"cpu": "2"}}},
          {"name": "besteffort", "resources": {}},
          {"name": "limits-only", "resources": {"limits": {"cpu": "1"}}},
          {"name": "no-resources-key"},
          {"name": "unparseable", "resources": {"requests": {"cpu": "not-a-number"}}}
        ]
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");

    // Only the containers that actually declare a parseable requests.cpu appear.
    ASSERT_EQ(identity.cpu_requests.size(), 2);
    EXPECT_DOUBLE_EQ(identity.cpu_requests.at("main"), 0.5);
    // From initContainers[], not containers[].
    EXPECT_DOUBLE_EQ(identity.cpu_requests.at("sidecar"), 0.05);

    // Every "no request" shape must be ABSENT rather than present-with-zero: absence is what makes
    // the emission omit k8s.cpu.requested instead of publishing a misleading 0.
    EXPECT_FALSE(identity.cpu_requests.contains("besteffort"));
    EXPECT_FALSE(identity.cpu_requests.contains("limits-only"));
    EXPECT_FALSE(identity.cpu_requests.contains("no-resources-key"));
    EXPECT_FALSE(identity.cpu_requests.contains("unparseable"));
}

// A pod with no spec at all (or no containers array) must parse cleanly with an empty map rather
// than failing the whole pod -- matching how this parser treats every other optional section.
TEST(PodIdentityClient, ParsePodListMissingSpecLeavesCpuRequestsEmpty)
{
    auto json = R"json(
{
  "kind": "PodList",
  "items": [
    {
      "metadata": {
        "uid": "11111111-1111-1111-1111-111111111111",
        "name": "pod-one",
        "namespace": "ns-one"
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");
    EXPECT_EQ(identity.name, "pod-one");
    EXPECT_TRUE(identity.cpu_requests.empty());
}

TEST(PodMonitor, JoinCgroupAndIdentityPartialMatch)
{
    atlasagent::PodCgroupMap cgroup_pods;
    cgroup_pods.emplace("11111111-1111-1111-1111-111111111111", std::filesystem::path("/sys/fs/cgroup/pod-one"));
    cgroup_pods.emplace("22222222-2222-2222-2222-222222222222", std::filesystem::path("/sys/fs/cgroup/pod-two"));

    atlasagent::PodIdentityMap identities;
    identities.emplace(
        "11111111-1111-1111-1111-111111111111",
        atlasagent::PodIdentity{"pod-one", "namespace-one", {}, {{"netflix.com/app", "myapp"}},
                                 {{"app.kubernetes.io/name", "mylabelapp"}}});

    auto result = PodMonitorTest::JoinCgroupAndIdentity(cgroup_pods, std::optional(identities));

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").name, "pod-one");
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").pod_namespace, "namespace-one");
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").cgroup_path,
              std::filesystem::path("/sys/fs/cgroup/pod-one"));
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").annotations.at("netflix.com/app"), "myapp");
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").labels.at("app.kubernetes.io/name"), "mylabelapp");

    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").name, "");
    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").pod_namespace, "");
    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").cgroup_path,
              std::filesystem::path("/sys/fs/cgroup/pod-two"));
    EXPECT_TRUE(result.at("22222222-2222-2222-2222-222222222222").annotations.empty());
    EXPECT_TRUE(result.at("22222222-2222-2222-2222-222222222222").labels.empty());
}

TEST(PodMonitor, JoinCgroupAndIdentityNulloptIdentities)
{
    atlasagent::PodCgroupMap cgroup_pods;
    cgroup_pods.emplace("11111111-1111-1111-1111-111111111111", std::filesystem::path("/sys/fs/cgroup/pod-one"));
    cgroup_pods.emplace("22222222-2222-2222-2222-222222222222", std::filesystem::path("/sys/fs/cgroup/pod-two"));

    auto result = PodMonitorTest::JoinCgroupAndIdentity(cgroup_pods, std::nullopt);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").name, "");
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").pod_namespace, "");
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").cgroup_path,
              std::filesystem::path("/sys/fs/cgroup/pod-one"));

    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").name, "");
    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").pod_namespace, "");
    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").cgroup_path,
              std::filesystem::path("/sys/fs/cgroup/pod-two"));
}

TEST(PodMonitor, JoinCgroupAndIdentityDropsIdentityWithoutCgroup)
{
    atlasagent::PodCgroupMap cgroup_pods;
    cgroup_pods.emplace("11111111-1111-1111-1111-111111111111", std::filesystem::path("/sys/fs/cgroup/pod-one"));

    atlasagent::PodIdentityMap identities;
    identities.emplace("11111111-1111-1111-1111-111111111111",
                        atlasagent::PodIdentity{"pod-one", "namespace-one"});
    identities.emplace("99999999-9999-9999-9999-999999999999",
                        atlasagent::PodIdentity{"pod-without-cgroup", "namespace-ghost"});

    auto result = PodMonitorTest::JoinCgroupAndIdentity(cgroup_pods, std::optional(identities));

    ASSERT_EQ(result.size(), cgroup_pods.size());
    EXPECT_EQ(result.find("99999999-9999-9999-9999-999999999999"), result.end());
}

TEST(PodMonitor, FindActivePodInfoWithoutKubeletIsHermetic)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd"};

    auto pods = podMonitor.FindActivePodInfo();

    ASSERT_EQ(pods.size(), 3);

    EXPECT_EQ(pods.at("11111111-1111-1111-1111-111111111111").cgroup_path,
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/systemd/kubepods.slice/"
                  "kubepods-pod11111111_1111_1111_1111_111111111111.slice"));
    EXPECT_EQ(pods.at("22222222-2222-2222-2222-222222222222").cgroup_path,
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/systemd/kubepods.slice/kubepods-burstable.slice/"
                  "kubepods-burstable-pod22222222_2222_2222_2222_222222222222.slice"));
    EXPECT_EQ(pods.at("33333333-3333-3333-3333-333333333333").cgroup_path,
              std::filesystem::path(
                  "lib/collectors/pod_monitor/test/resources/systemd/kubepods.slice/kubepods-besteffort.slice/"
                  "kubepods-besteffort-pod33333333_3333_3333_3333_333333333333.slice"));

    for (const auto& [uid, info] : pods)
    {
        EXPECT_EQ(info.name, "");
        EXPECT_EQ(info.pod_namespace, "");
    }
}

// PodInfo::uid and PodInfo::containers were read by no test at all, so the two lines of
// JoinCgroupAndIdentity that populate them could be deleted or mis-wired with everything else
// still green. containers in particular is what ReconcileContainers matches against, so losing
// it here gates out every container on the node.
TEST(PodMonitor, JoinCgroupAndIdentityCopiesContainersAndUid)
{
    atlasagent::PodCgroupMap cgroup_pods;
    cgroup_pods.emplace("uid-one", "/sys/fs/cgroup/pod-one");
    cgroup_pods.emplace("uid-two", "/sys/fs/cgroup/pod-two");

    atlasagent::PodIdentityMap identities;
    identities.emplace("uid-one", atlasagent::PodIdentity{"pod-one",
                                                           "namespace-one",
                                                           {{"abc123", "sidecar-name"}},
                                                           {{"netflix.com/app", "myapp"}},
                                                           {{"k8s-app", "mylabelapp"}}});

    auto result = PodMonitorTest::JoinCgroupAndIdentity(cgroup_pods, identities);

    ASSERT_EQ(result.size(), 2);

    // Matched pod: every identity-sourced field is copied through, keyed by the SAME uid.
    const auto& matched = result.at("uid-one");
    EXPECT_EQ(matched.uid, "uid-one");
    ASSERT_EQ(matched.containers.size(), 1);
    EXPECT_EQ(matched.containers.at("abc123"), "sidecar-name");
    EXPECT_EQ(matched.annotations.at("netflix.com/app"), "myapp");
    EXPECT_EQ(matched.labels.at("k8s-app"), "mylabelapp");

    // Unmatched pod: discovered from cgroups but absent from identities, so it keeps its uid and
    // cgroup path while every identity-sourced field stays empty. Asserting containers is empty
    // here (not just that name is empty) is what would catch a merge that leaked another pod's
    // container list into this one.
    const auto& unmatched = result.at("uid-two");
    EXPECT_EQ(unmatched.uid, "uid-two");
    EXPECT_TRUE(unmatched.containers.empty());
    EXPECT_TRUE(unmatched.annotations.empty());
    EXPECT_TRUE(unmatched.labels.empty());
}

// CollectMemoryStats() is the agent's ONLY refresh driver in shipped code (k8s-agent.cpp calls it
// once at startup to prime the tracked set, then on the 60s tick), yet it appeared nowhere in
// this file. Moving RefreshTrackedPods() after EmitMemoryStats(), or dropping it, would mean a
// newly-discovered container is never sampled on the cycle it appears.
//
// This pins the refresh, not the emission: routed through PodMonitor the container map is always
// empty, so EmitMemoryStats writes nothing here. Emission is covered by the TrackedPodRegistry
// tests above.
TEST(PodMonitor, CollectMemoryStatsRefreshesTrackedPods)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, std::string(kResources) + "/systemd"};

    podMonitor.CollectMemoryStats();
    EXPECT_EQ(podMonitor.TrackedPods().size(), 3);

    // Re-point at a root with no pods: proves the refresh actually ran on this call, rather than
    // the tracked set having happened to be correct already.
    podMonitor.SetPrefix(std::string(kResources) + "/does_not_exist");
    podMonitor.CollectMemoryStats();
    EXPECT_TRUE(podMonitor.TrackedPods().empty());
}

// Pins that an unreachable kubelet fails closed (nullopt) rather than throwing or hanging --
// which is what makes every other test in this file hermetic.
//
// It does NOT cover FetchPodIdentities' `status != 200` guard: an unreachable host yields an
// empty body that ParsePodList rejects at its JSON-parse check anyway, so deleting the status
// check entirely would leave this test green. Covering that guard needs the response-handling
// step split out behind a seam a test can feed a canned status/body to.
TEST(PodIdentityClient, FetchPodIdentitiesReturnsNulloptWhenKubeletUnreachable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodIdentityClientTest client{&r};
    EXPECT_FALSE(client.FetchPodIdentities().has_value());
}

}  // namespace
