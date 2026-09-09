#pragma once

#include <lib/http_client/src/http_client.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace atlasagent
{

// A container's kubelet identity joined with its declared CPU request. The map containing this
// value is keyed by the runtime id from the parsed status arrays, because that is the id that can
// be joined with a cgroup scope. The request is optional: absent means the spec did not declare a
// usable requests.cpu value.
struct ContainerIdentity
{
    std::string name;
    std::optional<double> cpu_request;
};

// What the kubelet's local API knows about a pod; nothing about cgroups.
struct PodIdentity
{
    std::string name;
    std::string pod_namespace;  // "namespace" is a reserved C++ keyword, cannot be a field name
    // ContainerIdentity keyed by containerID after removing everything through its first "://"
    // delimiter (or unchanged when there is no delimiter), merged from status.containerStatuses[]
    // and status.initContainerStatuses[]. The latter is where a native sidecar is reported. Empty
    // when neither parsed status array yields a usable non-empty id. Ephemeral-container statuses
    // are intentionally not included; see ParsePodList and BuildActivePods. Never holds an
    // empty-string key.
    std::unordered_map<std::string, ContainerIdentity> containers;
    // String-valued pod annotations from metadata.annotations. Empty when the field is absent or not
    // an object, or when it contains no string-valued entries.
    std::unordered_map<std::string, std::string> annotations;
    // String-valued pod labels from metadata.labels. Empty under the same conditions as annotations.
    std::unordered_map<std::string, std::string> labels;
};

// Pod UID string exactly as returned by kubelet -> that pod's identity. ParsePodList does not
// normalize or validate the UID; joining succeeds only when it matches the cgroup-discovered key.
using PodIdentityMap = absl::flat_hash_map<std::string, PodIdentity>;

enum class PodIdentityErrorKind
{
    UnavailableSource,
    Http,
    Parse,
    Envelope,
};

struct PodIdentityError
{
    PodIdentityErrorKind kind;
    int http_status = 0;
    std::size_t response_size = 0;
    std::size_t parse_offset = 0;
};

using PodIdentityResult = std::expected<PodIdentityMap, PodIdentityError>;

[[nodiscard]] std::string_view ToString(PodIdentityErrorKind error) noexcept;

class PodIdentitySource
{
   public:
    virtual ~PodIdentitySource() = default;

    [[nodiscard]] virtual PodIdentityResult FetchPodIdentities() const noexcept = 0;
};

struct PodIdentityClientConstants
{
    // Kubelet's own local, unauthenticated read-only API -- a v1.PodList of every pod on this node.
    // No fieldSelector/node-name scoping is needed: kubelet cannot see any other node's pods.
    static constexpr auto KubeletUrl = "http://localhost:10255";
};

class PodIdentityClient : public PodIdentitySource
{
   public:
    // Performs no I/O: stores the URL and constructs the HTTP client used by later fetches.
    explicit PodIdentityClient(Registry* registry,
                                std::string kubelet_url = PodIdentityClientConstants::KubeletUrl) noexcept;

    // One synchronous, uncached logical GET to kubelet's local /pods, subject to HttpClient's retry
    // policy. HTTP failures, malformed JSON, and invalid top-level pod-list envelopes are distinct
    // errors; malformed individual pod/status entries are skipped, so a successful result may be
    // partial or empty.
    [[nodiscard]] PodIdentityResult FetchPodIdentities() const noexcept override;

   protected:  // exposed to tests via a PodIdentityClientTest subclass
    static PodIdentityResult ParsePodList(const std::string& json) noexcept;

   private:
    std::string kubelet_url_;
    HttpClient http_client_;
};

}  // namespace atlasagent
