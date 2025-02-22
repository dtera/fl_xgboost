//
// Created by HqZhao on 2022/11/23.
//

#include "comm/grpc/XgbServer.h"

XgbServiceAsyncServer::XgbServiceAsyncServer(const uint32_t port, const string& host)
    : server_address_(host + ":" + to_string(port)) {
  Start();
}

void XgbServiceAsyncServer::Start() {
  ServerBuilder builder;
  builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
  builder.RegisterService(&service_);
  grad_cq_ = builder.AddCompletionQueue();
  splits_cq_ = builder.AddCompletionQueue();
  server_ = builder.BuildAndStart();

  grad_stream_.reset(
      new ServerAsyncReaderWriter<GradPairsResponse, GradPairsRequest>(&grad_context_));
  service_.RequestGetEncryptedGradPairs_(&grad_context_, grad_stream_.get(), grad_cq_.get(),
                                         grad_cq_.get(),
                                         reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));

  splits_stream_.reset(
      new ServerAsyncReaderWriter<SplitsResponse, SplitsRequest>(&splits_context_));
  service_.RequestGetEncryptedSplits_(&splits_context_, splits_stream_.get(), splits_cq_.get(),
                                      splits_cq_.get(),
                                      reinterpret_cast<void*>(XgbCommType::SPLITS_CONNECT));

  // This is important as the server should know when the client is done.
  grad_context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(XgbCommType::DONE));
  splits_context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(XgbCommType::DONE));
  grad_thread_.reset(new thread((bind(&XgbServiceAsyncServer::GradThread, this))));
  splits_thread_.reset(new thread((bind(&XgbServiceAsyncServer::SplitsThread, this))));

  cout << "Server listening on " << server_address_ << endl;
}

void XgbServiceAsyncServer::AsyncWaitForRequest(XgbCommType t) {
  if (is_running_) {
    switch (t) {
      case XgbCommType::GRAD_READ:
        grad_stream_->Read(&grad_request_, reinterpret_cast<void*>(XgbCommType::GRAD_READ));
        break;
      case XgbCommType::SPLITS_READ:
        splits_stream_->Read(&splits_request_, reinterpret_cast<void*>(XgbCommType::SPLITS_READ));
        break;
      default:
        LOG(FATAL) << "Unexpected type: " << static_cast<int>(t) << endl;
        GPR_ASSERT(false);
    }
  }
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantConditionsOC"
void XgbServiceAsyncServer::AsyncSendResponse(XgbCommType t) {
  if (t == XgbCommType::GRAD_WRITE) {
    GradPairsResponse gradPairsResponse;
    setGradPairsResponse(gradPairsResponse);
    grad_stream_->Write(gradPairsResponse, reinterpret_cast<void*>(XgbCommType::GRAD_WRITE));
  } else if (t == XgbCommType::SPLITS_WRITE) {
    SplitsResponse splitsResponse;
    setSplitsResponse(splitsResponse);
    splits_stream_->Write(splitsResponse, reinterpret_cast<void*>(XgbCommType::SPLITS_WRITE));
  } else {
    LOG(FATAL) << "Unexpected type: " << static_cast<int>(t) << endl;
    GPR_ASSERT(false);
  }
}

void XgbServiceAsyncServer::setGradPairsResponse(GradPairsResponse& gradPairsResponse) {
  gradPairsResponse.set_version(grad_request_.version());
  cout << "gradPairsResponse: " << gradPairsResponse.version() << endl;
}

void XgbServiceAsyncServer::setSplitsResponse(SplitsResponse& splitsResponse) {
  // cout << "splitsResponse: " << splitsResponse.version() << endl;
}

namespace {
#define GrpcThread(t, TYPE)                                                          \
  while (true) {                                                                     \
    void* tag = nullptr;                                                             \
    bool ok = false;                                                                 \
    if (!t##_cq_->Next(&tag, &ok)) {                                                 \
      break;                                                                         \
    }                                                                                \
    if (ok) {                                                                        \
      DEBUG << endl << #t << "**** Processing completion queue tag " << tag << endl; \
      switch (static_cast<XgbCommType>(reinterpret_cast<size_t>(tag))) {             \
        case XgbCommType::TYPE##_CONNECT:                                            \
          DEBUG << #t << " service connected." << endl;                              \
          AsyncWaitForRequest(XgbCommType::TYPE##_READ);                             \
          break;                                                                     \
        case XgbCommType::TYPE##_READ:                                               \
          DEBUG << #t << " read grad pairs." << endl;                                \
          AsyncSendResponse(XgbCommType::TYPE##_WRITE);                              \
          break;                                                                     \
        case XgbCommType::TYPE##_WRITE:                                              \
          DEBUG << #t << " sending grad pairs(async)." << endl;                      \
          AsyncWaitForRequest(XgbCommType::TYPE##_READ);                             \
          break;                                                                     \
        case XgbCommType::DONE:                                                      \
          DEBUG << #t << " server disconnecting." << endl;                           \
          is_running_ = false;                                                       \
          break;                                                                     \
        case XgbCommType::FINISH:                                                    \
          DEBUG << #t << " server quit." << endl;                                    \
          break;                                                                     \
        default:                                                                     \
          cerr << #t << " unexpected tag." << tag << endl;                           \
      }                                                                              \
    }                                                                                \
  }
}  // namespace

void XgbServiceAsyncServer::GradThread() { GrpcThread(grad, GRAD) }

void XgbServiceAsyncServer::SplitsThread() { GrpcThread(splits, SPLITS) }

bool XgbServiceAsyncServer::IsRunning() { return is_running_; }

void XgbServiceAsyncServer::Stop() {
  cout << "Shutting down server...." << endl;
  server_->Shutdown();
  grad_cq_->Shutdown();
  grad_thread_->join();
  splits_cq_->Shutdown();
  splits_thread_->join();
}

//=================================XgbServiceServer Begin=================================
XgbServiceServer::XgbServiceServer(const uint32_t port, const string& host) { Start(port, host); }

void XgbServiceServer::Start(const uint32_t port, const string& host, int32_t n_threads) {
  n_threads_ = n_threads;
  server_address_ = host + ":" + to_string(port);
  xgb_thread_.reset(new thread((bind(&XgbServiceServer::Run, this))));

  // init for rpc server
  // metrics_.resize(max_iter);
}

void XgbServiceServer::Run() {
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
  builder.AddChannelArgument("grpc.max_send_message_length", MAX_MESSAGE_LENGTH);
  builder.AddChannelArgument("grpc.max_receive_message_length", MAX_MESSAGE_LENGTH);
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(this);
  // Finally assemble the server.
  server_ = builder.BuildAndStart();
  LOG(CONSOLE) << "** RPC Server is listening on " << server_address_ << "..." << endl;
  server_->Wait();
}

void XgbServiceServer::Shutdown() {
  std::unique_lock<std::mutex> lk(mtx);
  while (!finished_) {
    cv.wait(lk);
  }  // wait for the data holder part
  // this_thread::sleep_for(chrono::milliseconds(10));
  LOG(CONSOLE) << "** RPC Server is Shutdowning..." << endl;
  server_->Shutdown();
  xgb_thread_->join();
}

void XgbServiceServer::ResizeNextNode(size_t n) {
  next_nodes_.clear();
  next_nodes_clear_ = true;
  cv.notify_one();
  next_nodes_.resize(n);
}

void XgbServiceServer::ClearNextNodeV2() {
  std::unique_lock<std::mutex> lk(mtx);
  while (!cli_next_nodes_clear_) {
    // LOG(CONSOLE) << "ClearNextNodeV2 Wait Before" << endl;
    cv.wait(lk);
  }  // wait for the data part
  // LOG(CONSOLE) << "ClearNextNodeV2 Wait After" << endl;
  cli_next_nodes_clear_ = false;

  next_nodes_v2_.clear();
  next_nodes_clear_ = true;
  cv.notify_all();
}

void XgbServiceServer::ResizeMetrics(int iter, size_t n) {
  metrics_.resize(iter + 1);
  metrics_[iter].resize(n);
}

void XgbServiceServer::SendPubKey(opt_public_key_t* pub) { pub_ = pub; }

void XgbServiceServer::SetPriKey(opt_private_key_t* pri) { pri_ = pri; }

void XgbServiceServer::SetTrainParam(const TrainParam* train_param) { train_param_ = train_param; }

void XgbServiceServer::SendGradPairs(mpz_t* grad_pairs, size_t size) {
  // grad_pairs_.insert({version, {size, encrypted_grad_pairs}});
}

void XgbServiceServer::SendGradPairs(const vector<xgboost::EncryptedGradientPair>& grad_pairs) {
  if (grad_pairs_.count(cur_version - 1) != 0) {
    grad_pairs_.erase(cur_version - 1);
  }
  grad_pairs_.insert({cur_version, {grad_pairs.size(), grad_pairs}});
  cv.notify_one();
}

void XgbServiceServer::SendSplits(XgbEncryptedSplit* splits, size_t size) {
  splits_.insert({cur_version, {size, splits}});
}

void XgbServiceServer::SendLeftRightNodeSize(size_t node_in_set, size_t n_left, size_t n_right) {
  // lock_guard lk(m);
  left_right_nodes_sizes_.insert({node_in_set, {n_left, n_right}});
  cv.notify_one();
}

void XgbServiceServer::SendFewerRight(int32_t nid, bool fewer_right) {
  fewer_right_.insert({nid, fewer_right});
  // cv.notify_one();
}

void XgbServiceServer::SendBlockInfo(size_t task_idx, PositionBlockInfo* block_info) {
  // lock_guard lk(m);
  block_infos_.insert({task_idx, make_shared<PositionBlockInfo>(*block_info)});
  cv.notify_all();
}

void XgbServiceServer::SendNextNode(size_t k, int32_t nid, bool flow_left) {
  // lock_guard lk(m);
  next_nodes_[k].insert({nid, flow_left});
  cv.notify_all();
}

void XgbServiceServer::SendNextNodesV2(int idx,
                                       const google::protobuf::Map<uint32_t, uint32_t>& next_nids) {
  // lock_guard lk(m);
  next_nodes_v2_.insert({idx, next_nids});
  cv.notify_all();
}

void XgbServiceServer::SendMetrics(int iter, size_t data_idx, const char* metric_name,
                                   double metric) {
  metrics_[iter][data_idx].insert({metric_name, metric});
  // cv.notify_one();
}

template <typename ExpandEntry>
void XgbServiceServer::UpdateExpandEntry(
    ExpandEntry& e,
    function<void(uint32_t, GradStats<double>&, GradStats<double>&, const SplitsRequest&)>
        update_grad_stats) {
  std::unique_lock<std::mutex> lk(mtx);
  while (!finish_splits_[e.nid]) {
    cv.wait(lk);
  }  // wait for the data holder part

  auto encrypted_splits = splits_requests_[e.nid].encrypted_splits();
  ParallelFor(encrypted_splits.size(), n_threads_, [&](uint32_t i) {
    GradStats<double> left_sum;
    GradStats<double> right_sum;
    GradStats<EncryptedType<double>> encrypted_left_sum;
    GradStats<EncryptedType<double>> encrypted_right_sum;
    mpz_type2_mpz_t(encrypted_left_sum, encrypted_splits[i].left_sum());
    mpz_type2_mpz_t(encrypted_right_sum, encrypted_splits[i].right_sum());
    opt_paillier_decrypt(left_sum, encrypted_left_sum, pub_, pri_);
    opt_paillier_decrypt(right_sum, encrypted_right_sum, pub_, pri_);
    // update grad statistics
    update_grad_stats(i, left_sum, right_sum, splits_requests_[e.nid]);
  });

  entries_.insert({e.nid, e});
  finish_splits_[e.nid] = false;
  cv.notify_one();
}

void XgbServiceServer::UpdateBestEncryptedSplit(uint32_t nidx, const EncryptedSplit& best_split) {
  // lock_guard lk(m);
  best_splits_.insert_or_assign(nidx, best_split);
}

void XgbServiceServer::UpdateFinishSplits(uint32_t nidx, bool finish_split) {
  finish_splits_.insert_or_assign(nidx, finish_split);
}

void XgbServiceServer::GetLeftRightNodeSize(size_t node_in_set, size_t* n_left, size_t* n_right) {
  std::unique_lock<std::mutex> lk(mtx);
  while (left_right_nodes_sizes_.count(node_in_set) == 0) {
    cv.wait(lk);
  }  // wait for data holder part
  auto left_right_node_size = left_right_nodes_sizes_[node_in_set];
  *n_left = left_right_node_size.first;
  *n_right = left_right_node_size.second;
}

void XgbServiceServer::GetBlockInfo(
    size_t task_idx, function<void(shared_ptr<PositionBlockInfo>&)> process_block_info) {
  std::unique_lock<std::mutex> lk(mtx);
  while (block_infos_.count(task_idx) == 0) {
    cv.wait(lk);
  }  // wait for data holder part
  auto block_info = block_infos_[task_idx];
  process_block_info(block_info);
}

void XgbServiceServer::GetNextNode(size_t k, int32_t nid, function<void(bool)> process_next_node) {
  std::unique_lock<std::mutex> lk(mtx);
  while (next_nodes_[k].count(nid) == 0) {
    cv.wait(lk);
  }  // wait for data holder part
  process_next_node(next_nodes_[k][nid]);
}

void XgbServiceServer::GetNextNodesV2(
    int idx, function<void(const google::protobuf::Map<uint32_t, uint32_t>&)> process_part_idxs) {
  std::unique_lock<std::mutex> lk(mtx);
  while (next_nodes_v2_.count(idx) == 0) {
    cv.wait(lk);
  }  // wait for data holder part
  process_part_idxs(next_nodes_v2_[idx]);
  // lock_guard lkg(m);
  // next_nodes_v2_.erase(idx);
}

Status XgbServiceServer::GetPubKey(ServerContext* context, const Request* request,
                                   PubKeyResponse* response) {
  do { /* do nothing, waiting for data prepared. */
  } while (pub_ == nullptr);
  // set response
  pub_key2pb(response, pub_);

  return Status::OK;
}

namespace {
#define GetEncryptedData(type, DATATYPE, process_stats)                                      \
  std::unique_lock<std::mutex> lk(mtx);                                                      \
  while (!type##s_.count(request->version())) { /* do nothing, waiting for data prepared. */ \
    cv.wait(lk);                                                                             \
  }                                                                                          \
  if (type##s_.count(request->version() - 1)) { /* remove the last version if exists. */     \
    type##s_.erase(request->version() - 1);                                                  \
  }                                                                                          \
  response->set_version(request->version());                                                 \
  DEBUG << "response.version: " << response->version() << endl;                              \
  size_t size;                                                                               \
  DATATYPE type##s;                                                                          \
  tie(size, type##s) = type##s_[request->version()];                                         \
  auto encrypted_##type##s = response->mutable_encrypted_##type##s();                        \
  for (int i = 0; i < size; ++i) {                                                           \
    auto encrypted_##type = encrypted_##type##s->Add();                                      \
    process_stats                                                                            \
  }                                                                                          \
  return Status::OK;
}  // namespace

Status XgbServiceServer::GetEncryptedGradPairs(ServerContext* context,
                                               const GradPairsRequest* request,
                                               GradPairsResponse* response){
    // GetEncryptedData(grad_pair, mpz_t, { mpz_t2_mpz_type(encrypted_grad_pair,
    // encrypted_grad_pairs[i]); });
    GetEncryptedData(grad_pair, vector<xgboost::EncryptedGradientPair>,
                     { mpz_t2_mpz_type(encrypted_grad_pair, grad_pairs[i]); })}

Status XgbServiceServer::SendEncryptedSplits(ServerContext* context, const SplitsRequest* request,
                                             SplitsResponse* response) {
  // test histogram
  /*if (request->part_id() == -2) {
    auto encrypted_splits = request->encrypted_splits();
    for (int i = 0; i < encrypted_splits.size(); ++i) {
      GradStats<double> left_sum;
      GradStats<double> right_sum;
      GradStats<EncryptedType<double>> encrypted_left_sum;
      GradStats<EncryptedType<double>> encrypted_right_sum;
      mpz_type2_mpz_t(encrypted_left_sum, encrypted_splits[i].left_sum());
      mpz_type2_mpz_t(encrypted_right_sum, encrypted_splits[i].right_sum());
      opt_paillier_decrypt(left_sum, encrypted_left_sum, pub_, pri_);
      opt_paillier_decrypt(right_sum, encrypted_right_sum, pub_, pri_);
      cout << "==nid: " << request->nidx() << ", mask_id: " << encrypted_splits[i].mask_id()
           << ", left_sum: " << left_sum << ", right_sum: --" << endl;
    }
    return Status::OK;
  }*/

  splits_requests_.insert({request->nidx(), *request});
  finish_splits_[request->nidx()] = true;
  cv.notify_one();
  std::unique_lock<std::mutex> lk(mtx);
  while (finish_splits_[request->nidx()]) {
    cv.wait(lk);
  }  // wait for the label part

  auto es = response->mutable_encrypted_split();
  auto left_sum = es->mutable_left_sum();
  auto right_sum = es->mutable_right_sum();
  if (best_splits_.count(request->nidx()) != 0) {
    // notify the data holder part: it's split is the best
    auto best_split = best_splits_[request->nidx()];
    es->set_mask_id(best_split.mask_id());
    es->set_d_step(best_split.d_step());
    es->set_default_left(best_split.default_left());
    left_sum->CopyFrom(best_split.left_sum());
    right_sum->CopyFrom(best_split.right_sum());
    response->set_part_id(request->part_id());
  } else {
    // notify the data holder part: the label holder is the best
    auto entry = entries_[request->nidx()];
    es->set_default_left(entry.split.DefaultLeft());
    response->set_part_id(entry.split.part_id);
    GradStats<EncryptedType<double>> encrypted_left_sum;
    GradStats<EncryptedType<double>> encrypted_right_sum;
    opt_paillier_encrypt(encrypted_left_sum, entry.split.left_sum, pub_);
    opt_paillier_encrypt(encrypted_right_sum, entry.split.right_sum, pub_);
    mpz_t2_mpz_type(left_sum, encrypted_left_sum);
    mpz_t2_mpz_type(right_sum, encrypted_right_sum);
  }

  return Status::OK;
}

Status XgbServiceServer::IsSplitEntryValid(ServerContext* context,
                                           const SplitEntryValidRequest* request,
                                           ValidResponse* response) {
  bool is_valid;
  if (request->num_leaves() == -1) {
    is_valid = entries_[request->nidx()].split.loss_chg > xgboost::kRtEps;
  } else {
    is_valid = entries_[request->nidx()].IsValid(*train_param_, request->num_leaves());
  }
  response->set_is_valid(is_valid);

  return Status::OK;
}

Status XgbServiceServer::IsSplitContainsMissingValues(ServerContext* context,
                                                      const MissingValuesRequest* request,
                                                      ValidResponse* response) {
  GradStats<EncryptedType<double>> encrypted_grad_stats;
  GradStats<EncryptedType<double>> encrypted_snode_stats;
  mpz_type2_mpz_t(encrypted_grad_stats, request->grad_stats());
  mpz_type2_mpz_t(encrypted_snode_stats, request->snode_stats());

  GradStats<double> grad_stats;
  GradStats<double> snode_stats;
  opt_paillier_decrypt(grad_stats, encrypted_grad_stats, pub_, pri_);
  opt_paillier_decrypt(snode_stats, encrypted_snode_stats, pub_, pri_);

  response->set_is_valid(grad_stats.GetGrad() == snode_stats.GetGrad() &&
                         grad_stats.GetHess() == snode_stats.GetHess());

  return Status::OK;
}

Status XgbServiceServer::IsFewerRight(ServerContext* context, const IsFewerRightRequest* request,
                                      ValidResponse* response) {
  EncryptedType<double> encrypted_left_sum_hess;
  EncryptedType<double> encrypted_right_sum_hess;
  mpz_type2_mpz_t(encrypted_left_sum_hess, request->left_sum_hess());
  mpz_type2_mpz_t(encrypted_right_sum_hess, request->right_sum_hess());

  double left_sum_hess;
  double right_sum_hess;
  opt_paillier_decrypt_t(left_sum_hess, encrypted_left_sum_hess.data_, pub_, pri_);
  opt_paillier_decrypt_t(right_sum_hess, encrypted_right_sum_hess.data_, pub_, pri_);

  response->set_is_valid(right_sum_hess < left_sum_hess);

  return Status::OK;
}

Status XgbServiceServer::FewerRight(ServerContext* context, const Request* request,
                                    ValidResponse* response) {
  // std::unique_lock<std::mutex> lk(mtx);
  while (fewer_right_.count(request->idx()) == 0) {
    // cv.wait(lk);
  }  // wait for the label part

  response->set_is_valid(fewer_right_[request->idx()]);

  return Status::OK;
}

Status XgbServiceServer::GetLeftRightNodeSize(ServerContext* context, const Request* request,
                                              BlockInfo* response) {
  std::unique_lock<std::mutex> lk(mtx);
  while (left_right_nodes_sizes_.count(request->idx()) == 0) {
    cv.wait(lk);
  }  // wait for the label part
  auto left_right_node_size = left_right_nodes_sizes_[request->idx()];

  response->set_idx(request->idx());
  response->set_n_left(left_right_node_size.first);
  response->set_n_right(left_right_node_size.second);

  return Status::OK;
}

Status XgbServiceServer::SendLeftRightNodeSize(ServerContext* context, const BlockInfo* request,
                                               Response* response) {
  SendLeftRightNodeSize(request->idx(), request->n_left(), request->n_right());
  return Status::OK;
}

Status XgbServiceServer::GetBlockInfo(ServerContext* context, const Request* request,
                                      BlockInfo* response) {
  std::unique_lock<std::mutex> lk(mtx);
  while (block_infos_.count(request->idx()) == 0) {
    cv.wait(lk);
  }  // wait for the label part
  auto block_info = block_infos_[request->idx()];

  response->set_idx(request->idx());
  response->set_n_left(block_info->n_left);
  response->set_n_right(block_info->n_right);
  response->set_n_offset_left(block_info->n_offset_left);
  response->set_n_offset_right(block_info->n_offset_right);
  auto left_data = response->mutable_left_data_();
  for (int i = 0; i < block_info->n_left; ++i) {
    left_data->Add(block_info->left_data_[i]);
  }
  auto right_data = response->mutable_right_data_();
  for (int i = 0; i < block_info->n_right; ++i) {
    right_data->Add(block_info->right_data_[i]);
  }

  return Status::OK;
}

Status XgbServiceServer::SendBlockInfo(ServerContext* context, const BlockInfo* request,
                                       Response* response) {
  PositionBlockInfo* block_info = new PositionBlockInfo;
  block_info->n_left = request->n_left();
  block_info->n_right = request->n_right();
  block_info->n_offset_left = request->n_offset_left();
  block_info->n_offset_right = request->n_offset_right();
  block_info->left_data_ = new size_t[request->n_left()];
  for (int i = 0; i < block_info->n_left; ++i) {
    block_info->left_data_[i] = request->left_data_(i);
  }
  block_info->right_data_ = new size_t[request->n_right()];
  for (int i = 0; i < block_info->n_right; ++i) {
    block_info->right_data_[i] = request->right_data_(i);
  }

  SendBlockInfo(request->idx(), block_info);
  return Status::OK;
}

Status XgbServiceServer::GetNextNode(ServerContext* context, const NextNode* request,
                                     NextNode* response) {
  std::unique_lock<std::mutex> lk(mtx);
  while (next_nodes_[request->k()].count(request->nid()) == 0) {
    cv.wait(lk);
  }  // wait for the label part
  // response->set_nid(request->nid());
  response->set_flow_left(next_nodes_[request->k()][request->nid()]);

  return Status::OK;
}

Status XgbServiceServer::SendNextNode(ServerContext* context, const NextNode* request,
                                      Response* response) {
  SendNextNode(request->k(), request->nid(), request->flow_left());
  return Status::OK;
}

Status XgbServiceServer::GetNextNodesV2(ServerContext* context, const Request* request,
                                        NextNodesV2* response) {
  auto next_ids = response->mutable_next_ids();
  GetNextNodesV2(request->idx(), [&](const google::protobuf::Map<uint32_t, uint32_t>& next_ids_) {
    *next_ids = next_ids_;
  });

  return Status::OK;
}

Status XgbServiceServer::SendNextNodesV2(ServerContext* context, const NextNodesV2* request,
                                         Response* response) {
  SendNextNodesV2(request->idx(), request->next_ids());

  return Status::OK;
}

Status XgbServiceServer::GetMetric(ServerContext* context, const MetricRequest* request,
                                   MetricResponse* response) {
  // std::unique_lock<std::mutex> lk(mtx);
  while (metrics_[request->iter()][request->data_idx()].count(request->metric_name()) == 0) {
    // cv.wait(lk);
  }  // wait for the label part
  response->set_metric(metrics_[request->iter()][request->data_idx()].at(request->metric_name()));

  return Status::OK;
}

Status XgbServiceServer::Clear(ServerContext* context, const Request* request, Response* response) {
  if (request->idx() == 0) {
    finish_splits_.clear();
    fewer_right_.clear();
    splits_requests_.clear();
    best_splits_.clear();
    entries_.clear();
    left_right_nodes_sizes_.clear();
    block_infos_.clear();
  } else if (request->idx() == 1) {
    cli_next_nodes_clear_ = true;
    cv.notify_all();
    std::unique_lock<std::mutex> lk(mtx);
    while (!next_nodes_clear_) {
      // LOG(CONSOLE) << "Clear Wait Before" << endl;
      cv.wait(lk);
    }  // wait for the label part
    // LOG(CONSOLE) << "Clear Wait After" << endl;
    next_nodes_clear_ = false;
  } else {
    splits_.clear();
    if (cur_version == max_iter) {
      // cout << "cur_version: " << cur_version << ", max_iter: " << max_iter << endl;
      grad_pairs_.clear();
      finished_ = true;
      cv.notify_one();
    }
  }

  return Status::OK;
}

template void XgbServiceServer::UpdateExpandEntry(
    CPUExpandEntry& entry,
    function<void(uint32_t, GradStats<double>&, GradStats<double>&, const SplitsRequest&)>
        update_grad_stats);
//=================================XgbServiceServer End===================================
