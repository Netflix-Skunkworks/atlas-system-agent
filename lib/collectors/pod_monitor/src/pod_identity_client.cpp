#include "pod_identity_client.h"
#include <lib/util/src/util.h>
#include <lib/logger/src/logger.h>

#include <rapidjson/document.h>
#include <fmt/format.h>

#include <cstdlib>

namespace atlasagent
{

std::optional<PodIdentityMap> PodIdentityClient::FetchPodIdentities() const noexcept
{
    auto token = read_file_to_string(PodIdentityClientConstants::TokenPath);
    const auto* host = std::getenv(PodIdentityClientConstants::ApiServerHostEnvVar);
    const auto* port = std::getenv(PodIdentityClientConstants::ApiServerPortEnvVar);
    const auto* node_name = std::getenv(PodIdentityClientConstants::NodeNameEnvVar);

    if (!token.has_value() || host == nullptr || port == nullptr || node_name == nullptr)
    {
        Logger()->warn(
            "Unable to fetch pod identities: missing ServiceAccount token at {} or one of the "
            "{}/{}/{} environment variables",
            PodIdentityClientConstants::TokenPath, PodIdentityClientConstants::ApiServerHostEnvVar,
            PodIdentityClientConstants::ApiServerPortEnvVar, PodIdentityClientConstants::NodeNameEnvVar);
        return std::nullopt;
    }

    auto url = BuildApiServerUrl(host, port, node_name);
    if (!url.has_value())
    {
        Logger()->warn("Unable to build apiserver URL from host={} port={} node_name={}", host, port, node_name);
        return std::nullopt;
    }

    auto resp = http_client_.Get(*url, {fmt::format("Authorization: Bearer {}", *token)});
    if (resp.status != 200)
    {
        Logger()->warn("Unable to fetch pod identities from {}: status={} body={}", *url, resp.status, resp.raw_body);
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

        result.emplace(metadata["uid"].GetString(),
                        PodIdentity{metadata["name"].GetString(), metadata["namespace"].GetString()});
    }

    return result;
}

std::optional<std::string> PodIdentityClient::BuildApiServerUrl(const std::string& host, const std::string& port,
                                                                  const std::string& node_name) noexcept
{
    if (host.empty() || port.empty() || node_name.empty())
    {
        return std::nullopt;
    }

    // KUBERNETES_SERVICE_HOST can be a literal IPv6 address on real clusters (confirmed on a
    // NOP dev cluster: "fdf6:8ce:f8e4::1"); a bare colon-containing host breaks URL parsing, so
    // wrap it in brackets for the authority component.
    auto authority_host = host.find(':') != std::string::npos ? fmt::format("[{}]", host) : host;

    return fmt::format("https://{}:{}/api/v1/pods?fieldSelector=spec.nodeName={}", authority_host, port, node_name);
}

}  // namespace atlasagent
