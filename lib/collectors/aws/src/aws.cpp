#include "aws.h"

#include <lib/logger/src/logger.h>

#include <absl/time/clock.h>
#include <fmt/format.h>
#include <rapidjson/document.h>

#include <cstdlib>
#include <ctime>
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
constexpr absl::Duration kMetadataTimeout = absl::Seconds(1);
constexpr int kHttpOk = 200;

std::string env_or(const char* name, std::string_view fallback)
{
    const char* value = std::getenv(name);
    return value != nullptr ? std::string{value} : std::string{fallback};
}

std::string metadata_url(std::string_view path)
{
    return fmt::format("{}/{}", env_or("AWS_EC2_METADATA_SERVICE_ENDPOINT", kDefaultMetadataEndpoint), path);
}

void count_refresh_error(const spectator::Registry& registry)
{
    registry.CreateCounter("aws.credentialsRefreshErrors").Increment();
}

absl::Time getDateFrom(const rapidjson::Document& doc, const char* dateStr)
{
    time_t t;
    if (doc.HasMember(dateStr))
    {
        auto p = doc[dateStr].GetString();
        tm datetime{};
        strptime(p, "%Y-%m-%dT%TZ", &datetime);
        t = timegm(&datetime);
    }
    else
    {
        t = 0;
    }
    return absl::FromTimeT(t);
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
        if (profile_resp.status != kHttpOk)
        {
            return;
        }

        // save the resulting URL now that we know the instance profile
        creds_url_ = fmt::format("{}{}", iam_endpoint_, profile_resp.raw_body);
        logger->info("Using {} as the credentials URL", creds_url_);
    }

    const auto creds_resp = http_client_.Get(creds_url_, token_header);
    if (creds_resp.status != kHttpOk)
    {
        count_refresh_error(*registry_);
        return;
    }

    update_stats_from(absl::Now(), creds_resp.raw_body);
}

void Aws::update_stats_from(absl::Time now, std::string_view json) const noexcept
{
    rapidjson::Document creds;
    creds.Parse(json.data(), json.size());
    if (creds.HasParseError())
    {
        Logger()->warn("Unable to parse {} as JSON", json);
        count_refresh_error(*registry_);
        return;
    }

    if (!creds.IsObject())
    {
        Logger()->warn("Got {} which is not a JSON object", json);
        count_refresh_error(*registry_);
        return;
    }

    const auto last_updated = getDateFrom(creds, "LastUpdated");
    if (last_updated > absl::UnixEpoch())
    {
        registry_->CreateGauge("aws.credentialsAge").Set(absl::ToDoubleSeconds(now - last_updated));
    }

    const auto expiration = getDateFrom(creds, "Expiration");
    if (expiration > absl::UnixEpoch())
    {
        const auto ttl = expiration - now;
        registry_->CreateGauge("aws.credentialsTtl").Set(absl::ToDoubleSeconds(ttl));
        registry_->CreateCounter("aws.credentialsTtlBucket", {{"bucket", ttl_bucket(ttl)}}).Increment();
    }
}

}  // namespace atlasagent
