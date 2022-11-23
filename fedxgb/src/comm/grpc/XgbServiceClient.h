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

using grpc::Channel;
using grpc::ClientAsyncReaderWriter;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using xgbcomm::GradPairsRequest;
using xgbcomm::GradPairsResponse;
using xgbcomm::SplitsRequest;
using xgbcomm::SplitsResponse;
using xgbcomm::XgbService;

using namespace std;
using namespace xgbcomm;

class XgbServiceClient {
 private:
  ClientContext context_;
  CompletionQueue cq_;
  unique_ptr<XgbService::Stub> stub_;
  unique_ptr<ClientAsyncReaderWriter<GradPairsRequest, GradPairsResponse>> grad_stream_;
  unique_ptr<ClientAsyncReaderWriter<SplitsRequest, SplitsResponse>> splits_stream_;
  GradPairsResponse grad_response_;
  SplitsResponse splits_response_;
  unique_ptr<thread> grpc_thread_;
  grpc::Status finish_status_ = grpc::Status::OK;
  string server_address_;

  void AsyncRequestNextMessage(XgbCommType t);

  void GrpcThread();

 public:
  XgbServiceClient(const uint32_t port = 50001, const string& host = "0.0.0.0");

  bool AsyncReq(const uint32_t version, XgbCommType t = XgbCommType::GRAD_WRITE);

  void Start(XgbCommType t);

  void Stop();
};
