//
// Created by HqZhao on 2022/11/23.
//

#pragma once

#include "xgboost/logging.h"

#define DEBUG std::cout //LOG(DEBUG)
#define INFO LOG(INFO)

enum class XgbCommType {
  GRAD_CONNECT = 1,
  GRAD_READ = 2,
  GRAD_WRITE = 3,
  SPLITS_CONNECT = 4,
  SPLITS_READ = 5,
  SPLITS_WRITE = 6,
  DONE = 7,
  FINISH = 8
};
