//
// Created by HqZhao on 2022/11/23.
//

#include "comm/grpc/XgbClient.h"

XgbServiceAsyncClient::XgbServiceAsyncClient(const uint32_t port, const string& host)
    : stub_(XgbService::NewStub(
          grpc::CreateChannel(host + ":" + to_string(port), grpc::InsecureChannelCredentials()))) {
  grad_thread_.reset(new thread(bind(&XgbServiceAsyncClient::GradThread, this)));
  splits_thread_.reset(new thread(bind(&XgbServiceAsyncClient::SplitsThread, this)));
  grad_stream_ = stub_->AsyncGetEncryptedGradPairs_(
      &grad_context_, &grad_cq_, reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));
  splits_stream_ = stub_->AsyncGetEncryptedSplits_(
      &splits_context_, &splits_cq_, reinterpret_cast<void*>(XgbCommType::SPLITS_CONNECT));
  this_thread::sleep_for(chrono::microseconds(520));
}

void XgbServiceAsyncClient::AsyncRequestNextMessage(XgbCommType t) {
  switch (t) {
    case XgbCommType::GRAD_READ:
      grad_stream_->Read(&grad_response_, reinterpret_cast<void*>(XgbCommType::GRAD_READ));
      break;
    case XgbCommType::SPLITS_READ:
      splits_stream_->Read(&splits_response_, reinterpret_cast<void*>(XgbCommType::GRAD_READ));
      break;
    default:
      LOG(FATAL) << "Unexpected type: " << static_cast<int>(t) << endl;
      GPR_ASSERT(false);
  }
}

namespace {
#define GrpcThread(t, TYPE)                                                                        \
  while (true) {                                                                                   \
    void* got_tag;                                                                                 \
    bool ok = false;                                                                               \
    if (!t##_cq_.Next(&got_tag, &ok)) {                                                            \
      cerr << #t << " client stream closed." << endl;                                              \
      break;                                                                                       \
    }                                                                                              \
    if (ok) {                                                                                      \
      DEBUG << endl << #t << "**** Processing completion queue tag " << got_tag << endl;           \
      switch (static_cast<XgbCommType>(reinterpret_cast<long>(got_tag))) {                         \
        case XgbCommType::TYPE##_CONNECT:                                                          \
          DEBUG << #t << " server connected." << endl;                                             \
          break;                                                                                   \
        case XgbCommType::TYPE##_READ:                                                             \
          DEBUG << #t << " read a new message." << endl;                                           \
          break;                                                                                   \
        case XgbCommType::TYPE##_WRITE:                                                            \
          DEBUG << #t << " sending message(async)." << endl;                                       \
          AsyncRequestNextMessage(XgbCommType::TYPE##_READ);                                       \
          break;                                                                                   \
        case XgbCommType::DONE:                                                                    \
          DEBUG << #t << " server disconnecting." << endl;                                         \
          break;                                                                                   \
        case XgbCommType::FINISH:                                                                  \
          DEBUG << #t << " client finish; status = " << (finish_status_.ok() ? "ok" : "cancelled") \
                << endl;                                                                           \
          t##_context_.TryCancel();                                                                \
          t##_cq_.Shutdown();                                                                      \
          break;                                                                                   \
        default:                                                                                   \
          cerr << #t << " unexpected tag " << got_tag << endl;                                     \
      }                                                                                            \
    }                                                                                              \
  }
}  // namespace

void XgbServiceAsyncClient::GradThread() { GrpcThread(grad, GRAD) }

void XgbServiceAsyncClient::SplitsThread() { GrpcThread(splits, SPLITS) }

bool XgbServiceAsyncClient::AsyncReq(const uint32_t version, XgbCommType t) {
  cout << "** request: " << version << ", grad.version: " << grad_response_.version() << endl;
  if (t == XgbCommType::GRAD_WRITE) {
    if (version == 0) {
      grad_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
      return false;
    }
    GradPairsRequest gradPairsRequest;
    gradPairsRequest.set_version(version);
    grad_stream_->Write(gradPairsRequest, reinterpret_cast<void*>(XgbCommType::GRAD_WRITE));
  } else if (t == XgbCommType::SPLITS_WRITE) {
    if (version == 0) {
      splits_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
      return false;
    }
    SplitsRequest splitsRequest;
    splits_stream_->Write(splitsRequest, reinterpret_cast<void*>(XgbCommType::SPLITS_WRITE));
  } else {
    LOG(FATAL) << "Unexpected type: " << static_cast<int>(t) << endl;
    GPR_ASSERT(false);
  }
  this_thread::sleep_for(chrono::microseconds(520));

  return true;
}

void XgbServiceAsyncClient::Stop() {
  DEBUG << "Shutting down client...." << endl;
  grad_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
  splits_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
  grad_cq_.Shutdown();
  grad_thread_->join();
  splits_cq_.Shutdown();
  splits_thread_->join();
}

//=================================XgbServiceClient Begin=================================
XgbServiceClient::XgbServiceClient(const uint32_t port, const string& host, int32_t n_threads) {
  Start(port, host, n_threads);
}

void XgbServiceClient::Start(const uint32_t port, const string& host, int32_t n_threads) {
  n_threads_ = n_threads;
  channel_args_.SetMaxReceiveMessageSize(-1);
  channel_args_.SetMaxReceiveMessageSize(-1);
  stub_ = XgbService::NewStub(grpc::CreateCustomChannel(
      host + ":" + to_string(port), grpc::InsecureChannelCredentials(), channel_args_));
}

void XgbServiceClient::CacheEncryptedSplit(string mask_id, EncryptedSplit* es) {
  lock_guard lk(m);
  encrypted_splits_.insert({mask_id, es});
}

EncryptedSplit* XgbServiceClient::GetEncryptedSplit(string mask_id) {
  return encrypted_splits_[mask_id];
}

void XgbServiceClient::GetPubKey(opt_public_key_t** pub) {
  RpcRequest(Request, GetPubKey, PubKeyResponse, , { pb2pub_key(pub, response); });
}

void XgbServiceClient::GetEncryptedGradPairs(mpz_t* encryptedGradPairs) {
  RpcRequest(GradPairsRequest, GetEncryptedGradPairs, GradPairsResponse,
             request.set_version(cur_version), {
               response.set_version(cur_version);
               auto egps = response.encrypted_grad_pairs();
               // encryptedGradPairs = new mpz_t[egps.size()];
               xgboost::common::ParallelFor(egps.size(), n_threads_, [&](int i) {
                 mpz_type2_mpz_t(encryptedGradPairs[2 * i], egps[i].grad());
                 mpz_type2_mpz_t(encryptedGradPairs[2 * i + 1], egps[i].hess());
               });
             });
  DEBUG << "response.version: " << response.version() << endl;
  DEBUG << "gradPairs[0]._mp_alloc: " << encryptedGradPairs[0]->_mp_alloc << endl;
  DEBUG << "gradPairs[0]._mp_size: " << encryptedGradPairs[0]->_mp_size << endl;
  DEBUG << "gradPairs[0]._mp_d: " << *encryptedGradPairs[0]->_mp_d << endl;
}

void XgbServiceClient::GetEncryptedGradPairs(
    vector<xgboost::EncryptedGradientPair>& encryptedGradPairs) {
  RpcRequest(GradPairsRequest, GetEncryptedGradPairs, GradPairsResponse,
             request.set_version(cur_version), {
               response.set_version(cur_version);
               auto egps = response.encrypted_grad_pairs();
               xgboost::common::ParallelFor(egps.size(), n_threads_, [&](int i) {
                 EncryptedType grad, hess;
                 mpz_type2_mpz_t(grad, egps[i].grad());
                 mpz_type2_mpz_t(hess, egps[i].hess());
                 encryptedGradPairs[i].SetGrad(grad);
                 encryptedGradPairs[i].SetHess(hess);
               });
             });
}

void XgbServiceClient::SendEncryptedSplits(SplitsRequest& splits_request,
                                           function<void(SplitsResponse&)> process_response) {
  RpcRequest_(splits_request, SendEncryptedSplits, SplitsResponse, {
    // if (splits_request.encrypted_splits().empty()) {
    process_response(response);
    //}
  });
}

void XgbServiceClient::GetEncryptedSplits(XgbEncryptedSplit* encryptedSplits) {
  RpcRequest(SplitsRequest, SendEncryptedSplits, SplitsResponse, {},
             {
                 /*
                 auto ess = response.encrypted_splits();
                 xgboost::common::ParallelFor(ess.size(), n_threads_, [&](int i) {
                   encryptedSplits[i].mask_id = ess[i].mask_id();
                   auto t = ess[i].left_sum();
                   // mpz_type2_mpz_t(encryptedSplits[i].encrypted_grad_pair_sum, t);
                   mpz_type2_mpz_t(encryptedSplits[i].encrypted_grad_pair_sum.grad, t.grad());
                   mpz_type2_mpz_t(encryptedSplits[i].encrypted_grad_pair_sum.hess, t.hess());
                 });*/
             });
}

bool XgbServiceClient::IsSplitEntryValid(int nid, xgboost::bst_node_t num_leaves) {
  RpcRequest(
      SplitEntryValidRequest, IsSplitEntryValid, ValidResponse,
      {
        // request.set_version(cur_version);
        request.set_nidx(nid);
        request.set_num_leaves(num_leaves);
      },
      { return response.is_valid(); });

  return response.is_valid();
}

bool XgbServiceClient::IsSplitContainsMissingValues(
    const xgboost::tree::GradStats<EncryptedType<double>>& e,
    const xgboost::tree::GradStats<EncryptedType<double>>& n) {
  RpcRequest(
      MissingValuesRequest, IsSplitContainsMissingValues, ValidResponse,
      {
        auto grad_stats = request.mutable_grad_stats();
        auto snode_stats = request.mutable_snode_stats();
        mpz_t2_mpz_type(grad_stats, e);
        mpz_t2_mpz_type(snode_stats, n);
      },
      { return response.is_valid(); });

  return response.is_valid();
}

bool XgbServiceClient::IsFewerRight(const EncryptedType<double>& left_sum_hess,
                                    const EncryptedType<double>& right_sum_hess) {
  RpcRequest(
      IsFewerRightRequest, IsFewerRight, ValidResponse,
      {
        auto lsh = request.mutable_left_sum_hess();
        auto rsh = request.mutable_right_sum_hess();
        mpz_t2_mpz_type(lsh, left_sum_hess);
        mpz_t2_mpz_type(rsh, right_sum_hess);
      },
      { return response.is_valid(); });

  return response.is_valid();
}

bool XgbServiceClient::FewerRight(int32_t nid) {
  RpcRequest(
      Request, FewerRight, ValidResponse, { request.set_idx(nid); },
      { return response.is_valid(); });

  return response.is_valid();
}

void XgbServiceClient::GetLeftRightNodeSize(size_t node_in_set, size_t* n_left, size_t* n_right) {
  RpcRequest(
      Request, GetLeftRightNodeSize, BlockInfo, { request.set_idx(node_in_set); },
      {
        *n_left = response.n_left();
        *n_right = response.n_right();
      });
}

void XgbServiceClient::SendLeftRightNodeSize(size_t node_in_set, size_t n_left, size_t n_right) {
  RpcRequest(BlockInfo, SendLeftRightNodeSize, Response,
             {
               request.set_idx(node_in_set);
               request.set_n_left(n_left);
               request.set_n_right(n_right);
             },
             {});
}

void XgbServiceClient::GetBlockInfo(size_t task_idx,
                                    function<void(BlockInfo&)> process_block_info) {
  RpcRequest(
      Request, GetBlockInfo, BlockInfo, { request.set_idx(task_idx); },
      { process_block_info(response); });
}

void XgbServiceClient::SendBlockInfo(size_t task_idx, PositionBlockInfo* block_info) {
  RpcRequest(BlockInfo, SendBlockInfo, Response,
             {
               request.set_idx(task_idx);
               request.set_n_left(block_info->n_left);
               request.set_n_right(block_info->n_right);
               request.set_n_offset_left(block_info->n_offset_left);
               request.set_n_offset_right(block_info->n_offset_right);
               auto left_data = request.mutable_left_data_();
               for (int i = 0; i < block_info->n_left; ++i) {
                 left_data->Add(block_info->left_data_[i]);
               }
               auto right_data = request.mutable_right_data_();
               for (int i = 0; i < block_info->n_right; ++i) {
                 right_data->Add(block_info->right_data_[i]);
               }
             },
             {});
}

void XgbServiceClient::GetNextNode(size_t k, int32_t nid, function<void(bool)> process_next_node) {
  RpcRequest(
      NextNode, GetNextNode, NextNode,
      {
        request.set_k(k);
        request.set_nid(nid);
      },
      { process_next_node(response.flow_left()); });
}

void XgbServiceClient::SendNextNode(size_t k, int32_t nid, bool flow_left) {
  RpcRequest(NextNode, SendNextNode, Response,
             {
               request.set_k(k);
               request.set_nid(nid);
               request.set_flow_left(flow_left);
             },
             {});
}

void XgbServiceClient::GetNextNodesV2(
    int idx, function<void(const google::protobuf::Map<uint32_t, uint32_t>&)> process_part_idxs) {
  RpcRequest(
      Request, GetNextNodesV2, NextNodesV2, { request.set_idx(idx); },
      { process_part_idxs(response.next_ids()); });
}

void XgbServiceClient::SendNextNodesV2(
    int idx, oneapi::tbb::concurrent_unordered_map<uint32_t, uint32_t>& part_idxs) {
  RpcRequest(NextNodesV2, SendNextNodesV2, Response,
             {
               request.set_idx(idx);
               auto next_ids = request.mutable_next_ids();
               for (auto part_idx : part_idxs) {
                 next_ids->insert({part_idx.first, part_idx.second});
               };
             },
             {});
}

void XgbServiceClient::GetMetric(int iter, size_t data_idx, const char* metric_name,
                                 function<void(double)> process_metric) {
  RpcRequest(
      MetricRequest, GetMetric, MetricResponse,
      {
        request.set_iter(iter);
        request.set_data_idx(data_idx);
        request.set_metric_name(metric_name);
      },
      { process_metric(response.metric()); });
}

void XgbServiceClient::Clear(int idx) {
  RpcRequest(Request, Clear, Response, { request.set_idx(idx); }, {});
}

//=================================XgbServiceClient End===================================