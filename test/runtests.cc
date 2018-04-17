#include <gtest/gtest.h>
#include "../lib/logger.h"

int main(int argc, char** argv) {
  atlasagent::UseConsoleLogger();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
