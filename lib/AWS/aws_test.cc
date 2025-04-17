#include "aws.h"
#include <lib/MeasurementUtils/src/measurement_utils.h>

#include <gtest/gtest.h>

namespace {
using Registry = spectator::TestRegistry;
using atlasagent::Aws;

inline std::string to_str(absl::Time t) {
  return absl::FormatTime("%Y-%m-%dT%H:%M:%E*SZ", t, absl::UTCTimeZone());
}

class AwsTest : public Aws<Registry> {
 public:
  explicit AwsTest(Registry* registry) : Aws{registry} {}

  void update_stats(absl::Time now, absl::Time lastUpdated, absl::Time expires) {
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
absl::Time currentTime() { return absl::FromUnixSeconds(absl::ToUnixSeconds(absl::Now())); }

TEST(Aws, UpdateStats) {
  Registry registry;

  auto now = currentTime();
  auto lastUpdated = now - absl::Minutes(10);
  auto expires = now + absl::Minutes(15);

  AwsTest aws{&registry};
  aws.update_stats(now, lastUpdated, expires);

  auto res = measurements_to_map(registry.Measurements(), "bucket");
  EXPECT_DOUBLE_EQ(res["aws.credentialsAge|gauge"], 600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtl|gauge"], 900.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtlBucket|count|30min"], 1.0);

  expires = now - absl::Seconds(1);
  aws.update_stats(now, lastUpdated, expires);
  res = measurements_to_map(registry.Measurements(), "bucket");
  EXPECT_DOUBLE_EQ(res["aws.credentialsAge|gauge"], 600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtl|gauge"], -1.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtlBucket|count|expired"], 1.0);

  expires = now + absl::Hours(12);
  aws.update_stats(now, lastUpdated, expires);
  res = measurements_to_map(registry.Measurements(), "bucket");
  EXPECT_DOUBLE_EQ(res["aws.credentialsAge|gauge"], 600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtl|gauge"], 12 * 3600.0);
  EXPECT_DOUBLE_EQ(res["aws.credentialsTtlBucket|count|hours"], 1.0);
}
}  // namespace