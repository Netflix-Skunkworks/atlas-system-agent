#pragma once

#include <lib/http_client/src/http_client.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>
#include <absl/time/time.h>

#include <optional>
#include <string>

namespace atlasagent
{

// What the apiserver knows about a pod; nothing about cgroups.
struct PodIdentity
{
    std::string name;
    std::string pod_namespace;  // "namespace" is a reserved C++ keyword, cannot be a field name
};

// Pod UID (apiserver's canonical dashed form) -> that pod's identity.
using PodIdentityMap = absl::flat_hash_map<std::string, PodIdentity>;

struct PodIdentityClientConstants
{
    static constexpr auto TokenPath = "/var/run/secrets/kubernetes.io/serviceaccount/token";
    // The cluster CA bundle projected into every pod; passed to HttpClient as
    // HttpClientConfig::ca_cert_path so the in-cluster apiserver call's TLS handshake verifies
    // against the cluster's own (normally self-signed) CA rather than the OS default trust store.
    static constexpr auto CaCertPath = "/var/run/secrets/kubernetes.io/serviceaccount/ca.crt";
    static constexpr auto NodeNameEnvVar = "NODE_NAME";
    static constexpr auto ApiServerHostEnvVar = "KUBERNETES_SERVICE_HOST";
    static constexpr auto ApiServerPortEnvVar = "KUBERNETES_SERVICE_PORT";
};

class PodIdentityClient
{
   public:
    explicit PodIdentityClient(Registry* registry) noexcept
        : registry_(registry),
          http_client_{registry,
                       HttpClientConfig{absl::Seconds(2), absl::Seconds(3), PodIdentityClientConstants::CaCertPath}}
    {
    }

    // One synchronous, uncached apiserver call: rereads the (rotating) ServiceAccount token
    // fresh from disk, reads host/port/node-name from the environment, issues one GET to
    // /api/v1/pods?fieldSelector=spec.nodeName=<node>, and parses the response. Returns
    // nullopt if the call could not be attempted at all (missing env/token) or failed
    // outright (transport error, non-200, malformed top-level JSON shape) -- never throws.
    [[nodiscard]] std::optional<PodIdentityMap> FetchPodIdentities() const noexcept;

   protected:  // exposed to tests via a PodIdentityClientTest subclass, same pattern used by
               // PodMonitorTest for PodMonitor's protected statics
    static std::optional<PodIdentityMap> ParsePodList(const std::string& json) noexcept;
    static std::optional<std::string> BuildApiServerUrl(const std::string& host, const std::string& port,
                                                          const std::string& node_name) noexcept;

   private:
    Registry* registry_;
    HttpClient http_client_;
};

}  // namespace atlasagent
