//
// Created by HqZhao on 2022/11/23.
//
#pragma once

#include <gmp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <boost/algorithm/string.hpp>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>

#include "common.h"
#include "common/threading_utils.h"
#include "opt_paillier.h"
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

class XgbServiceAsyncClient {
 private:
  ClientContext grad_context_;
  ClientContext splits_context_;
  CompletionQueue grad_cq_;
  CompletionQueue splits_cq_;
  unique_ptr<XgbService::Stub> stub_;
  unique_ptr<ClientAsyncReaderWriter<GradPairsRequest, GradPairsResponse>> grad_stream_;
  unique_ptr<ClientAsyncReaderWriter<SplitsRequest, SplitsResponse>> splits_stream_;
  GradPairsResponse grad_response_;
  SplitsResponse splits_response_;
  unique_ptr<thread> grad_thread_;
  unique_ptr<thread> splits_thread_;
  grpc::Status finish_status_ = grpc::Status::OK;

  void AsyncRequestNextMessage(XgbCommType t);

  void GradThread();

  void SplitsThread();

 public:
  XgbServiceAsyncClient(const uint32_t port = 50001, const string &host = "0.0.0.0");

  bool AsyncReq(const uint32_t version, XgbCommType t = XgbCommType::GRAD_WRITE);

  void Stop();
};

//=================================XgbServiceClient Begin=================================
class XgbServiceClient {
 private:
  std::unique_ptr<XgbService::Stub> stub_;
  int32_t n_threads_;
  grpc::ChannelArguments channel_args_;
  unordered_map<string, EncryptedSplit *> encrypted_splits_;
  shared_timed_mutex m{};

 public:
  uint32_t cur_version = 0;

  explicit XgbServiceClient() = default;

  XgbServiceClient(const uint32_t por, const string &host, int32_t n_threads);

  void Start(const uint32_t port = 50001, const string &host = "0.0.0.0",
             int32_t n_threads = omp_get_num_procs());

  void CacheEncryptedSplit(string mask_id, EncryptedSplit *es);

  EncryptedSplit *GetEncryptedSplit(string mask_id);

  void GetPubKey(opt_public_key_t **pub);

  void GetEncryptedGradPairs(mpz_t *encryptedGradPairs);

  void GetEncryptedGradPairs(vector<xgboost::EncryptedGradientPair> &encryptedGradPairs);

  void SendEncryptedSplits(
      SplitsRequest &splits_request,
      function<void(SplitsResponse &)> process_response = [](SplitsResponse &) {});

  void GetEncryptedSplits(XgbEncryptedSplit *encryptedSplits);

  bool IsSplitEntryValid(int nid, xgboost::bst_node_t num_leaves);

  bool IsSplitContainsMissingValues(const xgboost::tree::GradStats<EncryptedType<double>> &e,
                                    const xgboost::tree::GradStats<EncryptedType<double>> &n);

  bool IsSplitContainsMissingValues(const xgboost::tree::GradStats<double> &e,
                                    const xgboost::tree::GradStats<double> &n) {}

  bool IsFewerRight(const EncryptedType<double> &left_sum_hess,
                    const EncryptedType<double> &right_sum_hess);

  void GetLeftRightNodeSize(size_t node_in_set, size_t *n_left, size_t *n_right);

  void SendLeftRightNodeSize(size_t node_in_set, size_t n_left, size_t n_right);

  void GetBlockInfo(size_t task_idx, function<void(BlockInfo &)> process_block_info);

  void SendBlockInfo(size_t task_idx, PositionBlockInfo *block_info);

  void GetNextNode(size_t k, int32_t nid, function<void(bool)> process_next_node);

  void SendNextNode(size_t k, int32_t nid, bool flow_left);

  void GetMetric(int iter, const char *metric_name, function<void(double)> process_metric);

  void Clear(int idx = 0);
};
//=================================XgbServiceClient End===================================