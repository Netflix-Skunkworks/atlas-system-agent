#include "service_monitor.h"
#include "util.h"

#include <gtest/gtest.h>

// TEST(DCGMTest, ParseLinesValidInput) {
//   // std::string filePath{"testdata/resources2/dcgm/ValidInput1"};
//   // auto lines = atlasagent::read_file(filePath);
//   // std::map<int, std::vector<double>> dataMap;

//   // EXPECT_EQ(parse_lines(lines.value(), dataMap), true);
// }


void PrintConfig(std::vector<std::regex> config){
    for (auto x : config){
        std::cout << "Pattern2: " << x.pattern() << std::endl;
    }
}






TEST(ServiceMonitorTest, ParseValidConfig){
    auto filepath{"testdata/resources2/service_monitor/valid_regext.txt"};
    auto config = parse_service_monitor_config(filepath);
    PrintConfig(config.value());
    EXPECT_NE(std::nullopt, config);
    EXPECT_EQ(20, config.value().size());
}

TEST(ServiceTest, TestA){
  list_all_units();
}

TEST(ServiceTest, TestB){
  GetServiceProperties("nvidia-dcgm.service");
}
