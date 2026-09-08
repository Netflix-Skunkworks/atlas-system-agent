#pragma once

#include <lib/http_client/src/http_client.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace atlasagent
{

// What the kubelet's local API knows about a pod; nothing about cgroups.
struct PodIdentity
{
    std::string name;
    std::string pod_namespace;  // "namespace" is a reserved C++ keyword, cannot be a field name
    // Container id (bare hex, "containerd://" scheme prefix stripped) -> container name, merged from
    // status.containerStatuses[] and status.initContainerStatuses[] -- the latter is where a native
    // sidecar is reported. Empty when the pod has started no container yet, rather than a parse
    // failure. Never holds an empty-string key. See CollectContainerNames in the .cpp.
    std::unordered_map<std::string, std::string> containers;
    // Pod annotations from metadata.annotations. Empty if the pod has none (not a parse failure).
    std::unordered_map<std::string, std::string> annotations;
    // Pod labels from metadata.labels. Empty if the pod has none (not a parse failure).
    std::unordered_map<std::string, std::string> labels;
    // Container NAME -> its resources.requests.cpu in cores, from spec.containers[] and
    // spec.initContainers[]. Keyed by name because that is how the pod spec identifies containers,
    // and the caller (TrackedPodRegistry::ReconcileContainers) already holds it. A container that
    // declares no CPU request (BestEffort pods have none) is ABSENT rather than present with a zero.
    std::unordered_map<std::string, double> cpu_requests;
};

// Pod UID (kubelet's canonical dashed form) -> that pod's identity.
using PodIdentityMap = absl::flat_hash_map<std::string, PodIdentity>;

struct PodIdentityClientConstants
{
    // Kubelet's own local, unauthenticated read-only API -- a v1.PodList of every pod on this node.
    // No fieldSelector/node-name scoping is needed: kubelet cannot see any other node's pods.
    static constexpr auto KubeletUrl = "http://localhost:10255";
};

class PodIdentityClient
{
   public:
    // Cannot fail: no file read, no certificate, no subprocess -- just stores the URL, builds a client.
    explicit PodIdentityClient(Registry* registry,
                                std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // One synchronous, uncached GET to kubelet's local /pods. Returns nullopt if the HTTP call
    // failed or the response didn't parse as a pod list -- never throws.
    [[nodiscard]] std::optional<PodIdentityMap> FetchPodIdentities() const noexcept;

   protected:  // exposed to tests via a PodIdentityClientTest subclass
    static std::optional<PodIdentityMap> ParsePodList(const std::string& json) noexcept;

   private:
    std::string kubelet_url_;
    HttpClient http_client_;
};

}  // namespace atlasagent
