#pragma once

#include <lib/http_client/src/http_client.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>
#include <absl/time/time.h>

#include <optional>
#include <string>
#include <vector>

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

// The handful of exec-credential-plugin + cluster-connection fields needed out of
// /run/kubernetes/config. NOT a general YAML parser -- Netflix's kubelet provisioning always
// writes this file in this exact shape (one cluster, one user, one exec block calling
// "aws eks get-token"). A file with multiple clusters/users/contexts would silently produce the
// wrong result: scalar fields (server, certificate-authority-data, command) keep the LAST match
// in file order, and a second "args:" block would append its items onto the first rather than
// either block winning outright.
struct KubeconfigInfo
{
    std::string server;
    std::string ca_cert_pem;
    std::string exec_command;
    std::vector<std::string> exec_args;
};

struct PodIdentityClientConstants
{
    static constexpr auto KubeconfigPath = "/run/kubernetes/config";
    static constexpr auto CaCertWritePath = "/run/atlas-system-agent/apiserver-ca.crt";
    static constexpr auto InstanceIdEnvVar = "NETFLIX_INSTANCE_ID";
    static constexpr int ExecCredentialTimeoutMillis = 2000;
};

class PodIdentityClient
{
   public:
    // Reads and parses /run/kubernetes/config ONCE (the cluster URL, CA bundle, and exec
    // command+args are static for this process's lifetime) and writes the decoded CA bundle to
    // CaCertWritePath once. If the kubeconfig is missing/unparseable, the client is
    // permanently disabled: every FetchPodIdentities() call thereafter returns nullopt
    // immediately (logged once here at construction, not spammed per call).
    explicit PodIdentityClient(Registry* registry,
                                std::string kubeconfig_path = PodIdentityClientConstants::KubeconfigPath) noexcept;

    // One synchronous, uncached apiserver call: re-runs the kubeconfig's exec-credential
    // plugin fresh (tokens are short-lived, ~15 minutes observed) for a bearer token, re-reads
    // NETFLIX_INSTANCE_ID fresh from the environment, and issues one GET to
    // {kubeconfig server}/api/v1/pods?fieldSelector=spec.nodeName=<instance-id>. Returns
    // nullopt if construction failed, the exec plugin couldn't be run/parsed,
    // NETFLIX_INSTANCE_ID is unset, or the HTTP call failed outright -- never throws.
    [[nodiscard]] std::optional<PodIdentityMap> FetchPodIdentities() const noexcept;

   protected:  // exposed to tests via a PodIdentityClientTest subclass
    static std::optional<PodIdentityMap> ParsePodList(const std::string& json) noexcept;
    static std::optional<KubeconfigInfo> ExtractKubeconfigFields(const std::vector<std::string>& lines) noexcept;
    static std::optional<std::string> ParseExecCredentialToken(const std::string& json) noexcept;
    static std::string BuildExecCommandLine(const std::string& command, const std::vector<std::string>& args) noexcept;
    static std::optional<std::string> BuildApiServerUrl(const std::string& server, const std::string& node_name) noexcept;

   private:
    Registry* registry_;
    HttpClient http_client_;
    bool initialized_;
    std::string apiserver_server_;
    std::string exec_command_line_;
};

}  // namespace atlasagent
