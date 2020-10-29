#pragma once

#include <rapidjson/document.h>
#include "http_client.h"
#include "logger.h"
#include "spectator/registry.h"

namespace atlasagent {

namespace detail {
static constexpr const char* const kMetadataUrl =
    "http://169.254.169.254/latest/meta-data/iam/security-credentials/";

static constexpr const char* const kMetadataToken = "http://169.254.169.254/latest/api/token";

inline absl::Time getDateFrom(const rapidjson::Document& doc, const char* dateStr) {
  time_t t;
  if (doc.HasMember(dateStr)) {
    auto p = doc[dateStr].GetString();
    tm datetime{};
    strptime(p, "%Y-%m-%dT%TZ", &datetime);
    t = timegm(&datetime);
  } else {
    t = 0;
  }
  return absl::FromTimeT(t);
}

}  // namespace detail

template <typename Reg = spectator::Registry>
class Aws {
 public:
  explicit Aws(Reg* registry) noexcept
      : registry_{registry},
        http_client_{registry, HttpClientConfig{absl::Seconds(1), absl::Seconds(1)}} {}

  void update_stats() noexcept {
    auto logger = Logger();
    // get a token
    std::vector<std::string> tokenTtl{"X-aws-ec2-metadata-token-ttl-seconds: 60"};
    auto resp = http_client_.Put(detail::kMetadataToken, tokenTtl);
    if (resp.status != 200) {
      logger->error("Unable to get a security token from AWS: {}", resp.raw_body);
      return;
    }
    const auto& token = resp.raw_body;
    std::vector<std::string> tokenHeader{fmt::format("X-aws-ec2-metadata-token: {}", token)};

    if (creds_url_.empty()) {
      // get the instance profile
      resp = http_client_.Get(detail::kMetadataUrl, tokenHeader);
      if (resp.status == 200) {
        // save the resulting URL now that we know the instance profile
        creds_url_ = fmt::format("{}{}", detail::kMetadataUrl, resp.raw_body);
        logger->info("Using {} as the credentials URL", creds_url_);
      } else {
        return;
      }
    }

    resp = http_client_.Get(creds_url_, tokenHeader);
    if (resp.status != 200) {
      registry_->GetCounter("aws.credentialsRefreshErrors")->Increment();
      return;
    }

    update_stats_from(absl::Now(), resp.raw_body);
  }

 private:
  Reg* registry_;
  std::string creds_url_;
  HttpClient<Reg> http_client_;

 protected:
  void update_stats_from(absl::Time now, const std::string& json) noexcept {
    rapidjson::Document creds;
    creds.Parse(json.c_str(), json.length());
    if (creds.HasParseError()) {
      Logger()->warn("Unable to parse {} as JSON", json);
      registry_->GetCounter("aws.credentialsRefreshErrors")->Increment();
      return;
    }

    if (!creds.IsObject()) {
      Logger()->warn("Got {} which is not a JSON object", json);
      registry_->GetCounter("aws.credentialsRefreshErrors")->Increment();
      return;
    }

    auto lastUpdated = detail::getDateFrom(creds, "LastUpdated");
    if (lastUpdated > absl::UnixEpoch()) {
      auto updatedAge = now - lastUpdated;
      registry_->GetGauge("aws.credentialsAge")->Set(absl::ToDoubleSeconds(updatedAge));
    }

    auto expiration = detail::getDateFrom(creds, "Expiration");

    if (expiration > absl::UnixEpoch()) {
      auto ttl = expiration - now;
      registry_->GetGauge("aws.credentialsTtl")->Set(absl::ToDoubleSeconds(ttl));

      std::string bucket;
      // update some buckets
      if (ttl < absl::ZeroDuration()) {
        bucket = "expired";
      } else if (ttl <= absl::Minutes(5)) {
        bucket = "05min";
      } else if (ttl <= absl::Minutes(30)) {
        bucket = "30min";
      } else if (ttl <= absl::Minutes(60)) {
        bucket = "60min";
      } else {
        bucket = "hours";
      }

      registry_->GetCounter("aws.credentialsTtlBucket", spectator::Tags{{"bucket", bucket}})
          ->Increment();
    }
  }
};

}  // namespace atlasagent
