#include "aws.h"

#include <lib/logger/src/logger.h>

#include <absl/strings/ascii.h>
#include <absl/strings/match.h>
#include <absl/time/clock.h>
#include <fmt/format.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atlasagent
{

namespace
{

constexpr std::string_view kDefaultExecutor = "unknown";
constexpr std::string_view kDefaultMetadataEndpoint = "http://169.254.169.254";
constexpr std::string_view kCredsPath = "latest/meta-data/iam/security-credentials/";
constexpr std::string_view kTokenPath = "latest/api/token";
constexpr std::string_view kZonelessTimestampFormat = "%Y-%m-%d%ET%H:%M:%E*S";
constexpr absl::Duration kMetadataTimeout = absl::Seconds(1);
constexpr int kHttpOk = 200;

std::string env_or(const char* name, std::string_view fallback)
{
    const char* value = std::getenv(name);
    return value != nullptr ? std::string{value} : std::string{fallback};
}

std::string metadata_url(std::string_view path)
{
    // a configured endpoint may end in a slash, which would otherwise give "//latest/..."
    std::string endpoint = env_or("AWS_EC2_METADATA_SERVICE_ENDPOINT", kDefaultMetadataEndpoint);
    while (endpoint.ends_with('/'))
    {
        endpoint.pop_back();
    }
    return fmt::format("{}/{}", endpoint, path);
}

void count_refresh_error(const spectator::Registry& registry)
{
    registry.CreateCounter("aws.credentialsRefreshErrors").Increment();
}

// Returns the named member of the object if it is a string. The view is only valid while the object is.
std::optional<std::string_view> string_member(const rapidjson::Value& object, const char* name)
{
    const auto member = object.FindMember(name);
    if (member == object.MemberEnd() || !member->value.IsString())
    {
        return std::nullopt;
    }
    return std::string_view{member->value.GetString(), member->value.GetStringLength()};
}

// Returns the timestamp stored in the named member of the credentials object, or nullopt if the
// member is missing, is not a string, does not parse, or is not a finite time after the Unix epoch.
std::optional<absl::Time> timestamp_member(const rapidjson::Value& creds, const char* name)
{
    const auto text = string_member(creds, name);
    if (!text)
    {
        return std::nullopt;
    }

    // RFC 3339 is expected; a timestamp that carries no zone is read as UTC
    absl::Time timestamp;
    std::string error;
    if (!absl::ParseTime(absl::RFC3339_full, *text, &timestamp, &error) &&
        !absl::ParseTime(kZonelessTimestampFormat, *text, &timestamp, &error))
    {
        Logger()->warn("Unable to parse {} value {} as a timestamp: {}", name, *text, error);
        return std::nullopt;
    }

    // an unset (zero or epoch) or infinite timestamp says nothing about the credentials
    if (timestamp <= absl::UnixEpoch() || timestamp == absl::InfiniteFuture())
    {
        return std::nullopt;
    }
    return timestamp;
}

std::string ttl_bucket(absl::Duration ttl)
{
    if (ttl < absl::ZeroDuration())
    {
        return "expired";
    }
    if (ttl <= absl::Minutes(5))
    {
        return "05min";
    }
    if (ttl <= absl::Minutes(30))
    {
        return "30min";
    }
    if (ttl <= absl::Minutes(60))
    {
        return "60min";
    }
    return "hours";
}

}  // namespace

Aws::Aws(spectator::Registry* registry) noexcept
    : registry_{registry},
      executor_{env_or("NETFLIX_EXECUTOR", kDefaultExecutor)},
      token_endpoint_{metadata_url(kTokenPath)},
      iam_endpoint_{metadata_url(kCredsPath)},
      http_client_{registry, HttpClientConfig{.connect_timeout = kMetadataTimeout, .read_timeout = kMetadataTimeout}}
{
}

void Aws::collect() noexcept
{
    const auto logger = Logger();

    if (executor_ != "ec2" && executor_ != "titus")
    {
        logger->debug("Skip AWS stats, because NETFLIX_EXECUTOR is not ec2 or titus");
        return;
    }

    // get a token
    const std::vector<std::string> token_ttl_header{"X-aws-ec2-metadata-token-ttl-seconds: 60"};
    const auto token_resp = http_client_.Put(token_endpoint_, token_ttl_header);
    if (token_resp.status != kHttpOk)
    {
        logger->error("Unable to get a security token from AWS: {}", token_resp.raw_body);
        return;
    }
    const std::vector<std::string> token_header{fmt::format("X-aws-ec2-metadata-token: {}", token_resp.raw_body)};

    if (creds_url_.empty())
    {
        // get the instance profile
        const auto profile_resp = http_client_.Get(iam_endpoint_, token_header);
        const std::string profile{absl::StripAsciiWhitespace(profile_resp.raw_body)};
        if (profile_resp.status != kHttpOk || profile.empty())
        {
            // an instance without a profile is not an error, but losing one we already found is
            if (profile_resolved_)
            {
                count_refresh_error(*registry_);
            }
            return;
        }
        profile_resolved_ = true;

        // save the resulting URL now that we know the instance profile
        creds_url_ = fmt::format("{}{}", iam_endpoint_, profile);
        logger->info("Using {} as the credentials URL", creds_url_);
    }

    const auto creds_resp = http_client_.Get(creds_url_, token_header);
    if (creds_resp.status != kHttpOk)
    {
        // the instance profile may have changed, so look it up again on the next pass
        creds_url_.clear();
        count_refresh_error(*registry_);
        return;
    }

    update_stats_from(absl::Now(), creds_resp.raw_body);
}

void Aws::update_stats_from(absl::Time now, std::string_view json) const noexcept
{
    // The response carries the secret key and session token, so it is never written to the log.
    rapidjson::Document creds;
    creds.Parse(json.data(), json.size());
    if (creds.HasParseError())
    {
        Logger()->warn("Unable to parse the credentials response as JSON: {} at offset {} of {}",
                       rapidjson::GetParseError_En(creds.GetParseError()), creds.GetErrorOffset(), json.size());
        count_refresh_error(*registry_);
        return;
    }

    if (!creds.IsObject())
    {
        Logger()->warn("The credentials response is not a JSON object");
        count_refresh_error(*registry_);
        return;
    }

    // the metadata service can answer 200 with an error document, e.g. when EC2 cannot assume the role
    const auto code = string_member(creds, "Code");
    if (code && !absl::EqualsIgnoreCase(*code, "Success"))
    {
        const auto message = string_member(creds, "Message");
        Logger()->warn("The credentials response reports {}: {}", *code, message.value_or(""));
        count_refresh_error(*registry_);
        return;
    }

    if (const auto last_updated = timestamp_member(creds, "LastUpdated"))
    {
        registry_->CreateGauge("aws.credentialsAge").Set(absl::ToDoubleSeconds(now - *last_updated));
    }

    if (const auto expiration = timestamp_member(creds, "Expiration"))
    {
        const auto ttl = *expiration - now;
        registry_->CreateGauge("aws.credentialsTtl").Set(absl::ToDoubleSeconds(ttl));
        registry_->CreateCounter("aws.credentialsTtlBucket", {{"bucket", ttl_bucket(ttl)}}).Increment();
    }
}

}  // namespace atlasagent
