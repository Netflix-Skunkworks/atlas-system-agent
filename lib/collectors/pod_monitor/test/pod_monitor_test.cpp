#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/collectors/pod_monitor/src/pod_identity_client.h>
#include <lib/util/src/util.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <absl/strings/str_split.h>

#include <gtest/gtest.h>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class PodMonitorTest : public atlasagent::PodMonitor
{
   public:
    explicit PodMonitorTest(
        Registry* registry, std::string path_prefix = "/sys/fs/cgroup",
        std::string kubeconfig_path = "lib/collectors/pod_monitor/test/resources/does_not_exist/kubeconfig") noexcept
        : PodMonitor(registry, std::move(path_prefix), std::move(kubeconfig_path))
    {
    }

    // Expose protected members and methods for testing
    using PodMonitor::MatchPodSliceName;
    using PodMonitor::NormalizePodUid;
    using PodMonitor::ScanPodSliceDirectory;
    using PodMonitor::JoinCgroupAndIdentity;
    using PodMonitor::FindContainersInPod;
    using PodMonitor::ResolveContainerTags;
    using PodMonitor::RefreshTrackedPods;
    using PodMonitor::TrackedPods;
};

class PodIdentityClientTest : public atlasagent::PodIdentityClient
{
   public:
    explicit PodIdentityClientTest(
        Registry* registry,
        std::string kubeconfig_path = "lib/collectors/pod_monitor/test/resources/does_not_exist/kubeconfig") noexcept
        : PodIdentityClient(registry, std::move(kubeconfig_path))
    {
    }

    // Expose protected methods for testing
    using PodIdentityClient::ParsePodList;
    using PodIdentityClient::ExtractKubeconfigFields;
    using PodIdentityClient::ParseExecCredentialToken;
    using PodIdentityClient::BuildExecCommandLine;
    using PodIdentityClient::BuildApiServerUrl;
};

namespace
{

// Spawns a short-lived child (execve of /bin/sleep with a caller-supplied envp) so its real
// /proc/<pid>/environ genuinely contains the given variables. This test binary's own environ
// cannot be used for a positive-path test: proc(5) is explicit that the kernel does NOT update
// /proc/<pid>/environ after execve, so a setenv() call from within a running test would never be
// visible there -- only a process actually exec'd with the desired environment shows it. Polls
// briefly after fork() before returning, since the child's /proc/<pid>/environ still reflects the
// pre-exec (inherited) state during the narrow fork-to-exec window. Returns -1 if the child never
// became ready in time; caller must call KillAndReapChild() exactly once, regardless of outcome.
pid_t SpawnChildWithEnviron(const std::vector<std::string>& env_entries)
{
    std::vector<char*> envp;
    for (const auto& entry : env_entries)
    {
        envp.push_back(const_cast<char*>(entry.c_str()));
    }
    envp.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0)
    {
        char* argv[] = {const_cast<char*>("/bin/sleep"), const_cast<char*>("30"), nullptr};
        execve("/bin/sleep", argv, envp.data());
        _exit(127);  // execve itself failed
    }
    if (pid < 0)
    {
        return -1;
    }

    // Readiness means the child's environ actually contains what was requested -- NOT merely
    // non-empty, since the pre-exec (just-forked) child's environ is a copy of this test binary's
    // own environ and is already non-empty (PATH, HOME, etc. are essentially always present), so
    // an emptiness check alone would report "ready" before execve() has even run.
    std::unordered_map<std::string, std::string> expected;
    for (const auto& entry : env_entries)
    {
        auto eq = entry.find('=');
        if (eq != std::string::npos)
        {
            expected.emplace(entry.substr(0, eq), entry.substr(eq + 1));
        }
    }

    constexpr int kMaxPollAttempts = 100;
    for (int attempt = 0; attempt < kMaxPollAttempts; ++attempt)
    {
        auto environ_now = atlasagent::read_process_environ(pid);
        if (environ_now.has_value())
        {
            bool all_match = true;
            for (const auto& [key, value] : expected)
            {
                auto it = environ_now->find(key);
                if (it == environ_now->end() || it->second != value)
                {
                    all_match = false;
                    break;
                }
            }
            if (all_match)
            {
                return pid;
            }
        }
        usleep(10000);  // 10ms
    }
    return -1;
}

void KillAndReapChild(pid_t pid)
{
    if (pid > 0)
    {
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);
    }
}

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

TEST(PodMonitor, RefreshTrackedPodsPartialAddAndEvict)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    // Uses the identity-client's default kubeconfig path, which points at a nonexistent file
    // (see PodMonitorTest's default above), so FindAllActivePods2()'s identity lookup always
    // fails closed and no real apiserver/network call is ever made -- this test is hermetic.
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

// NOTE: the two tests this comment replaces (RefreshTrackedPodsAppliesQuotaDerivedCpuCountOverride
// and CollectMemoryStatsEmitsMemoryStatsStdV2WithPodTag) asserted on pod-level aggregate CGroup
// emission (a pod-level cpu.max/memory.* fixture, tagged via the now-removed BuildPodTags()).
// Per the "Per-container cgroup metrics" plan increment, pod-level aggregate CGroup emission is
// gone entirely -- TrackedPod no longer owns a CGroup, so there is nothing left for those
// assertions to observe; they are removed rather than salvaged. Container-level equivalents of
// ResolveCpuCountForPod()/SetCpuCountOverride() wiring and tag emission are exercised indirectly
// by RefreshTrackedPodsContainerGatedOutWhenEnvironLacksAnyTagKey below (the discovery/PID/environ
// plumbing) plus ResolveContainerTags's own direct unit tests (the tag-resolution logic).
// RefreshTrackedPodsContainerTracksWhenEnvironHasNetflixApp (further below) is the true end-to-end
// positive-path counterpart: a container whose real environ genuinely satisfies the primary tier
// (via a real exec'd child process, since this test binary's own environ can't be mutated
// retroactively -- see SpawnChildWithEnviron's own comment) is confirmed to actually get tracked.

TEST(PodMonitor, RefreshTrackedPodsContainerTracksWhenEnvironHasNetflixApp)
{
    pid_t child = SpawnChildWithEnviron({"netflix.app=test-tracked-app"});
    ASSERT_GT(child, 0) << "child process never became ready with the expected environ in time";

    auto tmp_root = std::filesystem::path(::testing::TempDir()) / "pod_monitor_container_tracking_test";
    std::filesystem::remove_all(tmp_root);

    auto pod_dir = tmp_root / "kubepods.slice/kubepods-pod11111111_1111_1111_1111_111111111111.slice";
    auto container_dir =
        pod_dir / "cri-containerd-cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc.scope";
    std::filesystem::create_directories(container_dir);
    {
        std::ofstream procs(container_dir / "cgroup.procs");
        procs << child << "\n";
    }

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, tmp_root.string()};

    podMonitor.RefreshTrackedPods();

    KillAndReapChild(child);
    std::filesystem::remove_all(tmp_root);

    ASSERT_TRUE(podMonitor.TrackedPods().contains("11111111-1111-1111-1111-111111111111"));
    const auto& containers = podMonitor.TrackedPods().at("11111111-1111-1111-1111-111111111111").containers;
    EXPECT_TRUE(containers.contains("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"));
}

// FindContainersInPod tests (parallel to the ScanPodSliceDirectory/MatchPodSliceName coverage
// above -- structural directory-name matching only, no PID/environ I/O involved).
TEST(PodMonitor, FindContainersInPodMatchesCriContainerdScopes)
{
    auto containers = PodMonitorTest::FindContainersInPod(
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

TEST(PodMonitor, FindContainersInPodIgnoresNonMatchingEntries)
{
    // The fixture directory also contains a plain file (cgroup.procs) and a subdirectory that
    // doesn't carry the cri-containerd-*.scope shape -- neither should be picked up.
    auto containers = PodMonitorTest::FindContainersInPod(
        "lib/collectors/pod_monitor/test/resources/systemd_pod_with_containers/kubepods.slice/"
        "kubepods-pod11111111_1111_1111_1111_111111111111.slice");

    EXPECT_FALSE(containers.contains("cgroup.procs"));
    for (const auto& [id, path] : containers)
    {
        EXPECT_EQ(id, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
}

TEST(PodMonitor, FindContainersInPodMissingDirReturnsEmpty)
{
    auto containers =
        PodMonitorTest::FindContainersInPod("lib/collectors/pod_monitor/test/resources/does_not_exist");
    EXPECT_TRUE(containers.empty());
}

// ResolveContainerTags: the pure fallback-chain tag resolution, covering the primary tier, the
// k8s.label.* fallback tier, nf.cluster's asymmetric primary-only gate, the all-absent Gating
// case, and a lone-structural-field case.
TEST(PodMonitor, ResolveContainerTagsPrimaryTierOnly)
{
    std::unordered_map<std::string, std::string> environ{
        {"netflix.app", "myapp"},
        {"netflix.stack", "mystack"},
        {"netflix.detail", "mydetail"},
    };

    auto result = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->at("nf.app"), "myapp");
    EXPECT_EQ(result->at("nf.stack"), "mystack");
    EXPECT_EQ(result->at("nf.cluster"), "myapp-mystack-mydetail");
    EXPECT_EQ(result->at("nf.platform"), "k8s");
    EXPECT_FALSE(result->contains("k8s.cluster.name"));
    EXPECT_FALSE(result->contains("nf.node"));
    EXPECT_FALSE(result->contains("nf.process"));
}

TEST(PodMonitor, ResolveContainerTagsPrimaryAppOnlyClusterHasNoStackOrDetailSuffix)
{
    std::unordered_map<std::string, std::string> environ{{"netflix.app", "myapp"}};

    auto result = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.cluster"), "myapp");
    EXPECT_FALSE(result->contains("nf.stack"));
}

TEST(PodMonitor, ResolveContainerTagsLabelFallbackTierOnly)
{
    std::unordered_map<std::string, std::string> environ{
        {"k8s.label.app.name", "labelapp"},
        {"k8s.label.app.instance", "labelstack"},
    };

    auto result = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->at("nf.app"), "labelapp");
    EXPECT_EQ(result->at("nf.stack"), "labelstack");
    EXPECT_EQ(result->at("nf.platform"), "k8s");
    // Asymmetric gate: nf.app resolved (via the label fallback tier, not netflix.app), so
    // nf.cluster must NOT be set even though nf.app is.
    EXPECT_FALSE(result->contains("nf.cluster"));
}

TEST(PodMonitor, ResolveContainerTagsLabelFallbackOrderPrefersAppNameOverK8sAppOverApp)
{
    std::unordered_map<std::string, std::string> environ{
        {"k8s.label.app", "third"},
        {"k8s.label.k8s-app", "second"},
        {"k8s.label.app.name", "first"},
    };

    auto result = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.app"), "first");
}

TEST(PodMonitor, ResolveContainerTagsEmptyNetflixAppFallsThroughToLabelTier)
{
    // netflix.app present but empty must be treated as unset, per "present and non-empty".
    std::unordered_map<std::string, std::string> environ{
        {"netflix.app", ""},
        {"k8s.label.k8s-app", "fallback-app"},
    };

    auto result = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.app"), "fallback-app");
    // netflix.app was present-but-empty, not absent -- but primary_app is still unset per the
    // "present and non-empty" rule, so nf.cluster (primary-only) must stay unset too.
    EXPECT_FALSE(result->contains("nf.cluster"));
}

TEST(PodMonitor, ResolveContainerTagsAllAbsentReturnsNullopt)
{
    std::unordered_map<std::string, std::string> environ{};
    EXPECT_FALSE(PodMonitorTest::ResolveContainerTags(environ, "").has_value());
    EXPECT_FALSE(PodMonitorTest::ResolveContainerTags(environ, "some-cluster").has_value());
}

TEST(PodMonitor, ResolveContainerTagsOnlyPodNameSetStillPassesGating)
{
    std::unordered_map<std::string, std::string> environ{{"k8s.pod.name", "my-pod-abc123"}};

    auto result = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.node"), "my-pod-abc123");
    EXPECT_EQ(result->at("nf.platform"), "k8s");
    EXPECT_FALSE(result->contains("nf.app"));
    EXPECT_FALSE(result->contains("nf.stack"));
    EXPECT_FALSE(result->contains("nf.process"));
    EXPECT_FALSE(result->contains("nf.cluster"));
}

TEST(PodMonitor, ResolveContainerTagsContainerNameOnlyStillPassesGating)
{
    std::unordered_map<std::string, std::string> environ{{"k8s.container.name", "sidecar"}};

    auto result = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at("nf.process"), "sidecar");
}

TEST(PodMonitor, ResolveContainerTagsSetsK8sClusterNameOnlyWhenNonEmpty)
{
    std::unordered_map<std::string, std::string> environ{{"netflix.app", "myapp"}};

    auto withCluster = PodMonitorTest::ResolveContainerTags(environ, "my-cluster");
    ASSERT_TRUE(withCluster.has_value());
    EXPECT_EQ(withCluster->at("k8s.cluster.name"), "my-cluster");

    auto withoutCluster = PodMonitorTest::ResolveContainerTags(environ, "");
    ASSERT_TRUE(withoutCluster.has_value());
    EXPECT_FALSE(withoutCluster->contains("k8s.cluster.name"));
}

// End-to-end wiring test through RefreshTrackedPods(): a container scope directory is discovered
// structurally, its cgroup.procs resolves to a real PID (this test process's own, via getpid()),
// but that PID's real /proc/<pid>/environ -- a plain gtest binary's inherited environment -- is
// asserted-by-construction not to define any of ResolveContainerTags' literal dotted-namespaced
// keys (netflix.app, k8s.pod.name, etc.), so Gating must drop it: the container is discovered but
// never tracked. A second container scope directory has an empty cgroup.procs (no PID at all yet)
// to exercise the separate "not ready this cycle" path in the same run. Neither path can leave
// anything in pod.containers.
TEST(PodMonitor, RefreshTrackedPodsContainerGatedOutWhenEnvironLacksAnyTagKey)
{
    auto tmp_root = std::filesystem::path(::testing::TempDir()) / "pod_monitor_container_gating_test";
    std::filesystem::remove_all(tmp_root);

    auto pod_dir = tmp_root /
        "kubepods.slice/kubepods-pod11111111_1111_1111_1111_111111111111.slice";
    auto gated_container_dir =
        pod_dir / "cri-containerd-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.scope";
    auto not_ready_container_dir =
        pod_dir / "cri-containerd-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.scope";
    std::filesystem::create_directories(gated_container_dir);
    std::filesystem::create_directories(not_ready_container_dir);

    {
        std::ofstream procs(gated_container_dir / "cgroup.procs");
        procs << getpid() << "\n";
    }
    { std::ofstream procs(not_ready_container_dir / "cgroup.procs"); }  // deliberately empty

    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, tmp_root.string()};

    podMonitor.RefreshTrackedPods();

    ASSERT_EQ(podMonitor.TrackedPods().size(), 1);
    ASSERT_TRUE(podMonitor.TrackedPods().contains("11111111-1111-1111-1111-111111111111"));
    EXPECT_TRUE(podMonitor.TrackedPods().at("11111111-1111-1111-1111-111111111111").containers.empty());

    std::filesystem::remove_all(tmp_root);
}

TEST(PodMonitor, RefreshTrackedPodsEvictsAllWhenRootDisappears)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd"};

    podMonitor.RefreshTrackedPods();
    ASSERT_EQ(podMonitor.TrackedPods().size(), 3);

    // Point at a nonexistent root, so FindAllActivePods2() discovers nothing.
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

TEST(PodMonitor, JoinCgroupAndIdentityPartialMatch)
{
    atlasagent::PodCgroupMap cgroup_pods;
    cgroup_pods.emplace("11111111-1111-1111-1111-111111111111", std::filesystem::path("/sys/fs/cgroup/pod-one"));
    cgroup_pods.emplace("22222222-2222-2222-2222-222222222222", std::filesystem::path("/sys/fs/cgroup/pod-two"));

    atlasagent::PodIdentityMap identities;
    identities.emplace("11111111-1111-1111-1111-111111111111",
                        atlasagent::PodIdentity{"pod-one", "namespace-one"});

    auto result = PodMonitorTest::JoinCgroupAndIdentity(cgroup_pods, std::optional(identities));

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").name, "pod-one");
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").pod_namespace, "namespace-one");
    EXPECT_EQ(result.at("11111111-1111-1111-1111-111111111111").cgroup_path,
              std::filesystem::path("/sys/fs/cgroup/pod-one"));

    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").name, "");
    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").pod_namespace, "");
    EXPECT_EQ(result.at("22222222-2222-2222-2222-222222222222").cgroup_path,
              std::filesystem::path("/sys/fs/cgroup/pod-two"));
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

TEST(PodMonitor, FindAllActivePods2WithoutKubeconfigIsHermetic)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodMonitorTest podMonitor{&r, "lib/collectors/pod_monitor/test/resources/systemd"};

    auto pods = podMonitor.FindAllActivePods2();

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

TEST(PodIdentityClient, FetchPodIdentitiesReturnsNulloptWhenKubeconfigMissing)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto r = Registry(config);
    PodIdentityClientTest client{&r};
    EXPECT_FALSE(client.FetchPodIdentities().has_value());
}

TEST(PodIdentityClient, ExtractKubeconfigFieldsWellFormed)
{
    std::string yaml = R"yaml(
clusters:
- cluster:
    certificate-authority-data: aGVsbG8td29ybGQ=
    server: https://FF1DE699AA64BDA14ED2F1570FB48502.sk1.eks-cluster.us-east-1.api.aws
  name: cluster-name
users:
- name: user-name
  user:
    exec:
      apiVersion: client.authentication.k8s.io/v1beta1
      args:
      - eks
      - get-token
      - --cluster-name
      - compute-us-east-1-test-ebadeaux
      command: aws
      env: null
)yaml";
    std::vector<std::string> lines = absl::StrSplit(yaml, '\n');
    auto result = PodIdentityClientTest::ExtractKubeconfigFields(lines);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->server, "https://FF1DE699AA64BDA14ED2F1570FB48502.sk1.eks-cluster.us-east-1.api.aws");
    EXPECT_EQ(result->ca_cert_pem, "hello-world");
    EXPECT_EQ(result->exec_command, "aws");
    EXPECT_EQ(result->exec_args,
              (std::vector<std::string>{"eks", "get-token", "--cluster-name", "compute-us-east-1-test-ebadeaux"}));
}

TEST(PodIdentityClient, ExtractKubeconfigFieldsMissingServerFails)
{
    std::string yaml = R"yaml(
clusters:
- cluster:
    certificate-authority-data: aGVsbG8td29ybGQ=
  name: cluster-name
users:
- name: user-name
  user:
    exec:
      apiVersion: client.authentication.k8s.io/v1beta1
      args:
      - eks
      - get-token
      - --cluster-name
      - compute-us-east-1-test-ebadeaux
      command: aws
      env: null
)yaml";
    std::vector<std::string> lines = absl::StrSplit(yaml, '\n');
    auto result = PodIdentityClientTest::ExtractKubeconfigFields(lines);
    EXPECT_FALSE(result.has_value());
}

TEST(PodIdentityClient, ExtractKubeconfigFieldsBadBase64Fails)
{
    std::string yaml = R"yaml(
clusters:
- cluster:
    certificate-authority-data: not-valid-base64!!!
    server: https://FF1DE699AA64BDA14ED2F1570FB48502.sk1.eks-cluster.us-east-1.api.aws
  name: cluster-name
users:
- name: user-name
  user:
    exec:
      apiVersion: client.authentication.k8s.io/v1beta1
      args:
      - eks
      - get-token
      - --cluster-name
      - compute-us-east-1-test-ebadeaux
      command: aws
      env: null
)yaml";
    std::vector<std::string> lines = absl::StrSplit(yaml, '\n');
    auto result = PodIdentityClientTest::ExtractKubeconfigFields(lines);
    EXPECT_FALSE(result.has_value());
}

TEST(PodIdentityClient, ParseExecCredentialTokenWellFormed)
{
    auto json = R"json({"kind":"ExecCredential","status":{"token":"k8s-aws-v1.abc","expirationTimestamp":"2026-08-18T19:34:02Z"}})json";
    auto result = PodIdentityClientTest::ParseExecCredentialToken(json);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "k8s-aws-v1.abc");
}

TEST(PodIdentityClient, ParseExecCredentialTokenMalformedJsonFails)
{
    EXPECT_FALSE(PodIdentityClientTest::ParseExecCredentialToken("not json{{{").has_value());
}

TEST(PodIdentityClient, ParseExecCredentialTokenMissingStatusFails)
{
    EXPECT_FALSE(PodIdentityClientTest::ParseExecCredentialToken("{}").has_value());
}

TEST(PodIdentityClient, ParseExecCredentialTokenMissingTokenFails)
{
    EXPECT_FALSE(PodIdentityClientTest::ParseExecCredentialToken(R"json({"status":{}})json").has_value());
}

TEST(PodIdentityClient, BuildExecCommandLineQuotesArgs)
{
    auto result = PodIdentityClientTest::BuildExecCommandLine("aws", {"eks", "get-token", "--cluster-name", "my-cluster"});
    EXPECT_EQ(result, "'aws' 'eks' 'get-token' '--cluster-name' 'my-cluster'");
}

TEST(PodIdentityClient, BuildExecCommandLineEscapesEmbeddedSingleQuote)
{
    auto result = PodIdentityClientTest::BuildExecCommandLine("aws", {"o'brien"});
    // Tracing ShellQuoteSingleArg("o'brien"): opening quote, 'o', then the embedded quote is
    // replaced by close-quote + backslash-escaped-quote + reopen-quote ('\''), then "brien",
    // then the closing quote -- i.e. 'o'\''brien'.
    EXPECT_EQ(result, "'aws' 'o'\\''brien'");
}

TEST(PodIdentityClient, BuildApiServerUrlAppendsPodsPath)
{
    auto result = PodIdentityClientTest::BuildApiServerUrl("https://example.com", "i-002f70ede9e03494f");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/api/v1/pods?fieldSelector=spec.nodeName=i-002f70ede9e03494f");
}

TEST(PodIdentityClient, BuildApiServerUrlStripsTrailingSlash)
{
    auto result = PodIdentityClientTest::BuildApiServerUrl("https://example.com/", "i-002f70ede9e03494f");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/api/v1/pods?fieldSelector=spec.nodeName=i-002f70ede9e03494f");
}

TEST(PodIdentityClient, BuildApiServerUrlEmptyInputsFail)
{
    EXPECT_FALSE(PodIdentityClientTest::BuildApiServerUrl("", "i-002f70ede9e03494f").has_value());
    EXPECT_FALSE(PodIdentityClientTest::BuildApiServerUrl("https://example.com", "").has_value());
}

}  // namespace
