#include "../src/dcgm_stats.cc"
#include <lib/Util/src/util.h>


#include <gtest/gtest.h>

TEST(DCGMTest, ParseLinesValidInput) {
  std::string filePath{"testdata/resources2/dcgm/ValidInput1"};
  auto lines = atlasagent::read_file(filePath);
  std::map<int, std::vector<double>> dataMap;

  EXPECT_EQ(parse_lines(lines.value(), dataMap), true);
}

TEST(DCGMTest, ParseLinesInvalidInput1) {
  std::string filePath{"testdata/resources2/dcgm/InvalidInput1"};
  auto lines = atlasagent::read_file(filePath);
  std::map<int, std::vector<double>> dataMap;

  EXPECT_EQ(parse_lines(lines.value(), dataMap), false);
}

TEST(DCGMTest, ParseLinesInvalidInput2) {
  std::string filePath{"testdata/resources2/dcgm/InvalidInput2"};
  auto lines = atlasagent::read_file(filePath);
  std::map<int, std::vector<double>> dataMap;

  EXPECT_EQ(parse_lines(lines.value(), dataMap), false);
}

TEST(DCGMTest, ParseLinesEmptyLines) {
  std::vector<std::string> lines{};
  std::map<int, std::vector<double>> dataMap;
  EXPECT_EQ(parse_lines(lines, dataMap), false);
}