//
// Created by HqZhao on 2022/11/14.
//
#include <gtest/gtest.h>

#include "comm/grpc/GRPCComm.hpp"
#include "comm/grpc/XgbServiceAsyncClient.h"
#include "comm/grpc/XgbServiceAsyncServer.h"

using namespace std;

TEST(grpc, xgb_server) {
  XgbServiceAsyncServer server;
  while (server.IsRunning()) {
  }
  // sleep(1);
  server.Stop();
}

TEST(grpc, xgb_client) {
  XgbServiceAsyncClient client;
  for (int i = 1; i < 100; ++i) {
    client.AsyncReq(i);
  }
  client.Stop();
}

TEST(grpc, server) {
  GRPCServer<float> grpcServer;
  Message<float> message;
  message.msg_type = MessageType::BestSplit;
  message.content = 123;
  grpcServer.send(message);
  sleep(1);
  grpcServer.close();
}

TEST(grpc, client) {
  GRPCClient grpcClient;
  Message<float>* message = nullptr;
  grpcClient.AsyncSendMessage(MsgType::BestSplit);
  grpcClient.receive<float>(message, [&](float& t, const grpccomm::ListValue& listValue) {
    cout << "listValue: " << listValue.SerializeAsString() << endl;
    for (int i = 0; i < listValue.list_value_size(); ++i) {
      cout << listValue.list_value(i).scala_msg().float_value() << endl;
    }
  });
  sleep(1);
  grpcClient.close();
}
