#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace atlasagent
{

// Annotation/label key names ResolvePodTags() checks below, per the Netflix K8s-native
// observability design (a mutating admission webhook stamps netflix.com/{app,stack,detail}
// pod annotations as the tagging source of truth; the app.kubernetes.io/*/k8s-app/app labels
// are this agent's own fallback tier for pods that webhook hasn't (yet) annotated). Public
// (not local to the .cpp) so debug tooling -- find-activepods's "filtered" mode -- can
// explain a Gating failure by naming the exact keys it checked, without duplicating this
// list into a second file.
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

// Pure fallback-chain resolution of one pod's tags from its own already-resolved annotations
// and labels (PodIdentity::annotations/labels, populated from kubelet's local /pods endpoint
// -- see pod_identity_client.{h,cpp}) plus this agent's own K8S_CLUSTER value (may be
// empty). No I/O -- easily unit-testable with canned input maps. Mirrors the Netflix
// K8s-native observability design (a mutating admission webhook stamps netflix.com/app,
// netflix.com/stack, netflix.com/detail annotations onto every pod as the tagging source of
// truth): primary annotation tier, then (only when still unset) a label fallback tier --
// app.kubernetes.io/name -> k8s-app -> app for nf.app, app.kubernetes.io/instance for
// nf.stack, app.kubernetes.io/component for nf.detail -- covering pods that webhook hasn't
// (yet) annotated. nf.cluster is gated on the *primary* netflix.com/app annotation
// specifically, never on a label-fallback-resolved nf.app (a deliberate asymmetry). Returns
// nullopt if NONE of nf.app/nf.stack/nf.detail resolved -- Gating: no metrics for any
// container in this pod. nf.node/nf.process are deliberately excluded from the Gating
// decision -- they're always structurally available once a pod's identity resolves at all
// (nf.node from pod_name here; nf.process from the caller, per-container -- see
// TrackedPodRegistry::ReconcileContainers), so including them would make Gating vacuous.
// Otherwise the returned map holds nf.app/nf.stack/nf.detail/nf.cluster (whichever resolved),
// nf.node=pod_name if pod_name is non-empty, nf.platform="k8s" (unconditionally), and, if
// k8s_cluster is non-empty, k8s.cluster.name. nf.process is NOT set here -- it's per-container,
// applied by the caller.
[[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> ResolvePodTags(
    const std::unordered_map<std::string, std::string>& annotations,
    const std::unordered_map<std::string, std::string>& labels, const std::string& pod_name,
    const std::string& k8s_cluster) noexcept;

}  // namespace atlasagent
