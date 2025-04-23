#include <lib/tagging/src/tagging_registry.h>
#include <lib/measurement_utils/src/measurement_utils.h>
#include <gtest/gtest.h>

namespace {

using Reg = atlasagent::base_tagging_registry<spectator::TestRegistry>;
using atlasagent::Tagger;

TEST(TaggingRegistry, Counter) {
  Tagger::Rule rule{atlasagent::TagRuleOp::Name, "foo", {{"id", "val1"}}};
  Tagger tagger{{rule}};
  spectator::TestRegistry registry;
  Reg reg{&registry, std::move(tagger)};

  reg.GetCounter("ctr")->Increment();
  reg.GetCounter("foo")->Increment();
  reg.GetCounter(spectator::Id::of("foo", {{"key", "val2"}}))->Increment();
  auto ms = my_measurements(&registry);
  auto map = measurements_to_map(ms, "key");
  expect_value(&map, "ctr|count", 1.0);
  expect_value(&map, "foo|count|val1", 1.0);       // adds id=val1
  expect_value(&map, "foo|count|val1|val2", 1.0);  // preserves prev tag
  EXPECT_TRUE(map.empty());
}

TEST(TaggingRegistry, Gauge) {
  Tagger::Rule rule{atlasagent::TagRuleOp::Name, "foo", {{"id", "val1"}}};
  Tagger tagger{{rule}};
  spectator::TestRegistry registry;
  Reg reg{&registry, std::move(tagger)};

  reg.GetGauge("g")->Set(1);
  reg.GetGauge("foo")->Set(2);
  reg.GetGauge(spectator::Id::of("foo", {{"key", "val2"}}))->Set(3);
  auto ms = my_measurements(&registry);
  auto map = measurements_to_map(ms, "key");
  expect_value(&map, "g|gauge", 1.0);
  expect_value(&map, "foo|gauge|val1", 2.0);       // adds id=val1
  expect_value(&map, "foo|gauge|val1|val2", 3.0);  // preserves prev tag
  EXPECT_TRUE(map.empty());
}

TEST(TaggingRegistry, MaxGauge) {
  Tagger::Rule rule{atlasagent::TagRuleOp::Name, "foo", {{"id", "val1"}}};
  Tagger tagger{{rule}};
  spectator::TestRegistry registry;
  Reg reg{&registry, std::move(tagger)};

  reg.GetMaxGauge("g")->Set(1);
  reg.GetMaxGauge("foo")->Set(2);
  reg.GetMaxGauge(spectator::Id::of("foo", {{"key", "val2"}}))->Set(3);
  auto ms = my_measurements(&registry);
  auto map = measurements_to_map(ms, "key");
  expect_value(&map, "g|max", 1.0);
  expect_value(&map, "foo|max|val1", 2.0);       // adds id=val1
  expect_value(&map, "foo|max|val1|val2", 3.0);  // preserves prev tag
  EXPECT_TRUE(map.empty());
}

template <typename T>
void expect_ids(T no_tags, T one, T two) {
  auto empty = no_tags->MeterId()->GetTags();
  EXPECT_EQ(empty.size(), 0);
  auto one_tag = one->MeterId()->GetTags();
  EXPECT_EQ(one_tag.size(), 1);
  EXPECT_EQ(one_tag.at("id"), "val1");

  auto two_tags = two->MeterId()->GetTags();
  EXPECT_EQ(two_tags.size(), 2);
  EXPECT_EQ(two_tags.at("id"), "val1");
  EXPECT_EQ(two_tags.at("key"), "val2");
}

auto get_registry(spectator::TestRegistry* registry) {
  Tagger::Rule rule{atlasagent::TagRuleOp::Name, "foo", {{"id", "val1"}}};
  Tagger tagger{{rule}};
  return Reg{registry, std::move(tagger)};
}

TEST(TaggingRegistry, DistSummary) {
  spectator::TestRegistry registry;
  Reg reg = get_registry(&registry);
  expect_ids(reg.GetDistributionSummary("ds"), reg.GetDistributionSummary("foo"),
             reg.GetDistributionSummary(spectator::Id::of("foo", {{"key", "val2"}})));
}

TEST(TaggingRegistry, Timer) {
  spectator::TestRegistry registry;
  Reg reg = get_registry(&registry);
  expect_ids(reg.GetTimer("foo2"), reg.GetTimer("foo"),
             reg.GetTimer(spectator::Id::of("foo", {{"key", "val2"}})));
}
}  // namespace