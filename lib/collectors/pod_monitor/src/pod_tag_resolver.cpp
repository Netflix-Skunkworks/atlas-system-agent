#include "pod_tag_resolver.h"

namespace atlasagent
{

namespace
{

// A key present in `values` with a non-empty value; nullopt otherwise (missing, or present but
// empty -- both treated as "not set" per the fallback-chain wording).
std::optional<std::string> NonEmptyValue(const std::unordered_map<std::string, std::string>& values,
                                         std::string_view key) noexcept
{
    auto it = values.find(std::string(key));
    if (it != values.end() && !it->second.empty())
    {
        return it->second;
    }
    return std::nullopt;
}

// The first non-empty value among `keys`, in order; nullopt if none of them are set. The
// label-fallback half of ResolvePodTags's primary-annotation-else-label-fallback pattern, shared
// across nf.app (3 candidate keys)/nf.stack/nf.detail (1 each).
std::optional<std::string> FirstNonEmptyValue(const std::unordered_map<std::string, std::string>& values,
                                               std::initializer_list<std::string_view> keys) noexcept
{
    for (auto key : keys)
    {
        if (auto value = NonEmptyValue(values, key); value.has_value())
        {
            return value;
        }
    }
    return std::nullopt;
}

// nf.cluster is gated on the *primary* netflix.com/app annotation specifically -- NOT on
// whatever nf_app ended up resolving to. A pod whose nf.app only resolved via a label fallback
// must not get an nf.cluster tag; this mirrors the Netflix K8s-native observability design's own
// `where resource.attributes["netflix.app"] != nil` guards, which all key off the primary
// attribute, not the transform's own output.
std::optional<std::string> BuildNfCluster(const std::optional<std::string>& primary_app,
                                           const std::optional<std::string>& primary_stack,
                                           const std::optional<std::string>& primary_detail) noexcept
{
    if (!primary_app.has_value())
    {
        return std::nullopt;
    }
    std::string cluster = *primary_app;
    if (primary_stack.has_value())
    {
        cluster += "-";
        cluster += *primary_stack;
    }
    if (primary_detail.has_value())
    {
        cluster += "-";
        cluster += *primary_detail;
    }
    return cluster;
}

}  // namespace

std::optional<std::unordered_map<std::string, std::string>> ResolvePodTags(
    const std::unordered_map<std::string, std::string>& annotations,
    const std::unordered_map<std::string, std::string>& labels, const std::string& pod_name,
    const std::string& k8s_cluster) noexcept
{
    auto primary_app = NonEmptyValue(annotations, PodTagKeys::kAnnotationApp);
    auto primary_stack = NonEmptyValue(annotations, PodTagKeys::kAnnotationStack);
    auto primary_detail = NonEmptyValue(annotations, PodTagKeys::kAnnotationDetail);

    // primary_app/primary_stack/primary_detail are kept separate from nf_app/nf_stack/nf_detail
    // below -- nf.cluster (see BuildNfCluster) needs the primary-only values specifically, not
    // whatever the label fallback resolved.
    auto nf_app = primary_app.has_value()
                      ? primary_app
                      : FirstNonEmptyValue(labels, {PodTagKeys::kLabelAppName, PodTagKeys::kLabelK8sApp, PodTagKeys::kLabelApp});
    auto nf_stack = primary_stack.has_value() ? primary_stack : FirstNonEmptyValue(labels, {PodTagKeys::kLabelAppInstance});
    auto nf_detail = primary_detail.has_value() ? primary_detail : FirstNonEmptyValue(labels, {PodTagKeys::kLabelAppComponent});

    if (!nf_app.has_value() && !nf_stack.has_value() && !nf_detail.has_value())
    {
        // Gating: none of the three identity-bearing keys resolved (annotation or label
        // fallback) -- no metrics for any container in this pod. nf.node/nf.process are
        // deliberately NOT part of this check -- they're always structurally available once a
        // pod's identity resolves at all, so including them would make Gating vacuous.
        return std::nullopt;
    }

    auto nf_cluster = BuildNfCluster(primary_app, primary_stack, primary_detail);

    std::unordered_map<std::string, std::string> tags;
    if (nf_app.has_value())
    {
        tags.emplace("nf.app", *nf_app);
    }
    if (nf_stack.has_value())
    {
        tags.emplace("nf.stack", *nf_stack);
    }
    // todo uncomment later
    // if (nf_detail.has_value())
    // {
    //     tags.emplace("nf.detail", *nf_detail);
    // }
    if (nf_cluster.has_value())
    {
        tags.emplace("nf.cluster", *nf_cluster);
    }

    if (!pod_name.empty())
    {
        tags.emplace("nf.node", pod_name);
    }
    if (!k8s_cluster.empty())
    {
        tags.emplace("k8s.cluster.name", k8s_cluster);
    }
    // todo uncomment later
    //tags.emplace("nf.platform", "k8s");

    return tags;
}

}  // namespace atlasagent
