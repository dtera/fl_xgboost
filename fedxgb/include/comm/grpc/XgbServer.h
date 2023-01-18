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
#include <shared_mutex>
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

  const TrainParam *train_param_;
  unordered_map<uint32_t, bool> finish_splits_;
  unordered_map<uint32_t, const SplitsRequest> splits_requests_;
  unordered_map<uint32_t, const EncryptedSplit> best_splits_;
  unordered_map<uint32_t, const CPUExpandEntry> entries_;
  unordered_map<size_t, const pair<size_t, size_t>> left_right_nodes_sizes_;
  vector<std::shared_ptr<PositionBlockInfo>> block_infos_;
  bool finished_ = false;
  // shared mutex to control updating the mask id
  std::shared_timed_mutex m{};

 public:
  uint32_t cur_version{0};
  uint32_t max_version{std::numeric_limits<uint32_t>().max()};

  explicit XgbServiceServer() = default;

  XgbServiceServer(const uint32_t port, const string &host);

  void Start(const uint32_t port = 50001, const string &host = "0.0.0.0",
             int32_t n_threads = omp_get_num_procs());

  void Run();

  void Shutdown();

  void SendPubKey(opt_public_key_t *pub);

  void SetPriKey(opt_private_key_t *pri);

  void SetTrainParam(const TrainParam *train_param);

  void SendGradPairs(mpz_t *grad_pairs, size_t size);

  void SendGradPairs(const vector<xgboost::EncryptedGradientPair> &grad_pairs);

  void SendSplits(XgbEncryptedSplit *splits, size_t size);

  void SendLeftRightNodeSize(size_t node_in_set, size_t n_left, size_t n_right);

  void ReSizeBlockInfo(size_t n_tasks);

  void SendBlockInfo(size_t task_idx, PositionBlockInfo *block_info);

  template <typename ExpandEntry>
  void UpdateExpandEntry(
      ExpandEntry &entry,
      function<void(uint32_t, GradStats<double> &, GradStats<double> &, const SplitsRequest &)>
          update_grad_stats);

  void UpdateBestEncryptedSplit(uint32_t nidx, const EncryptedSplit &best_split);

  void UpdateFinishSplits(uint32_t nidx, bool finish_split = false);

  void GetLeftRightNodeSize(size_t node_in_set, size_t *n_left, size_t *n_right);

  Status GetPubKey(ServerContext *context, const Request *request,
                   PubKeyResponse *response) override;

  Status GetEncryptedGradPairs(ServerContext *context, const GradPairsRequest *request,
                               GradPairsResponse *response) override;

  Status SendEncryptedSplits(ServerContext *context, const SplitsRequest *request,
                             SplitsResponse *response) override;

  Status IsSplitEntryValid(ServerContext *context, const SplitEntryValidRequest *request,
                           SplitEntryValidResponse *response) override;

  Status GetLeftRightNodeSize(ServerContext *context, const Request *request,
                              BlockInfo *response) override;

  Status SendLeftRightNodeSize(ServerContext *context, const BlockInfo *request,
                               Response *response) override;

  Status GetBlockInfo(ServerContext *context, const Request *request, BlockInfo *response) override;

  Status SendBlockInfo(ServerContext *context, const BlockInfo *request,
                       Response *response) override;
};
//=================================XgbServiceServer End===================================