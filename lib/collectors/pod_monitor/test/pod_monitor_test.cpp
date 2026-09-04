#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/collectors/pod_monitor/src/util/pod_identity_client.h>
#include <lib/collectors/pod_monitor/src/util/cgroup_pod_discovery.h>
#include <lib/collectors/pod_monitor/src/util/pod_tag_resolver.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

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
// RefreshTrackedPodsContainerNotTrackedWhenPodIdentityUnresolved below is the concrete
// end-to-end proof of the Gating-*failure* path, which this file's hermetic (identity-lookup-
// always-fails) test setup can actually exercise.

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
    // The fixture directory also contains a plain file (cgroup.procs) and a subdirectory that
    // doesn't carry the cri-containerd-*.scope shape -- neither should be picked up.
    auto containers = atlasagent::CgroupPodDiscovery::FindContainersInPod(
        "lib/collectors/pod_monitor/test/resources/systemd_pod_with_containers/kubepods.slice/"
        "kubepods-pod11111111_1111_1111_1111_111111111111.slice");

    EXPECT_FALSE(containers.contains("cgroup.procs"));
    for (const auto& [id, path] : containers)
    {
        EXPECT_EQ(id, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
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

TEST(PodTagResolver, ResolvePodTagsLabelFallbackOrderPrefersAppNameOverK8sAppOverApp)
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

// End-to-end wiring test through RefreshTrackedPods(): a pod with a real, discoverable container
// (systemd_pod_with_containers, the same fixture FindContainersInPod's own tests use) is
// discovered structurally, but this test's hermetic setup means FindActivePodInfo()'s identity
// lookup always fails closed -- so info.annotations/info.labels are always empty and
// ResolvePodTags always returns nullopt. Confirms Gating drops every container in that pod, not
// just skips tagging it -- see the NOTE above this test group for why the successful path can't
// be exercised the same way.
TEST(PodMonitor, RefreshTrackedPodsContainerNotTrackedWhenPodIdentityUnresolved)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd_pod_with_containers"};

    podMonitor.RefreshTrackedPods();

    ASSERT_TRUE(podMonitor.TrackedPods().contains("11111111-1111-1111-1111-111111111111"));
    EXPECT_TRUE(podMonitor.TrackedPods().at("11111111-1111-1111-1111-111111111111").containers.empty());
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

TEST(PodIdentityClient, FetchPodIdentitiesReturnsNulloptWhenKubeletUnreachable)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodIdentityClientTest client{&r};
    EXPECT_FALSE(client.FetchPodIdentities().has_value());
}

}  // namespace
