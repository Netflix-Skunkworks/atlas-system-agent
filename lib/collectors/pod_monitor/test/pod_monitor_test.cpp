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
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

class PodMonitorTest : public atlasagent::PodMonitor
{
   public:
    using PodMonitor::PodMonitor;
    using PodMonitor::TrackedPods;
};

// Exposes CgroupPodDiscovery's protected parsing helpers.
class CgroupPodDiscoveryTest : public atlasagent::CgroupPodDiscovery
{
   public:
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

// Centralized fixture root used by tests whose runtime assertions detect a missing or mistyped path.
constexpr auto kResources = "lib/collectors/pod_monitor/test/resources";

// The uid every single-pod fixture uses, in its cgroup-directory (underscore) and canonical
// (dashed) spellings.
constexpr auto kPod1Dir = "kubepods-pod11111111_1111_1111_1111_111111111111.slice";
constexpr auto kPod1Uid = "11111111-1111-1111-1111-111111111111";

std::string ContainerId(char c) { return std::string(64, c); }

std::string Pod1SlicePath(const std::string& tree)
{
    return std::string(kResources) + "/" + tree + "/kubepods.slice/" + kPod1Dir;
}

atlasagent::ContainerCgroupMap ContainerScopes(const std::string& pod_path,
                                                std::initializer_list<std::string> container_ids)
{
    atlasagent::ContainerCgroupMap result;
    for (const auto& container_id : container_ids)
    {
        result.emplace(container_id, std::filesystem::path(pod_path) /
                                      ("cri-containerd-" + container_id + ".scope"));
    }
    return result;
}

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

// The path and cgroup_containers supply the cgroup side; identity_containers supplies the
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

// ResolvePodTags: pure fallback-chain tag resolution -- the netflix.com/* primary tier, the
// app.kubernetes.io/{name,instance,component} / k8s-app / app label fallback tier, nf.cluster's
// asymmetric primary-only gate, the all-absent Gating case, and nf.node's pod-name sourcing.
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
    // nf.detail's tag-emplace is currently disabled in ResolvePodTags ("todo uncomment later");
    // the "-mydetail" suffix still reaches nf.cluster because BuildNfCluster reads the annotation.
    EXPECT_FALSE(result->contains("nf.detail"));
    EXPECT_EQ(result->at("nf.cluster"), "myapp-mystack-mydetail");
    EXPECT_EQ(result->at("nf.node"), "my-pod-abc123");
    // nf.platform's tag-emplace is likewise currently disabled.
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
    // nf.detail/nf.platform's tag-emplaces are currently disabled in ResolvePodTags.
    EXPECT_FALSE(result->contains("nf.detail"));
    EXPECT_FALSE(result->contains("nf.platform"));
    // Asymmetric gate: nf.app resolved (via the label fallback tier, not the netflix.com/app
    // annotation), so nf.cluster must NOT be set even though nf.app is.
    EXPECT_FALSE(result->contains("nf.cluster"));
}

// Pins only that app.kubernetes.io/name wins when all three nf.app fallback labels are present.
// It deliberately does NOT pin k8s-app vs app: with all three seeded only the overall winner is
// observable, so swapping the last two entries of ResolvePodTags's fallback list -- or dropping
// "app" -- would still pass. Pinning those needs one test per label, each seeding it alone.
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
    // Present-but-empty still leaves primary_app unset, so nf.cluster (primary-only) stays unset.
    EXPECT_FALSE(result->contains("nf.cluster"));
}

TEST(PodTagResolver, ResolvePodTagsAllAbsentReturnsNullopt)
{
    EXPECT_FALSE(atlasagent::ResolvePodTags({}, {}, "", "").has_value());
    EXPECT_FALSE(atlasagent::ResolvePodTags({}, {}, "", "some-cluster").has_value());
}

// nf.node comes from a non-empty pod name rather than environ/annotations. It is deliberately
// excluded from the Gating decision: otherwise any normally named pod would pass without an
// app-identity annotation or label. A pod name alone must still gate out.
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

// The unit's headline rule, and unpinned elsewhere: no other test supplies both a non-empty
// primary annotation and a competing label for the same key, so inverting any of the three
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
// from label-fallback values -- the deliberate asymmetry the header documents. No other test has
// a primary app engaged alongside a contributing label, the only shape that catches a switch from
// the primary-only values to the post-fallback ones.
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

    // No primary app: nf.app resolves via label fallback, so nf.cluster must be absent even so.
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

    // The only test that makes the app.kubernetes.io/component fallback load-bearing: it is the
    // sole reason this pod is not gated out, and the returned map is legitimately EMPTY (no
    // app/stack, nf.detail disabled, no primary app so no cluster, no pod name, no K8S_CLUSTER).
    auto componentLabelOnly = atlasagent::ResolvePodTags({}, {{"app.kubernetes.io/component", "labeldetail"}}, "", "");
    ASSERT_TRUE(componentLabelOnly.has_value());
    EXPECT_TRUE(componentLabelOnly->empty());
}

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

TEST_F(TrackedPodRegistryTest, EvictsContainerWhoseCgroupScopeDisappears)
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
    // with NO container scopes -- the scope directory vanished. Gating still passes, so this
    // isolates EvictUntrackedContainers rather than the Gating clear.
    registry.Reconcile(OnePodPlan(kPod1Uid, Pod1SlicePath("systemd"), {}, containers, annotations));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    EXPECT_TRUE(registry.TrackedPods().at(kPod1Uid).containers.empty());
}

TEST_F(TrackedPodRegistryTest, DefersContainerUntilIdentityArrives)
{
    atlasagent::TrackedPodRegistry registry{&r};

    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    // The container's cgroup scope exists on disk but kubelet hasn't reported it yet (empty
    // container list) -- the documented transient race. It must not be tracked...
    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}), {},
                                  annotations));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    EXPECT_TRUE(registry.TrackedPods().at(kPod1Uid).containers.empty());

    // ...and once kubelet does report it, the next cycle picks it up with no intervening restart.
    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                  {{ContainerId('a'), "main"}}, annotations));
    EXPECT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
}

TEST_F(TrackedPodRegistryTest, MissingCurrentContainerIdentityEvictsTrackedContainer)
{
    atlasagent::TrackedPodRegistry registry{&r};
    const auto cgroup_path = Pod1SlicePath("systemd_pod_with_containers");
    const std::unordered_map<std::string, std::string> annotations{{"netflix.com/app", "myapp"}};

    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}),
                                  {{ContainerId('a'), "main"}}, annotations));
    ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);

    registry.Reconcile(OnePodPlan(kPod1Uid, cgroup_path, ContainerScopes(cgroup_path, {ContainerId('a')}), {},
                                  annotations));
    ASSERT_TRUE(registry.TrackedPods().contains(kPod1Uid));
    EXPECT_TRUE(registry.TrackedPods().at(kPod1Uid).containers.empty());
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

TEST_F(TrackedPodRegistryTest, ResolvesCpuCountFromQuotaThenFallsBackToSysconf)
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

    // cpu.max "max 100000" is the unlimited case: QuotaCpuCount returns nullopt and the code falls
    // back to the online CPU count from sysconf, computed here the same way ResolveCpuCountForPod
    // does.
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

TEST_F(TrackedPodRegistryTest, ReResolvesCpuCountEveryRefreshCycle)
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

    // Resize to 2 CPUs and refresh again. Re-resolving only at first insertion would keep
    // publishing 0.5 here, understating the container's capacity for the rest of the process.
    tree.WriteScopeFile("cpu.max", "200000 100000\n");
    registry.Reconcile(OnePodPlan(kPod1Uid, tree.PodPath(), ContainerScopes(tree.PodPath(), {ContainerId('a')}),
                                  containers, annotations));
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

    // Case A: the container scope has no cpu.stat and no cpu.max at all.
    {
        atlasagent::TrackedPodRegistry registry{&r};
        const auto fixture_path = Pod1SlicePath("systemd_pod_with_containers");
        registry.Reconcile(OnePodPlan(kPod1Uid, fixture_path, ContainerScopes(fixture_path, {ContainerId('a')}),
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
    // file, which a whole-file existence check would have let through.
    {
        TempCgroupTree tree{"partial_cpu_stat"};
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

    // Case C: cpu.stat COMPLETE but cpu.max absent, so GetAvailCpuTime returns 0. Cases A and B
    // both bail out at the cpu.stat key guard and never reach the divide-by-zero gate, so without
    // this case that third part of the fix is uncovered. TWO cycles are required: on the first the
    // prev_* baselines are still -1, so the gauges are skipped for an unrelated reason -- only on
    // the second is the avail_cpu_time gate the deciding factor, and removing it publishes secs/0.
    {
        TempCgroupTree tree{"complete_cpu_stat_no_cpu_max"};
        tree.WriteScopeFile("cpu.stat", "usage_usec 1000\nuser_usec 400\nsystem_usec 600\n");

        atlasagent::TrackedPodRegistry registry{&r};
        registry.Reconcile(OnePodPlan(kPod1Uid, tree.PodPath(), ContainerScopes(tree.PodPath(), {ContainerId('a')}),
                                      containers, annotations));
        ASSERT_EQ(registry.TrackedPods().at(kPod1Uid).containers.size(), 1);
        registry.EmitCpuStats(true, true);  // seeds the prev_* baselines

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
    const std::string json = "not json{{{";
    auto result = PodIdentityClientTest::ParsePodList(json);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::PodIdentityErrorKind::Parse);
    EXPECT_EQ(result.error().response_size, json.size());
}

TEST(PodIdentityClient, ParsePodListMissingItemsFails)
{
    auto result = PodIdentityClientTest::ParsePodList(R"json({"kind":"PodList"})json");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::PodIdentityErrorKind::Envelope);
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

// status.containerStatuses parsing had NO coverage at all before this. PodIdentity::containers is
// what BuildActivePods matches cgroup-discovered ids against, so a parsing regression here
// gates out every container on the node while leaving pods tracked and every other test green.
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
          {
            "name": "main",
            "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          },
          {
            "name": "sidecar",
            "containerID": "cri-o://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
          },
          {
            "name": "double-scheme",
            "containerID": "docker://x://cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
          },
          {
            "name": "bare",
            "containerID": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
          }
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
    // The runtime scheme prefix must be stripped so a normal containerID key matches the id segment
    // carried by the cgroup scope directory name.
    EXPECT_EQ(identity.containers.at(ContainerId('a')).name, "main");
    EXPECT_EQ(identity.containers.at(ContainerId('b')).name, "sidecar");
    // Strips at the FIRST "://", not the last -- this entry is what distinguishes the two, and
    // a switch to find-last would key this container as the bare c's instead.
    EXPECT_EQ(identity.containers.at("x://" + std::string(60, 'c')).name, "double-scheme");
    // No scheme at all: passed through unchanged rather than mangled.
    EXPECT_EQ(identity.containers.at(ContainerId('d')).name, "bare");
}

// A NATIVE SIDECAR -- an initContainer with restartPolicy=Always (k8s 1.28+) -- is reported in
// status.initContainerStatuses, NOT status.containerStatuses, yet runs for the pod's whole lifetime
// with its own cgroup scope. While that array went unparsed, such a container was discovered on
// disk and skipped every cycle, forever and silently: zero CPU/memory/IO metrics for the entire
// mesh-proxy / log-forwarder tier while the pod and its main container looked perfectly healthy.
//
// Parsing spec.initContainers[] (which ParsePodListParsesCpuRequestsFromSpec covers) is NOT a
// substitute: spec carries container names but no ids -- an id is assigned when the runtime creates
// the container -- so status is the only source of ids, and BuildActivePods resolves
// cgroup-discovered scopes by id.
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
          {
            "name": "envoy",
            "containerID": "containerd://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
          }
        ],
        "containerStatuses": [
          {
            "name": "app",
            "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          }
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
    EXPECT_EQ(identity.containers.at(ContainerId('a')).name, "app");
    EXPECT_EQ(identity.containers.at(ContainerId('b')).name, "envoy");

    // Requests from the spec are attached directly to the corresponding runtime identities.
    ASSERT_TRUE(identity.containers.at(ContainerId('b')).cpu_request.has_value());
    EXPECT_DOUBLE_EQ(*identity.containers.at(ContainerId('b')).cpu_request, 0.05);
    ASSERT_TRUE(identity.containers.at(ContainerId('a')).cpu_request.has_value());
    EXPECT_DOUBLE_EQ(*identity.containers.at(ContainerId('a')).cpu_request, 0.5);
}

// This fixture models waiting statuses (such as ImagePullBackOff or CreateContainerError) with an
// explicitly present but empty containerID, which passes the IsString() check. Keying the map on ""
// would break its non-empty-key contract, and every not-yet-started container in the pod would
// contend for that single entry.
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
          {
            "name": "running",
            "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          }
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
    EXPECT_EQ(identity.containers.at(ContainerId('a')).name, "running");
    EXPECT_FALSE(identity.containers.contains(""));
}

TEST(PodIdentityClient, ParsePodListOmitsEphemeralContainerStatuses)
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
          {
            "name": "main",
            "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          }
        ],
        "ephemeralContainerStatuses": [
          {
            "name": "debugger",
            "containerID": "containerd://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
          }
        ]
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& containers = result->at(kPod1Uid).containers;
    ASSERT_EQ(containers.size(), 1);
    EXPECT_TRUE(containers.contains(ContainerId('a')));
    EXPECT_FALSE(containers.contains(ContainerId('b')));
}

// spec.containers[] and spec.initContainers[] are the parsed sources for container CPU requests;
// the cgroup filesystem carries cpu.max (the limit) instead. The spec was fetched but never parsed
// before this plumbing was added. See the native-sidecar note above for why initContainers matters.
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
      },
      "status": {
        "initContainerStatuses": [
          {
            "name": "sidecar",
            "containerID": "containerd://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
          }
        ],
        "containerStatuses": [
          {
            "name": "main",
            "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          },
          {
            "name": "besteffort",
            "containerID": "containerd://cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
          },
          {
            "name": "limits-only",
            "containerID": "containerd://dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
          },
          {
            "name": "no-resources-key",
            "containerID": "containerd://eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
          },
          {
            "name": "unparseable",
            "containerID": "containerd://ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
          }
        ]
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");

    ASSERT_EQ(identity.containers.size(), 6);
    ASSERT_TRUE(identity.containers.at(ContainerId('a')).cpu_request.has_value());
    EXPECT_DOUBLE_EQ(*identity.containers.at(ContainerId('a')).cpu_request, 0.5);
    // From initContainers[], not containers[].
    ASSERT_TRUE(identity.containers.at(ContainerId('b')).cpu_request.has_value());
    EXPECT_DOUBLE_EQ(*identity.containers.at(ContainerId('b')).cpu_request, 0.05);

    // Every "no request" shape keeps an empty optional rather than fabricating zero.
    EXPECT_FALSE(identity.containers.at(ContainerId('c')).cpu_request.has_value());
    EXPECT_FALSE(identity.containers.at(ContainerId('d')).cpu_request.has_value());
    EXPECT_FALSE(identity.containers.at(ContainerId('e')).cpu_request.has_value());
    EXPECT_FALSE(identity.containers.at(ContainerId('f')).cpu_request.has_value());
}

// A pod with no spec must retain its runtime identity with no CPU request.
TEST(PodIdentityClient, ParsePodListMissingSpecLeavesCpuRequestEmpty)
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
          {
            "name": "main",
            "containerID": "containerd://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          }
        ]
      }
    }
  ]
}
  )json";

    auto result = PodIdentityClientTest::ParsePodList(json);

    ASSERT_TRUE(result.has_value());
    const auto& identity = result->at("11111111-1111-1111-1111-111111111111");
    EXPECT_EQ(identity.name, "pod-one");
    ASSERT_EQ(identity.containers.size(), 1);
    EXPECT_FALSE(identity.containers.at(ContainerId('a')).cpu_request.has_value());
}

TEST(BuildActivePods, BuildsActivePodsOnlyForMatchedCgroups)
{
    atlasagent::CgroupSnapshot cgroups;
    cgroups.emplace("matched", atlasagent::CgroupPod{"/cgroup/matched", {{"container-a", "/cgroup/matched/a"}}});
    cgroups.emplace("cgroup-only", atlasagent::CgroupPod{"/cgroup/cgroup-only", {}});

    atlasagent::PodIdentityMap identities;
    identities.emplace("matched",
                       atlasagent::PodIdentity{"pod-one", "ns-one",
                                               {{"container-a", atlasagent::ContainerIdentity{"main", 0.25}}},
                                               {{"netflix.com/app", "myapp"}}, {}});
    identities.emplace("identity-only", atlasagent::PodIdentity{"pod-two", "ns-two"});

    auto active_pods = atlasagent::BuildActivePods(cgroups, identities, "test-cluster");

    ASSERT_EQ(active_pods.size(), 1);
    const auto& matched = active_pods.at("matched");
    EXPECT_EQ(matched.tags.at("k8s.namespace.name"), "ns-one");
    EXPECT_EQ(matched.tags.at("k8s.cluster.name"), "test-cluster");
    ASSERT_EQ(matched.containers.size(), 1);
    EXPECT_EQ(matched.containers.at("container-a").name, "main");
    ASSERT_TRUE(matched.containers.at("container-a").cpu_request.has_value());
    EXPECT_DOUBLE_EQ(*matched.containers.at("container-a").cpu_request, 0.25);
}

TEST(BuildActivePods, RequiresCurrentContainerIdentityForCgroupScope)
{
    atlasagent::CgroupSnapshot cgroups;
    cgroups.emplace("pod-uid", atlasagent::CgroupPod{"/cgroup/pod", {{"matched", "/cgroup/pod/matched"},
                                                                      {"cgroup-only", "/cgroup/pod/cgroup-only"}}});
    atlasagent::PodIdentityMap identities;
    identities.emplace("pod-uid",
                       atlasagent::PodIdentity{"pod-one", "ns-one",
                                               {{"matched", atlasagent::ContainerIdentity{"main", std::nullopt}},
                                                {"identity-only", atlasagent::ContainerIdentity{"old", std::nullopt}}},
                                               {{"netflix.com/app", "myapp"}}, {}});

    auto active_pods = atlasagent::BuildActivePods(cgroups, identities, "");

    ASSERT_EQ(active_pods.at("pod-uid").containers.size(), 1);
    EXPECT_TRUE(active_pods.at("pod-uid").containers.contains("matched"));
}

TEST(PodMonitor, SuccessfulRefreshThenIdentityFailureSuspendsEmission)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

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
    PodMonitorTest podMonitor{&r, std::move(cgroup_source), std::move(identity_source), "test-cluster"};

    auto refreshed = podMonitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kActive);
    ASSERT_EQ(podMonitor.TrackedPods().at(kPod1Uid).containers.size(), 1);
    EXPECT_EQ(refreshed.active_pods.at(kPod1Uid).containers.size(), 1);
    EXPECT_EQ(cgroup_source_ptr->Calls(), 1);
    EXPECT_EQ(identity_source_ptr->Calls(), 1);

    podMonitor.CollectMemoryStats();
    EXPECT_EQ(cgroup_source_ptr->Calls(), 1);
    EXPECT_EQ(identity_source_ptr->Calls(), 1);

    identity_source_ptr->SetResult(
        std::unexpected(atlasagent::PodIdentityError{atlasagent::PodIdentityErrorKind::Http, 503}));
    refreshed = podMonitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kIdentityUnavailable);
    EXPECT_TRUE(podMonitor.TrackedPods().empty());
    EXPECT_TRUE(refreshed.active_pods.empty());

    memoryWriter->Clear();
    podMonitor.CollectCpuStats(true, true);
    podMonitor.CollectIOStats();
    podMonitor.CollectMemoryStats();
    EXPECT_TRUE(memoryWriter->GetMessages().empty());

    // A successful empty identity snapshot is authoritative rather than an error: emission is
    // active again, but there are no admitted containers.
    identity_source_ptr->SetResult(atlasagent::PodIdentityMap{});
    refreshed = podMonitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kActive);
    EXPECT_TRUE(podMonitor.TrackedPods().empty());
}

TEST(PodMonitor, CgroupFailureSuspendsEmissionBeforeIdentityFetch)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    auto* memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());

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
    PodMonitorTest podMonitor{&r, std::move(cgroup_source), std::move(identity_source), ""};

    auto refreshed = podMonitor.Refresh();
    ASSERT_EQ(refreshed.state, atlasagent::PodMonitorState::kActive);
    ASSERT_EQ(podMonitor.TrackedPods().at(kPod1Uid).containers.size(), 1);

    const auto missing_path = std::filesystem::path{"/missing/cgroup/root"};
    const auto missing_cause = std::make_error_code(std::errc::no_such_file_or_directory);
    cgroup_source_ptr->SetResult(std::unexpected(atlasagent::CgroupDiscoveryError{
        atlasagent::CgroupDiscoveryErrorKind::kMissingRoot, missing_path, missing_cause}));
    refreshed = podMonitor.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kCgroupUnavailable);
    ASSERT_TRUE(refreshed.cgroup_error.has_value());
    EXPECT_EQ(refreshed.cgroup_error->kind, atlasagent::CgroupDiscoveryErrorKind::kMissingRoot);
    EXPECT_EQ(refreshed.cgroup_error->path, missing_path);
    EXPECT_EQ(refreshed.cgroup_error->cause, missing_cause);
    EXPECT_TRUE(podMonitor.TrackedPods().empty());
    EXPECT_EQ(cgroup_source_ptr->Calls(), 2);
    EXPECT_EQ(identity_source_ptr->Calls(), 1);

    memoryWriter->Clear();
    podMonitor.CollectCpuStats(true, true);
    podMonitor.CollectIOStats();
    podMonitor.CollectMemoryStats();
    EXPECT_TRUE(memoryWriter->GetMessages().empty());
}

TEST(PodMonitor, MissingInjectedSourcesReturnExplicitFailures)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);

    auto identity_source = std::make_unique<FakePodIdentitySource>(atlasagent::PodIdentityMap{});
    auto* identity_source_ptr = identity_source.get();
    PodMonitorTest missing_cgroup{&r, std::unique_ptr<atlasagent::PodCgroupSource>{},
                                  std::move(identity_source), ""};

    auto refreshed = missing_cgroup.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kCgroupUnavailable);
    ASSERT_TRUE(refreshed.cgroup_error.has_value());
    EXPECT_EQ(refreshed.cgroup_error->kind, atlasagent::CgroupDiscoveryErrorKind::kUnavailableSource);
    EXPECT_EQ(identity_source_ptr->Calls(), 0);

    auto cgroup_source = std::make_unique<FakePodCgroupSource>(atlasagent::CgroupSnapshot{});
    auto* cgroup_source_ptr = cgroup_source.get();
    PodMonitorTest missing_identity{&r, std::move(cgroup_source),
                                    std::unique_ptr<atlasagent::PodIdentitySource>{}, ""};

    refreshed = missing_identity.Refresh();
    EXPECT_EQ(refreshed.state, atlasagent::PodMonitorState::kIdentityUnavailable);
    ASSERT_TRUE(refreshed.identity_error.has_value());
    EXPECT_EQ(refreshed.identity_error->kind, atlasagent::PodIdentityErrorKind::UnavailableSource);
    EXPECT_EQ(cgroup_source_ptr->Calls(), 1);
}

TEST(PodIdentityClient, FetchPodIdentitiesReturnsHttpErrorWhenKubeletUnreachable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodIdentityClientTest client{&r};
    auto result = client.FetchPodIdentities();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, atlasagent::PodIdentityErrorKind::Http);
}

}  // namespace
