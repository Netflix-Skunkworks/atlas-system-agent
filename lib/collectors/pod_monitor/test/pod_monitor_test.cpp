#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/collectors/pod_monitor/src/pod_identity_client.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <absl/strings/str_split.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
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
