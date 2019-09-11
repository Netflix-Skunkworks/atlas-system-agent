#include "../lib/aws.h"
#include "../lib/logger.h"
#include "measurement_utils.h"
#include <fmt/ostream.h>

#include <gtest/gtest.h>

using namespace atlasagent;
using spectator::GetConfiguration;
using spectator::Registry;
using std::chrono::seconds;
using std::chrono::system_clock;

static std::string to_str(system_clock::time_point t) {
  auto tt = system_clock::to_time_t(t);

  auto ptm = gmtime(&tt);
  // "2019-09-10T22:22:58Z"
  return fmt::format("{}-{:02}-{:02}T{:02}:{:02}:{:02}Z", ptm->tm_year + 1900, ptm->tm_mon + 1,
                     ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
}

class AwsTest : public Aws {
 public:
  explicit AwsTest(Registry* registry) : Aws{registry} {}

  void update_stats(system_clock::time_point now, system_clock::time_point lastUpdated,
                    system_clock::time_point expires) {
    auto lastUpdatedStr = to_str(lastUpdated);
    auto expiresStr = to_str(expires);
    auto json = fmt::format(R"json(
{{
  "Code" : "Success",
  "LastUpdated" : "{}",
  "Type" : "AWS-HMAC",
  "AccessKeyId" : "KeyId",
  "SecretAccessKey" : "SomeSecret",
  "Token" : "SomeToken",
  "Expiration" : "{}"
}}
  )json",
                            lastUpdatedStr, expiresStr);

    Aws::update_stats_from(now, json);
  }
};

// now() truncated to seconds
static system_clock::time_point currentTime() {
  auto seconds_since_epoch =
      std::chrono::duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  return std::chrono::system_clock::from_time_t(seconds_since_epoch);
}

TEST(Aws, UpdateStats) {
  Registry registry(GetConfiguration(), Logger());

  auto now = currentTime();
  auto lastUpdated = now - seconds{600};
  auto expires = now + seconds{900};

  AwsTest aws{&registry};
  aws.update_stats(now, lastUpdated, expires);

  auto res = measurements_to_map(registry.Measurements(), "bucket");
  EXPECT_DOUBLE_EQ(res["aws.credentialsAge|gauge"], 600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtl|gauge"], 900.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtlBucket|count|30min"], 1.0);

  expires = now - seconds{1};
  aws.update_stats(now, lastUpdated, expires);
  res = measurements_to_map(registry.Measurements(), "bucket");
  EXPECT_DOUBLE_EQ(res["aws.credentialsAge|gauge"], 600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtl|gauge"], -1.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtlBucket|count|expired"], 1.0);

  expires = now + std::chrono::hours{12};
  aws.update_stats(now, lastUpdated, expires);
  res = measurements_to_map(registry.Measurements(), "bucket");
  EXPECT_DOUBLE_EQ(res["aws.credentialsAge|gauge"], 600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtl|gauge"], 12 * 3600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtlBucket|count|hours"], 1.0);
}
