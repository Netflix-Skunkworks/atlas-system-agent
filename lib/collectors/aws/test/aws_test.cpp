
#include <lib/collectors/aws/src/aws.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <thirdparty/spectator-cpp/libs/writer/writer_wrapper/writer_test_helper.h>

#include <gtest/gtest.h>

struct AwsTestingConstants {
  static constexpr auto expectedMessage1 = "g:aws.credentialsAge:600.000000\n";
  static constexpr auto expectedMessage2 = "g:aws.credentialsTtl:900.000000\n";
  static constexpr auto expectedMessage3 = "c:aws.credentialsTtlBucket,bucket=30min:1.000000\n";

  static constexpr auto expectedMessage4 = "g:aws.credentialsAge:600.000000\n";
  static constexpr auto expectedMessage5 = "g:aws.credentialsTtl:-1.000000\n";
  static constexpr auto expectedMessage6 = "c:aws.credentialsTtlBucket,bucket=expired:1.000000\n";

  static constexpr auto expectedMessage7 = "g:aws.credentialsAge:600.000000\n";
  static constexpr auto expectedMessage8 = "g:aws.credentialsTtl:43200.000000\n";
  static constexpr auto expectedMessage9 = "c:aws.credentialsTtlBucket,bucket=hours:1.000000\n";
};

inline std::string to_str(absl::Time t) {
  return absl::FormatTime("%Y-%m-%dT%H:%M:%E*SZ", t, absl::UTCTimeZone());
}

class AwsTest : public atlasagent::Aws {
 public:
  explicit AwsTest(Registry* registry) : atlasagent::Aws{registry} {}

  void update_stats_for_test(absl::Time now, absl::Time lastUpdated, absl::Time expires) {
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

    update_stats_from(now, json);
  }
};

// now() truncated to seconds
absl::Time currentTime() { return absl::FromUnixSeconds(absl::ToUnixSeconds(absl::Now())); }

TEST(Aws, UpdateStats) {
  auto config = Config(WriterConfig(WriterTypes::Memory));
  auto r = Registry(config);

  auto now = currentTime();
  auto lastUpdated = now - absl::Minutes(10);
  auto expires = now + absl::Minutes(15);

  AwsTest aws(&r);
  aws.update_stats_for_test(now, lastUpdated, expires);

  auto memoryWriter = static_cast<MemoryWriter*>(WriterTestHelper::GetImpl());
  auto messages = memoryWriter->GetMessages();

  EXPECT_EQ(messages.size(), 3);
  EXPECT_EQ(messages.at(0), AwsTestingConstants::expectedMessage1);
  EXPECT_EQ(messages.at(1), AwsTestingConstants::expectedMessage2);
  EXPECT_EQ(messages.at(2), AwsTestingConstants::expectedMessage3);

  memoryWriter->Clear();
  expires = now - absl::Seconds(1);
  aws.update_stats_for_test(now, lastUpdated, expires);

  messages = memoryWriter->GetMessages();
  EXPECT_EQ(messages.size(), 3);
  EXPECT_EQ(messages.at(0), AwsTestingConstants::expectedMessage4);
  EXPECT_EQ(messages.at(1), AwsTestingConstants::expectedMessage5);
  EXPECT_EQ(messages.at(2), AwsTestingConstants::expectedMessage6);

  memoryWriter->Clear();
  expires = now + absl::Hours(12);
  aws.update_stats_for_test(now, lastUpdated, expires);

  messages = memoryWriter->GetMessages();
  EXPECT_EQ(messages.size(), 3);
  EXPECT_EQ(messages.at(0), AwsTestingConstants::expectedMessage7);
  EXPECT_EQ(messages.at(1), AwsTestingConstants::expectedMessage8);
  EXPECT_EQ(messages.at(2), AwsTestingConstants::expectedMessage9);
}