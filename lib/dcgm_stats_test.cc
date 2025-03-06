#include "dcgm_stats.cc"

#include <gtest/gtest.h>

std::vector<std::string> read_file(const std::string& filePath) {
  std::ifstream file(filePath);
  if (file.is_open() == false) {
    std::cout << "Error opening file." << std::endl;
    return std::vector<std::string>();
  }

  // Read lines into a vector
  std::vector<std::string> lines{};
  std::string line{};
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  return lines;
}

TEST(DCGMTest, ParseLinesValidInput) {
  std::string filePath{"testdata/resources2/dcgm/ValidInput1"};
  auto lines = read_file(filePath);
  std::map<int, std::vector<double>> dataMap;

  EXPECT_EQ(parse_lines(lines, dataMap), true);
}

TEST(DCGMTest, ParseLinesInvalidInput1) {
  std::string filePath{"testdata/resources2/dcgm/InvalidInput1"};
  auto lines = read_file(filePath);
  std::map<int, std::vector<double>> dataMap;

  EXPECT_EQ(parse_lines(lines, dataMap), false);
}

TEST(DCGMTest, ParseLinesInvalidInput2) {
  std::string filePath{"testdata/resources2/dcgm/InvalidInput2"};
  auto lines = read_file(filePath);
  std::map<int, std::vector<double>> dataMap;

  EXPECT_EQ(parse_lines(lines, dataMap), false);
}

TEST(DCGMTest, ParseLinesEmptyLines) {
  std::vector<std::string> lines{};
  std::map<int, std::vector<double>> dataMap;
  EXPECT_EQ(parse_lines(lines, dataMap), false);
}