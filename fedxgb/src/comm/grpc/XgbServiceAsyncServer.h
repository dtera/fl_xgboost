//
// Created by HqZhao on 2022/11/23.
//
#pragma once

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "common.h"
#include "xgbcomm.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using xgbcomm::GradPairsRequest;
using xgbcomm::GradPairsResponse;
using xgbcomm::SplitsRequest;
using xgbcomm::SplitsResponse;
using xgbcomm::XgbService;

using namespace std;
using namespace xgbcomm;

class XgbServiceAsyncServer {
 private:
  XgbService::AsyncService service_;
  unique_ptr<Server> server_;
  unique_ptr<ServerCompletionQueue> grad_cq_;
  unique_ptr<ServerCompletionQueue> splits_cq_;
  ServerContext grad_context_;
  ServerContext splits_context_;
  GradPairsRequest grad_request_;
  SplitsRequest splits_request_;
  unique_ptr<ServerAsyncReaderWriter<GradPairsResponse, GradPairsRequest>> grad_stream_;
  unique_ptr<ServerAsyncReaderWriter<SplitsResponse, SplitsRequest>> splits_stream_;
  unique_ptr<thread> grad_thread_;
  unique_ptr<thread> splits_thread_;
  bool is_running_ = true;
  string server_address_;

  void setGradPairsResponse(GradPairsResponse& gradPairsResponse);

  void setSplitsResponse(SplitsResponse& splitsResponse);

  void AsyncWaitForRequest(XgbCommType t);

  void AsyncSendResponse(XgbCommType t);

  void GradThread();

  void SplitsThread();

  void Start();

 public:
  XgbServiceAsyncServer(const uint32_t port = 50001, const string& host = "0.0.0.0");

  bool IsRunning();

  void Stop();
};
