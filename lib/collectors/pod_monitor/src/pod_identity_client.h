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
    // Container id (bare hex, scheme prefix like "containerd://" stripped) -> container name,
    // from status.containerStatuses[]. Empty when the pod has no containerStatuses yet (e.g. not
    // started) rather than treated as a parse failure.
    std::unordered_map<std::string, std::string> containers;
    // Pod annotations from metadata.annotations. Empty if the pod has none (not a parse failure).
    std::unordered_map<std::string, std::string> annotations;
    // Pod labels from metadata.labels. Empty if the pod has none (not a parse failure).
    std::unordered_map<std::string, std::string> labels;
};

// Pod UID (kubelet's canonical dashed form) -> that pod's identity.
using PodIdentityMap = absl::flat_hash_map<std::string, PodIdentity>;

struct PodIdentityClientConstants
{
    // Kubelet's own local, unauthenticated read-only API -- returns a v1.PodList of every pod
    // currently assigned to this node. Kubelet has no visibility into any other node's pods, so
    // (unlike the apiserver-based mechanism this replaced) no fieldSelector/node-name lookup is
    // needed to scope the response.
    static constexpr auto KubeletUrl = "http://localhost:10255";
};

class PodIdentityClient
{
   public:
    // Nothing here can fail: no file is read, no certificate is decoded/written, no subprocess
    // is run -- construction just stores the URL and builds an HttpClient.
    explicit PodIdentityClient(Registry* registry,
                                std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // One synchronous, uncached GET to kubelet's local /pods endpoint. Returns nullopt if the
    // HTTP call failed outright or the response didn't parse as a pod list -- never throws.
    [[nodiscard]] std::optional<PodIdentityMap> FetchPodIdentities() const noexcept;

   protected:  // exposed to tests via a PodIdentityClientTest subclass
    static std::optional<PodIdentityMap> ParsePodList(const std::string& json) noexcept;

   private:
    std::string kubelet_url_;
    HttpClient http_client_;
};

}  // namespace atlasagent
