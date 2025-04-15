#include "service_monitor_utils.cc"
#include "../Util/util.h"

#include <gtest/gtest.h>

TEST(ServiceMonitorTest, ParseValidConfig) {
  auto filepath{"testdata/resources2/service_monitor/regex_directory/valid_regex.systemd-unit"};
  auto config = parse_regex_config_file(filepath);
  EXPECT_NE(std::nullopt, config);
  EXPECT_EQ(12, config.value().size());
}

TEST(ServiceMonitorTest, ParseRegexDirectory) {
  auto directory{"testdata/resources2/service_monitor/regex_directory"};
  auto config = parse_service_monitor_config_directory(directory);
  EXPECT_NE(std::nullopt, config);
  EXPECT_EQ(20, config.value().size());
}

TEST(ServiceMonitorTest, ParseNumberOfCores) {
  auto filepath{"testdata/resources2/service_monitor/valid-cpu-info.txt"};
  auto fileContents = atlasagent::read_file(filepath);
  EXPECT_NE(std::nullopt, fileContents);
  auto coreCount = parse_cores(fileContents.value()[0]);
  EXPECT_EQ(96, coreCount);
}

TEST(ServiceMonitorTest, ParseProcStat) {
  auto filepath{"testdata/resources2/service_monitor/valid-proc-stat-info.txt"};
  auto fileContents = atlasagent::read_file(filepath);
  EXPECT_NE(std::nullopt, fileContents);
  auto cpuTime = parse_cpu_time(fileContents.value());
  EXPECT_EQ(46255609, cpuTime);
}

TEST(ServiceMonitorTest, ParseProcPidStat) {
  auto filepath{"testdata/resources2/service_monitor/valid-proc-pid-stat-info.txt"};
  auto fileContents = atlasagent::read_file(filepath);
  EXPECT_NE(std::nullopt, fileContents);
  auto processTimes = parse_process_times(fileContents.value());
  auto rss = parse_rss(fileContents.value());
  EXPECT_EQ(5, processTimes.value().uTime);
  EXPECT_EQ(6, processTimes.value().sTime);
  EXPECT_EQ(2933, rss);
}
