//
// Created by HqZhao on 2023/01/11.
//
#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

TEST(boost, str) {
  vector<string> vs;
  boost::split(vs, "1236_5", boost::is_any_of("_"));
  for (auto v : vs) {
    cout << v << endl;
  }
}
