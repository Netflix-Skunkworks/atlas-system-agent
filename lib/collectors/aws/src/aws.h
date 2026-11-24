#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/http_client/src/http_client.h>
#include <lib/logger/src/logger.h>
#include <rapidjson/document.h>

#include "absl/time/clock.h"

namespace atlasagent
{

namespace detail
{

static constexpr const char* const kDefaultExecutor = "unknown";
static constexpr const char* const kDefaultMetadataEndpoint = "http://169.254.169.254";
static constexpr const char* const kCredsPath = "latest/meta-data/iam/security-credentials/";
static constexpr const char* const kTokenPath = "latest/api/token";

absl::Time getDateFrom(const rapidjson::Document& doc, const char* dateStr);

std::string get_netflix_executor();

std::string get_metadata_endpoint();

std::string get_token_endpoint();

std::string get_iam_endpoint();

}  // namespace detail

class Aws
{
   public:
    explicit Aws(spectator::Registry* registry) noexcept;

    void collect() noexcept;

   private:
    spectator::Registry* registry_;
    std::string executor_;
    std::string token_endpoint_;
    std::string iam_endpoint_;
    std::string creds_url_;
    HttpClient http_client_;

   protected:
    void update_stats_from(absl::Time now, const std::string& json) noexcept;
};

}  // namespace atlasagent
