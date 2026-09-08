#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace atlasagent
{

// Annotation/label keys ResolvePodTags() checks. The netflix.com/{app,stack,detail} annotations are
// primary (stamped by a mutating admission webhook); the app.kubernetes.io/*, k8s-app, and app
// labels are the fallback tier for pods the webhook hasn't annotated yet. Public so
// find-activepods's "filtered" mode can report which keys it checked without duplicating the list.
struct PodTagKeys
{
    static constexpr std::string_view kAnnotationApp = "netflix.com/app";
    static constexpr std::string_view kAnnotationStack = "netflix.com/stack";
    static constexpr std::string_view kAnnotationDetail = "netflix.com/detail";
    static constexpr std::string_view kLabelAppName = "app.kubernetes.io/name";
    static constexpr std::string_view kLabelK8sApp = "k8s-app";
    static constexpr std::string_view kLabelApp = "app";
    static constexpr std::string_view kLabelAppInstance = "app.kubernetes.io/instance";
    static constexpr std::string_view kLabelAppComponent = "app.kubernetes.io/component";
};

// Resolves one pod's tags from its own annotations/labels (PodIdentity's maps) and this agent's
// K8S_CLUSTER (may be empty) -- pure, no I/O. The netflix.com/* annotations above are the primary
// tier; the label fallback, used only where the primary is unset, is kLabelAppName -> kLabelK8sApp
// -> kLabelApp for nf.app, kLabelAppInstance for nf.stack, kLabelAppComponent for nf.detail.
//
// Returns nullopt (Gating: no metrics for any container in this pod) if none of
// nf.app/nf.stack/nf.detail resolved -- nf.node/nf.process are excluded from that decision because
// they are always available once identity resolves at all, which would make Gating vacuous.
// Otherwise returns whichever of nf.app/nf.stack/nf.cluster resolved, nf.node=pod_name if non-empty,
// and k8s.cluster.name if k8s_cluster is non-empty. nf.cluster uses the *primary* annotations only,
// so a stack/detail resolved via label fallback is left out of its suffix even though nf.stack still
// reflects it (deliberate -- see BuildNfCluster). nf.process is per-container, set by the caller.
// nf.detail is used internally (Gating, nf.cluster) but not returned -- its tag-emplace, and
// nf.platform's, are disabled in the .cpp pending a "todo uncomment later".
[[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> ResolvePodTags(
    const std::unordered_map<std::string, std::string>& annotations,
    const std::unordered_map<std::string, std::string>& labels, const std::string& pod_name,
    const std::string& k8s_cluster) noexcept;

}  // namespace atlasagent
