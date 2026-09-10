#include <lib/collectors/pod_monitor/src/util/pod_tag_resolver.h>

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

namespace
{

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
    auto componentLabelOnly =
        atlasagent::ResolvePodTags({}, {{"app.kubernetes.io/component", "labeldetail"}}, "", "");
    ASSERT_TRUE(componentLabelOnly.has_value());
    EXPECT_TRUE(componentLabelOnly->empty());
}

}  // namespace
