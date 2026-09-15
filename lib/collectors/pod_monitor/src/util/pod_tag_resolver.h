#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace atlasagent
{

// Resolves one pod's tags from its own annotations/labels (PodIdentity's maps) and this agent's
// K8S_CLUSTER (may be empty) -- pure, no I/O. The netflix.com/{app,stack,detail} annotations are the
// primary tier. When a primary is unset, nf.app falls back through app.kubernetes.io/name, k8s-app,
// and app; nf.stack uses app.kubernetes.io/instance; nf.detail uses app.kubernetes.io/component.
//
// Returns nullopt (Gating: no metrics for any container in this pod) if none of
// nf.app/nf.stack/nf.detail resolved. nf.node/nf.process are excluded from that decision because
// they are derived from pod/container names rather than the app-identity annotations and labels;
// either tag may still be absent when its source string is empty.
// Otherwise returns whichever of nf.app/nf.stack/nf.cluster resolved, nf.node=pod_name if non-empty,
// and k8s.cluster.name if k8s_cluster is non-empty. nf.cluster uses the *primary* annotations only,
// so a stack/detail resolved via label fallback is left out of its suffix even though nf.stack still
// reflects it (deliberate -- see BuildNfCluster). nf.process is per-container, set by the caller.
// Resolved nf.detail participates in Gating, and the primary detail annotation can contribute to
// nf.cluster, but nf.detail itself is not returned. Its tag-emplace and nf.platform's are disabled
// in the .cpp pending a "todo uncomment later".
[[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> ResolvePodTags(
    const std::unordered_map<std::string, std::string>& annotations,
    const std::unordered_map<std::string, std::string>& labels, const std::string& pod_name,
    const std::string& k8s_cluster) noexcept;

}  // namespace atlasagent
