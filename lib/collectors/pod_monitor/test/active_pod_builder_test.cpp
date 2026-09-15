#include <lib/collectors/pod_monitor/src/util/active_pod_builder.h>

#include <gtest/gtest.h>

#include <optional>

namespace
{

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

}  // namespace
