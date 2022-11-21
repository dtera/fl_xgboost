//
// Created by HqZhao on 2022/11/21.
//

#ifndef FEDXGB_BASECOMM_H
#define FEDXGB_BASECOMM_H

class BaseComm {
  virtual void send() = 0;

  virtual void reveive() = 0;

  virtual void close() = 0;
};

#endif  // FEDXGB_BASECOMM_H
