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
    // containerID after removing everything through its first "://" delimiter (or unchanged when
    // there is no delimiter) -> container name, merged from status.containerStatuses[] and
    // status.initContainerStatuses[]. The latter is where a native sidecar is reported. Empty when
    // neither parsed status array yields a usable non-empty id. Ephemeral-container statuses are
    // intentionally not included; see ParsePodList and ReconcileContainers. Never holds an
    // empty-string key.
    std::unordered_map<std::string, std::string> containers;
    // String-valued pod annotations from metadata.annotations. Empty when the field is absent or not
    // an object, or when it contains no string-valued entries.
    std::unordered_map<std::string, std::string> annotations;
    // String-valued pod labels from metadata.labels. Empty under the same conditions as annotations.
    std::unordered_map<std::string, std::string> labels;
    // Container NAME -> its resources.requests.cpu in cores, from spec.containers[] and
    // spec.initContainers[]. Keyed by name because that is how the pod spec identifies containers,
    // and the caller (TrackedPodRegistry::ReconcileContainers) already holds it. A container is
    // ABSENT rather than present with zero when it declares no request or its quantity cannot be
    // parsed by ParseCpuQuantity().
    std::unordered_map<std::string, double> cpu_requests;
};

// Pod UID string exactly as returned by kubelet -> that pod's identity. ParsePodList does not
// normalize or validate the UID; joining succeeds only when it matches the cgroup-discovered key.
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
    // Performs no I/O: stores the URL and constructs the HTTP client used by later fetches.
    explicit PodIdentityClient(Registry* registry,
                                std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // One synchronous, uncached logical GET to kubelet's local /pods, subject to HttpClient's retry
    // policy. Reports HTTP failures and invalid top-level pod-list envelopes as nullopt; malformed
    // individual pod/status entries are skipped, so a successful result may be partial or empty.
    [[nodiscard]] std::optional<PodIdentityMap> FetchPodIdentities() const noexcept;

   protected:  // exposed to tests via a PodIdentityClientTest subclass
    static std::optional<PodIdentityMap> ParsePodList(const std::string& json) noexcept;

   private:
    std::string kubelet_url_;
    HttpClient http_client_;
};

}  // namespace atlasagent
