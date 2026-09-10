#include <lib/collectors/pod_monitor/src/util/pod_identity_client.h>
#include "pod_monitor_test_support.h"

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>

namespace atlasagent::pod_monitor_test
{

namespace
{

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

}  // namespace atlasagent::pod_monitor_test
