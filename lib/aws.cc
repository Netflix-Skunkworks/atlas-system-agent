#include "aws.h"

namespace atlasagent {

static constexpr const char* const kMetadataUrl =
    "http://169.254.169.254/latest/meta-data/iam/security-credentials/";

Aws::Aws(spectator::Registry* registry) noexcept
    : registry_{registry},
      http_client_{registry,
                   spectator::HttpClientConfig{std::chrono::seconds{1}, std::chrono::seconds{1},
                                               false, true, true}} {}

void Aws::update_stats() noexcept {
  if (creds_url_.empty()) {
    auto resp = http_client_.Get(kMetadataUrl);
    if (resp.status == 200) {
      creds_url_ = fmt::format("{}{}", kMetadataUrl, resp.raw_body);
      Logger()->info("Using {} as the credentials URL", creds_url_);
    } else {
      return;
    }
  }

  auto res = http_client_.Get(creds_url_);
  if (res.status != 200) {
    registry_->GetCounter("aws.credentialsRefreshErrors")->Increment();
    return;
  }

  update_stats_from(std::chrono::system_clock::now(), res.raw_body);
}

static std::chrono::system_clock::time_point getDateFrom(const rapidjson::Document& doc,
                                                         const char* dateStr) {
  time_t t;
  if (doc.HasMember(dateStr)) {
    auto p = doc[dateStr].GetString();
    tm datetime{};
    strptime(p, "%Y-%m-%dT%TZ", &datetime);
    t = timegm(&datetime);
  } else {
    t = 0;
  }
  return std::chrono::system_clock::from_time_t(t);
}

void Aws::update_stats_from(std::chrono::system_clock::time_point now,
                            const std::string& json) noexcept {
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

  auto lastUpdated = getDateFrom(creds, "LastUpdated");
  if (lastUpdated.time_since_epoch().count() > 0) {
    auto updatedAge = now - lastUpdated;
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(updatedAge).count();
    registry_->GetGauge("aws.credentialsAge")->Set(millis / 1e3);
  }

  auto expiration = getDateFrom(creds, "Expiration");

  if (expiration.time_since_epoch().count() > 0) {
    auto ttl = expiration - now;
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(ttl).count();
    registry_->GetGauge("aws.credentialsTtl")->Set(millis / 1e3);

    std::string bucket;
    // update some buckets
    if (ttl.count() < 0) {
      bucket = "expired";
    } else if (ttl <= std::chrono::minutes{5}) {
      bucket = "05min";
    } else if (ttl <= std::chrono::minutes{30}) {
      bucket = "30min";
    } else if (ttl <= std::chrono::minutes{60}) {
      bucket = "60min";
    } else {
      bucket = "hours";
    }

    registry_
        ->GetCounter(
            registry_->CreateId("aws.credentialsTtlBucket", spectator::Tags{{"bucket", bucket}}))
        ->Increment();
  }
}

}  // namespace atlasagent
