//
// Created by HqZhao on 2022/11/23.
//
#pragma once

#include <gmp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "common.h"
#include "tree/hist/expand_entry.h"
#include "xgbcomm.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;
using xgbcomm::GradPairsRequest;
using xgbcomm::GradPairsResponse;
using xgbcomm::SplitsRequest;
using xgbcomm::SplitsResponse;
using xgbcomm::XgbService;

using namespace std;
using namespace xgbcomm;
using namespace xgboost::tree;

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

  void setGradPairsResponse(GradPairsResponse &gradPairsResponse);

  void setSplitsResponse(SplitsResponse &splitsResponse);

  void AsyncWaitForRequest(XgbCommType t);

  void AsyncSendResponse(XgbCommType t);

  void GradThread();

  void SplitsThread();

  void Start();

 public:
  XgbServiceAsyncServer(const uint32_t port = 50001, const string &host = "0.0.0.0");

  bool IsRunning();

  void Stop();
};

//=================================XgbServiceServer Begin=================================
class XgbServiceServer final : public XgbService::Service {
 private:
  string server_address_;
  unique_ptr<thread> xgb_thread_;
  unique_ptr<Server> server_;
  int32_t n_threads_;
  // unordered_map<uint32_t, pair<size_t, mpz_t *>> grad_pairs_;
  unordered_map<uint32_t, pair<size_t, const vector<xgboost::EncryptedGradientPair>>> grad_pairs_;
  unordered_map<uint32_t, pair<size_t, XgbEncryptedSplit *>> splits_;
  opt_public_key_t *pub_;
  opt_private_key_t *pri_;
  unordered_map<uint32_t, const SplitsRequest> splits_requests_;
  bool finish_split_ = false;
  bool finished_ = false;

 public:
  uint32_t cur_version = 0;
  uint32_t max_version = -1;

  explicit XgbServiceServer() = default;

  XgbServiceServer(const uint32_t port, const string &host);

  void Start(const uint32_t port = 50001, const string &host = "0.0.0.0",
             int32_t n_threads = omp_get_num_procs());

  void Run();

  void Shutdown();

  void SendPubKey(opt_public_key_t *pub);

  void SetPriKey(opt_private_key_t *pri);

  void SendGradPairs(mpz_t *grad_pairs, size_t size);

  void SendGradPairs(const vector<xgboost::EncryptedGradientPair> &grad_pairs);

  void SendSplits(XgbEncryptedSplit *splits, size_t size);

  template <typename ExpandEntry>
  void UpdateExpandEntry(
      std::vector<ExpandEntry> *entries,
      function<void(uint32_t, GradStats<double> &, GradStats<double> &, const SplitsRequest &)>
          update_grad_stats);

  Status GetPubKey(ServerContext *context, const Request *request,
                   PubKeyResponse *response) override;

  Status GetEncryptedGradPairs(ServerContext *context, const GradPairsRequest *request,
                               GradPairsResponse *response) override;

  Status SendEncryptedSplits(ServerContext *context, const SplitsRequest *request,
                             SplitsResponse *response) override;
};
//=================================XgbServiceServer End===================================