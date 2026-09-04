#include "pod_identity_client.h"
#include <lib/logger/src/logger.h>

#include <rapidjson/document.h>

namespace atlasagent
{

namespace
{

// containerID is shaped like "containerd://<64-hex-id>" (scheme varies by runtime); strip
// everything up to and including the first "://" to match the bare hex id the cgroup scope
// directory name itself carries. Returns the input unchanged if there's no "://" to strip.
std::string StripContainerIdScheme(const std::string& container_id) noexcept
{
    auto pos = container_id.find("://");
    if (pos == std::string::npos)
    {
        return container_id;
    }
    return container_id.substr(pos + 3);
}

// Copies every string-valued member of a JSON object into a map -- shared by
// metadata.annotations and metadata.labels, which have the identical shape. A non-string value
// is skipped defensively rather than failing the whole parse (matches this file's existing
// treatment of malformed containerStatuses entries).
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

}  // namespace

PodIdentityClient::PodIdentityClient(Registry* registry, std::string kubelet_url) noexcept
    : kubelet_url_(std::move(kubelet_url)), http_client_{registry, HttpClientConfig{absl::Seconds(2), absl::Seconds(3), ""}}
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

        // status.containerStatuses is absent for a pod that hasn't started any containers yet --
        // leave `containers` empty for it rather than failing the whole pod's parse.
        if (entry.HasMember("status") && entry["status"].IsObject())
        {
            const auto& status = entry["status"];
            if (status.HasMember("containerStatuses") && status["containerStatuses"].IsArray())
            {
                for (const auto& container : status["containerStatuses"].GetArray())
                {
                    if (!container.IsObject() || !container.HasMember("name") || !container["name"].IsString() ||
                        !container.HasMember("containerID") || !container["containerID"].IsString())
                    {
                        Logger()->debug("Skipping containerStatuses entry with incomplete name/containerID");
                        continue;
                    }
                    auto container_id = StripContainerIdScheme(container["containerID"].GetString());
                    identity.containers.emplace(std::move(container_id), container["name"].GetString());
                }
            }
        }

        result.emplace(metadata["uid"].GetString(), std::move(identity));
    }

    return result;
}

}  // namespace atlasagent
