#include <lib/Tagging/src/tagger.h>
#include <gtest/gtest.h>

namespace {

using atlasagent::Tagger;
using atlasagent::TagRuleOp;

TEST(Tagger, FromCfgFile) {
  auto tagger = Tagger::FromConfigFile("testdata/resources/atlas-agent.json");
  std::vector<Tagger::Rule> expected;
  spectator::Tags tags{{"tag-key1", "tag-val1"}, {"tag-key2", "tag-val2"}};
  expected.emplace_back(TagRuleOp::Prefix, "disk.io.", tags);
  expected.emplace_back(TagRuleOp::Name, "net.iface.bytes", tags);

  Tagger expected_tagger{expected};
  ASSERT_EQ(expected_tagger, tagger);
  atlasagent::Logger()->debug("Got {} from config file", *tagger);
}

TEST(Tagger, AddTags) {
  auto tagger = Tagger::FromConfigFile("testdata/resources/atlas-agent.json");
  auto id_prefix = std::make_unique<spectator::Id>("disk.io.foo", spectator::Tags{{"dev", "root"}});
  auto id_name =
      std::make_unique<spectator::Id>("net.iface.bytes", spectator::Tags{{"dev", "eth0"}});
  // match is for prefix = disk.io.
  auto id_no_match = std::make_unique<spectator::Id>("disk.io", spectator::Tags({{"dev", "sda"}}));

  auto id_prefix_tagged = tagger->GetId(std::move(id_prefix));
  auto id_name_tagged = tagger->GetId(std::move(id_name));
  auto id_no_match_tagged = tagger->GetId(std::move(id_no_match));

  auto expected_disk_tags =
      spectator::Tags{{"dev", "root"}, {"tag-key1", "tag-val1"}, {"tag-key2", "tag-val2"}};
  auto expected_net_tags =
      spectator::Tags{{"dev", "eth0"}, {"tag-key1", "tag-val1"}, {"tag-key2", "tag-val2"}};
  auto expected_unchanged = spectator::Tags{{"dev", "sda"}};

  EXPECT_EQ(id_prefix_tagged->GetTags(), expected_disk_tags);
  EXPECT_EQ(id_name_tagged->GetTags(), expected_net_tags);
  EXPECT_EQ(id_no_match_tagged->GetTags(), expected_unchanged);
}
}  // namespace