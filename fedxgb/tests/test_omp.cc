//
// Created by HqZhao on 2022/11/14.
//
#include <gtest/gtest.h>

#include "common/threading_utils.h"

using namespace std;

void init(vector<int> &arr, int n = 50) {
  for (int i = 0; i < n; ++i) {
    arr.push_back(i);
  }
}

TEST(demo, omp) {
  vector<int> arr;
  init(arr);
  mutex g_mutex;

  xgboost::common::ParallelFor(arr.size(), 10, xgboost::common::Sched::Dyn(), [&](size_t i) {
    sleep(1);
    g_mutex.lock();
    cout << i << endl;
    g_mutex.unlock();
  });
}

TEST(demo, iter) {
  vector<int> arr;
  init(arr);

  for_each(arr.begin(), arr.end(), [&](int i) {
    sleep(1);
    cout << i << endl;
  });
}
