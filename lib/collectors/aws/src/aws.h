#pragma once

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/http_client/src/http_client.h>

#include <absl/time/time.h>

#include <string>
#include <string_view>

namespace atlasagent
{

// Publishes the age and remaining lifetime of the instance's IAM credentials, as reported by the
// EC2 instance metadata service.
class Aws
{
   public:
    explicit Aws(spectator::Registry* registry) noexcept;

    void collect() noexcept;

   protected:
    // for testing
    void update_stats_from(absl::Time now, std::string_view json) const noexcept;

   private:
    spectator::Registry* registry_;
    std::string executor_;
    std::string token_endpoint_;
    std::string iam_endpoint_;
    std::string creds_url_;
    HttpClient http_client_;
};

}  // namespace atlasagent
