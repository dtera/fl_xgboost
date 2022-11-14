//
// Created by HqZHao on 2022/11/14.
//
#include <gtest/gtest.h>

using namespace std;

int main(int argc, char **argv) {
  printf("Running main() from %s\n", __FILE__);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}