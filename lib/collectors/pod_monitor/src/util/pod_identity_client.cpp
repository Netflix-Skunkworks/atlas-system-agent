#include "pod_identity_client.h"

#include "cpu_quantity.h"

#include <lib/logger/src/logger.h>

#include <rapidjson/document.h>

namespace atlasagent
{

namespace
{

// containerID commonly looks like "containerd://<runtime-id>" (scheme varies by runtime). Remove
// everything through the first "://"; normal runtime values then match the id in the cgroup scope
// name. An additional delimiter in the remainder is intentionally left untouched.
std::string StripContainerIdScheme(const std::string& container_id) noexcept
{
    auto pos = container_id.find("://");
    if (pos == std::string::npos)
    {
        return container_id;
    }
    return container_id.substr(pos + 3);
}

// Shared by metadata.annotations and metadata.labels, which are identically shaped. A non-string
// value is skipped defensively rather than failing the whole parse.
std::unordered_map<std::string, std::string> ParseStringMap(const rapidjson::Value& obj) noexcept
{
    std::unordered_map<std::string, std::string> result;
    for (const auto& member : obj.GetObject())
    {
        if (member.value.IsString())
        {
            result.emplace(member.name.GetString(), member.value.GetString());
        }
    }
    return result;
}

// Container name -> resources.requests.cpu (in cores) out of one spec container array. Called for
// BOTH spec.containers[] and spec.initContainers[] so the two cannot drift: a native sidecar gets
// its own cgroup scope and runs the pod's whole lifetime, so omitting initContainers would leave its
// CPU request unavailable. Every level is optional and skipped rather than failing the pod's parse:
// no resources, only limits, or a value ParseCpuQuantity cannot represent all leave the container
// ABSENT from the map, never present with a fabricated zero.
void CollectCpuRequests(const rapidjson::Value& containers,
                        std::unordered_map<std::string, double>* cpu_requests) noexcept
{
    for (const auto& container : containers.GetArray())
    {
        if (!container.IsObject() || !container.HasMember("name") || !container["name"].IsString())
        {
            continue;
        }
        if (!container.HasMember("resources") || !container["resources"].IsObject())
        {
            continue;
        }
        const auto& resources = container["resources"];
        if (!resources.HasMember("requests") || !resources["requests"].IsObject())
        {
            continue;
        }
        const auto& requests = resources["requests"];
        if (!requests.HasMember("cpu") || !requests["cpu"].IsString())
        {
            continue;
        }

        if (auto cores = ParseCpuQuantity(requests["cpu"].GetString()); cores.has_value())
        {
            cpu_requests->emplace(container["name"].GetString(), *cores);
        }
        else
        {
            Logger()->debug("Skipping unparseable requests.cpu {} for container {}", requests["cpu"].GetString(),
                            container["name"].GetString());
        }
    }
}

// containerID after first-delimiter stripping -> container name out of one status array. Called for BOTH
// status.containerStatuses[] and status.initContainerStatuses[] so the two cannot drift; keyed by
// id, unique per container, so merging them into one map cannot collide.
//
// This map is the ONLY thing that attributes a cgroup-discovered scope to anything: the scope
// directory carries a runtime id, and `status` is the only place ids exist -- spec has names but no
// ids (assigned when the runtime creates the container), so spec alone cannot resolve a container.
//
// So initContainerStatuses is no edge case: a NATIVE SIDECAR (an initContainer with
// restartPolicy=Always, k8s 1.28+) is reported there rather than in containerStatuses, and runs the
// pod's whole lifetime with its own cgroup scope. While this array went unparsed, such a container
// was discovered and skipped every cycle -- permanently and silently, since ReconcileContainers
// matches discovered ids against this map.
void CollectContainerNames(const rapidjson::Value& statuses,
                           std::unordered_map<std::string, std::string>* containers) noexcept
{
    for (const auto& container : statuses.GetArray())
    {
        if (!container.IsObject() || !container.HasMember("name") || !container["name"].IsString() ||
            !container.HasMember("containerID") || !container["containerID"].IsString())
        {
            Logger()->debug("Skipping container status entry with incomplete name/containerID");
            continue;
        }
        auto container_id = StripContainerIdScheme(container["containerID"].GetString());
        if (container_id.empty())
        {
            // A container still in `waiting` (ImagePullBackOff, CreateContainerError) reports an EMPTY
            // containerID rather than none, which passes IsString() above. Emplacing it adds a key no
            // cgroup scope can match, and every not-yet-started container would contend for that "" key.
            Logger()->debug("Skipping container status entry for {} with an empty containerID",
                            container["name"].GetString());
            continue;
        }
        containers->emplace(std::move(container_id), container["name"].GetString());
    }
}

}  // namespace

PodIdentityClient::PodIdentityClient(Registry* registry, std::string kubelet_url) noexcept
    : kubelet_url_(std::move(kubelet_url)), http_client_{registry, HttpClientConfig{absl::Seconds(2), absl::Seconds(3)}}
{
}

std::optional<PodIdentityMap> PodIdentityClient::FetchPodIdentities() const noexcept
{
    auto resp = http_client_.Get(kubelet_url_ + "/pods");
    if (resp.status != 200)
    {
        Logger()->warn("Unable to fetch pod identities from {}/pods: status={} body={}", kubelet_url_, resp.status,
                        resp.raw_body);
        return std::nullopt;
    }

    return ParsePodList(resp.raw_body);
}

std::optional<PodIdentityMap> PodIdentityClient::ParsePodList(const std::string& json) noexcept
{
    rapidjson::Document doc;
    doc.Parse(json.c_str(), json.length());
    if (doc.HasParseError())
    {
        Logger()->warn("Unable to parse pod list response as JSON: {}", json);
        return std::nullopt;
    }

    if (!doc.IsObject())
    {
        Logger()->warn("Pod list response is not a JSON object: {}", json);
        return std::nullopt;
    }

    if (!doc.HasMember("items") || !doc["items"].IsArray())
    {
        Logger()->warn("Pod list response has no 'items' array: {}", json);
        return std::nullopt;
    }

    PodIdentityMap result;
    for (const auto& entry : doc["items"].GetArray())
    {
        if (!entry.IsObject() || !entry.HasMember("metadata") || !entry["metadata"].IsObject())
        {
            Logger()->debug("Skipping pod list entry with no metadata object");
            continue;
        }

        const auto& metadata = entry["metadata"];
        if (!metadata.HasMember("uid") || !metadata["uid"].IsString() || !metadata.HasMember("name") ||
            !metadata["name"].IsString() || !metadata.HasMember("namespace") || !metadata["namespace"].IsString())
        {
            Logger()->debug("Skipping pod list entry with incomplete metadata");
            continue;
        }

        PodIdentity identity{metadata["name"].GetString(), metadata["namespace"].GetString(), {}, {}, {}};

        if (metadata.HasMember("annotations") && metadata["annotations"].IsObject())
        {
            identity.annotations = ParseStringMap(metadata["annotations"]);
        }
        if (metadata.HasMember("labels") && metadata["labels"].IsObject())
        {
            identity.labels = ParseStringMap(metadata["labels"]);
        }

        // spec's DECLARED resources are the only source for a container's CPU request -- the cgroup
        // filesystem exposes the limit (cpu.max), not the request. Both arrays are optional: a pod
        // may declare no initContainers, and a static/mirror pod may omit resources entirely.
        if (entry.HasMember("spec") && entry["spec"].IsObject())
        {
            const auto& spec = entry["spec"];
            if (spec.HasMember("containers") && spec["containers"].IsArray())
            {
                CollectCpuRequests(spec["containers"], &identity.cpu_requests);
            }
            if (spec.HasMember("initContainers") && spec["initContainers"].IsArray())
            {
                CollectCpuRequests(spec["initContainers"], &identity.cpu_requests);
            }
        }

        // status is the only source of runtime ids -- see CollectContainerNames, including for why
        // initContainerStatuses matters. Both arrays are optional, and entries with missing or empty
        // ids are skipped. If neither array yields a usable id, `containers` remains empty without
        // failing the whole pod's parse.
        if (entry.HasMember("status") && entry["status"].IsObject())
        {
            const auto& status = entry["status"];
            if (status.HasMember("containerStatuses") && status["containerStatuses"].IsArray())
            {
                CollectContainerNames(status["containerStatuses"], &identity.containers);
            }
            if (status.HasMember("initContainerStatuses") && status["initContainerStatuses"].IsArray())
            {
                CollectContainerNames(status["initContainerStatuses"], &identity.containers);
            }
            // NOT parsed: status.ephemeralContainerStatuses[] (kubectl debug containers). Those do
            // get their own cgroup scope, so they land on the skip path native sidecars used to --
            // but each one's name would become a new nf.process value, i.e. a fresh Atlas series per
            // debug session. Left out pending that call (adding it is one more CollectContainerNames
            // call here); ReconcileContainers' skip-path debug log keeps the gap diagnosable.
        }

        result.emplace(metadata["uid"].GetString(), std::move(identity));
    }

    return result;
}

}  // namespace atlasagent
