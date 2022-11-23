//
// Created by HqZhao on 2022/11/14.
//
#include <gtest/gtest.h>

#include "comm/grpc/XgbServiceServer.h"
#include "comm/grpc/XgbServiceClient.h"
#include "comm/grpc/GRPCComm.hpp"

using namespace std;

TEST(grpc, xgb_server) {
  XgbServiceServer server;
  server.Start();
  while (server.IsRunning()) {}
  //sleep(1);
  server.Stop();
}

TEST(grpc, xgb_client) {
  XgbServiceClient client;
  client.Start(XgbCommType::GRAD_CONNECT);
  sleep(2);
  client.AsyncReq(133);
  sleep(1);
  client.AsyncReq(0);
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
    cout<< "listValue: " << listValue.SerializeAsString() << endl;
    for (int i = 0; i < listValue.list_value_size(); ++i) {
      cout << listValue.list_value(i).scala_msg().float_value() << endl;
    }
  });
  sleep(1);
  grpcClient.close();
}
