#include "pod_identity_client.h"
#include <lib/util/src/util.h>
#include <lib/logger/src/logger.h>

#include <absl/strings/escaping.h>
#include <rapidjson/document.h>
#include <fmt/format.h>

#include <cctype>
#include <cstdlib>

namespace atlasagent
{

namespace
{

std::string_view TrimAsciiWhitespace(std::string_view s) noexcept
{
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

// Classic POSIX single-quote shell escaping: wrap the whole argument in single quotes, and for
// every literal single-quote character inside the argument, close the quote, emit a
// backslash-escaped single quote, then reopen the quote. e.g. o'brien -> 'o'\''brien'
std::string ShellQuoteSingleArg(const std::string& arg)
{
    std::string result;
    result.push_back('\'');
    for (char c : arg)
    {
        if (c == '\'')
        {
            result.push_back('\'');
            result.push_back('\\');
            result.push_back('\'');
            result.push_back('\'');
        }
        else
        {
            result.push_back(c);
        }
    }
    result.push_back('\'');
    return result;
}

}  // namespace

PodIdentityClient::PodIdentityClient(Registry* registry, std::string kubeconfig_path) noexcept
    : registry_(registry),
      http_client_{registry, HttpClientConfig{absl::Seconds(2), absl::Seconds(3), PodIdentityClientConstants::CaCertWritePath}},
      initialized_(false)
{
    auto lines = read_file(kubeconfig_path);
    if (!lines.has_value())
    {
        Logger()->warn("Unable to read kubeconfig at {}; pod identity resolution disabled", kubeconfig_path);
        return;
    }
    auto info = ExtractKubeconfigFields(*lines);
    if (!info.has_value())
    {
        Logger()->warn("Unable to extract required fields from kubeconfig at {}; pod identity resolution disabled",
                        kubeconfig_path);
        return;
    }
    if (!write_string_to_file(PodIdentityClientConstants::CaCertWritePath, info->ca_cert_pem))
    {
        Logger()->warn("Unable to write CA bundle to {}; pod identity resolution disabled",
                        PodIdentityClientConstants::CaCertWritePath);
        return;
    }
    apiserver_server_ = std::move(info->server);
    exec_command_line_ = BuildExecCommandLine(info->exec_command, info->exec_args);
    initialized_ = true;
}

std::optional<PodIdentityMap> PodIdentityClient::FetchPodIdentities() const noexcept
{
    if (!initialized_)
    {
        return std::nullopt;
    }

    const auto* node_name = std::getenv(PodIdentityClientConstants::InstanceIdEnvVar);
    if (node_name == nullptr)
    {
        Logger()->warn("Unable to fetch pod identities: {} is not set", PodIdentityClientConstants::InstanceIdEnvVar);
        return std::nullopt;
    }

    auto url = BuildApiServerUrl(apiserver_server_, node_name);
    if (!url.has_value())
    {
        Logger()->warn("Unable to build apiserver URL from server={} node_name={}", apiserver_server_, node_name);
        return std::nullopt;
    }

    auto exec_output = read_output_string(exec_command_line_.c_str(), PodIdentityClientConstants::ExecCredentialTimeoutMillis);
    auto token = ParseExecCredentialToken(exec_output);
    if (!token.has_value())
    {
        Logger()->warn("Unable to obtain a bearer token via exec-credential plugin: {}", exec_command_line_);
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

std::optional<KubeconfigInfo> PodIdentityClient::ExtractKubeconfigFields(const std::vector<std::string>& lines) noexcept
{
    std::string server;
    std::string ca_cert_base64;
    std::string exec_command;
    std::vector<std::string> exec_args;
    bool collecting_args = false;

    static constexpr std::string_view kServerPrefix = "server:";
    static constexpr std::string_view kCaDataPrefix = "certificate-authority-data:";
    static constexpr std::string_view kArgsLine = "args:";
    static constexpr std::string_view kCommandPrefix = "command:";

    for (const auto& raw_line : lines)
    {
        auto trimmed = TrimAsciiWhitespace(raw_line);

        if (collecting_args)
        {
            if (!trimmed.empty() && trimmed.front() == '-')
            {
                auto item = TrimAsciiWhitespace(trimmed.substr(1));
                exec_args.emplace_back(item);
                continue;
            }
            collecting_args = false;
            // fall through: this line is the one after the YAML list ended, evaluate normally
        }

        if (trimmed.size() >= kServerPrefix.size() && trimmed.substr(0, kServerPrefix.size()) == kServerPrefix)
        {
            server = std::string(TrimAsciiWhitespace(trimmed.substr(kServerPrefix.size())));
        }
        else if (trimmed.size() >= kCaDataPrefix.size() && trimmed.substr(0, kCaDataPrefix.size()) == kCaDataPrefix)
        {
            ca_cert_base64 = std::string(TrimAsciiWhitespace(trimmed.substr(kCaDataPrefix.size())));
        }
        else if (trimmed == kArgsLine)
        {
            collecting_args = true;
        }
        else if (trimmed.size() >= kCommandPrefix.size() && trimmed.substr(0, kCommandPrefix.size()) == kCommandPrefix)
        {
            exec_command = std::string(TrimAsciiWhitespace(trimmed.substr(kCommandPrefix.size())));
        }
    }

    if (server.empty() || ca_cert_base64.empty() || exec_command.empty() || exec_args.empty())
    {
        Logger()->warn(
            "kubeconfig is missing one or more required fields (server/certificate-authority-data/command/args)");
        return std::nullopt;
    }

    std::string ca_cert_pem;
    if (!absl::Base64Unescape(ca_cert_base64, &ca_cert_pem))
    {
        Logger()->warn("Unable to base64-decode certificate-authority-data from kubeconfig");
        return std::nullopt;
    }

    return KubeconfigInfo{server, ca_cert_pem, exec_command, exec_args};
}

std::optional<std::string> PodIdentityClient::ParseExecCredentialToken(const std::string& json) noexcept
{
    rapidjson::Document doc;
    doc.Parse(json.c_str(), json.length());
    if (doc.HasParseError())
    {
        Logger()->warn("Unable to parse exec-credential output as JSON: {}", json);
        return std::nullopt;
    }

    if (!doc.IsObject() || !doc.HasMember("status") || !doc["status"].IsObject())
    {
        Logger()->warn("exec-credential output has no 'status' object: {}", json);
        return std::nullopt;
    }

    const auto& status = doc["status"];
    if (!status.HasMember("token") || !status["token"].IsString())
    {
        Logger()->warn("exec-credential output's 'status' has no string 'token': {}", json);
        return std::nullopt;
    }

    return std::string(status["token"].GetString());
}

std::string PodIdentityClient::BuildExecCommandLine(const std::string& command, const std::vector<std::string>& args) noexcept
{
    std::string result = ShellQuoteSingleArg(command);
    for (const auto& arg : args)
    {
        result += " ";
        result += ShellQuoteSingleArg(arg);
    }
    return result;
}

std::optional<std::string> PodIdentityClient::BuildApiServerUrl(const std::string& server,
                                                                  const std::string& node_name) noexcept
{
    if (server.empty() || node_name.empty())
    {
        return std::nullopt;
    }

    auto base = server;
    if (!base.empty() && base.back() == '/')
    {
        base.pop_back();
    }

    return fmt::format("{}/api/v1/pods?fieldSelector=spec.nodeName={}", base, node_name);
}

}  // namespace atlasagent
