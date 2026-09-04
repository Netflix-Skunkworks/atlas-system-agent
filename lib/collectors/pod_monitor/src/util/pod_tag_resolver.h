#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace atlasagent
{

// Annotation/label keys ResolvePodTags() checks. netflix.com/{app,stack,detail} annotations are
// the primary source (stamped by a mutating admission webhook); the app.kubernetes.io/*/k8s-app/
// app labels are the fallback tier for pods the webhook hasn't annotated yet. Public (not
// .cpp-local) so find-activepods's "filtered" mode can report exactly which keys it checked,
// without duplicating this list.
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

// Pure fallback-chain resolution of one pod's tags from its own resolved annotations/labels
// (PodIdentity::annotations/labels) and this agent's K8S_CLUSTER (may be empty) -- no I/O,
// easily unit-tested with canned maps. Primary tier is the netflix.com/{app,stack,detail}
// annotations (stamped by the K8s-native observability admission webhook); label fallback, used
// only when the primary is unset, is app.kubernetes.io/name -> k8s-app -> app for nf.app,
// app.kubernetes.io/instance for nf.stack, app.kubernetes.io/component for nf.detail. nf.cluster
// is built only from the *primary* netflix.com/{app,stack,detail} annotations, never from
// label-fallback-resolved nf.app/nf.stack/nf.detail -- so a stack/detail resolved only via label
// fallback is omitted from nf.cluster's suffix even though nf.stack itself still reflects it
// (deliberate asymmetry). Returns nullopt (Gating: no metrics for any container in this pod) if
// none of nf.app/nf.stack/nf.detail resolved; nf.node/nf.process are excluded from that decision
// since they're always available once identity resolves at all, which would make Gating vacuous.
// Otherwise returns whichever of nf.app/nf.stack/nf.cluster resolved, nf.node=pod_name if
// non-empty, and k8s.cluster.name if k8s_cluster is non-empty; nf.process is NOT set here,
// applied by the caller per-container. nf.detail and nf.platform are resolved/used internally
// (Gating, nf.cluster) but NOT currently included in the returned map -- their tag-emplace calls
// are disabled in pod_tag_resolver.cpp pending a "todo uncomment later".
[[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> ResolvePodTags(
    const std::unordered_map<std::string, std::string>& annotations,
    const std::unordered_map<std::string, std::string>& labels, const std::string& pod_name,
    const std::string& k8s_cluster) noexcept;

}  // namespace atlasagent
