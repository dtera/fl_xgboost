//
// Created by HqZhao on 2022/11/21.
//

#ifndef FEDXGB_BASECOMM_H
#define FEDXGB_BASECOMM_H
#include <chrono>
#include <cstdint>
using namespace std;

enum MessageType { GradPair, BestSplit };

template <typename T>
struct Message {
  MessageType msg_type = BestSplit;  // The type of message to trigger the corresponding handlers
  uint32_t sender = 0;               // The sender's ID
  uint32_t receiver = 0;             // The receiver's ID
  uint32_t state = 0;                // The training round of the message
  T content;                         // The content of the message
  uint32_t timestamp = chrono::duration_cast<chrono::milliseconds>(
                           chrono::high_resolution_clock::now().time_since_epoch())
                           .count();
  uint32_t strategy = 0;  // redundant attribute
};

template <typename T>
class BaseComm {
  virtual void send(const Message<T>& msg) = 0;

  virtual void reveive(Message<T>* msg) = 0;

  virtual void close() = 0;
};

#endif  // FEDXGB_BASECOMM_H
