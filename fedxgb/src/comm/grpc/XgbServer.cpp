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
  splitsResponse.set_version(splits_request_.version());
  cout << "splitsResponse: " << splitsResponse.version() << endl;
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
  cout << "RPC Server listening on " << server_address_ << endl;
  server_->Wait();
}

void XgbServiceServer::Shutdown() {
  while (!finished_) {
  }
  server_->Shutdown();
  xgb_thread_->join();
}

void XgbServiceServer::SendPubKey(opt_public_key_t* pub) { pub_ = pub; }

void XgbServiceServer::SetPriKey(opt_private_key_t* pri) { pri_ = pri; }

void XgbServiceServer::SetTrainParam(const TrainParam* train_param) { train_param_ = train_param; }

void XgbServiceServer::SendGradPairs(mpz_t* grad_pairs, size_t size) {
  // grad_pairs_.insert({version, {size, encrypted_grad_pairs}});
}

void XgbServiceServer::SendGradPairs(const vector<xgboost::EncryptedGradientPair>& grad_pairs) {
  grad_pairs_.insert({cur_version, {grad_pairs.size(), grad_pairs}});
}

void XgbServiceServer::SendSplits(XgbEncryptedSplit* splits, size_t size) {
  splits_.insert({cur_version, {size, splits}});
}

void XgbServiceServer::SendLeftRightNodeSize(size_t node_in_set, size_t n_left, size_t n_right) {
  left_right_nodes_sizes_.insert({node_in_set, {n_left, n_right}});
}

template <typename ExpandEntry>
void XgbServiceServer::UpdateExpandEntry(
    ExpandEntry& e,
    function<void(uint32_t, GradStats<double>&, GradStats<double>&, const SplitsRequest&)>
        update_grad_stats) {
  entries_.insert({e.nid, e});
  while (!finish_splits_[e.nid]) {
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

  finish_splits_[e.nid] = false;
}

void XgbServiceServer::UpdateBestEncryptedSplit(uint32_t nidx, const EncryptedSplit& best_split) {
  lock_guard lk(m);
  best_splits_.insert({nidx, best_split});
}

void XgbServiceServer::UpdateFinishSplits(uint32_t nidx, bool finish_split) {
  finish_splits_.insert({nidx, finish_split});
}

void XgbServiceServer::GetLeftRightNodeSize(size_t node_in_set, size_t* n_left, size_t* n_right) {
  while (left_right_nodes_sizes_.count(node_in_set) == 0) {
  }  // wait for data holder part
  auto left_right_node_size = left_right_nodes_sizes_[node_in_set];
  *n_left = left_right_node_size.first;
  *n_right = left_right_node_size.second;
}

Status XgbServiceServer::GetPubKey(ServerContext* context, const Request* request,
                                   PubKeyResponse* response) {
  do { /* do nothing, waiting for data prepared. */
  } while (pub_ == nullptr);
  // set response
  response->set_nbits(pub_->nbits);
  response->set_lbits(pub_->lbits);
  mpz_t2_mpz_type(response->mutable_n(), pub_->n);
  mpz_t2_mpz_type(response->mutable_half_n(), pub_->half_n);
  mpz_t2_mpz_type(response->mutable_n_squared(), pub_->n_squared);
  mpz_t2_mpz_type(response->mutable_h_s(), pub_->h_s);
  mpz_t2_mpz_type(response->mutable_p_squared_mul_p_squared_inverse(),
                  pub_->P_squared_mul_P_squared_inverse);
  auto fb_mod_p_sqaured = response->mutable_fb_mod_p_sqaured();
  mpz_t2_mpz_type(fb_mod_p_sqaured->mutable_m_mod(), pub_->fb_mod_P_sqaured.m_mod);
  for (int i = 0; i <= pub_->fb_mod_P_sqaured.m_t; ++i) {
    auto m_table_g = fb_mod_p_sqaured->mutable_m_table_g()->Add();
    mpz_t2_mpz_type(m_table_g, pub_->fb_mod_P_sqaured.m_table_G[i]);
  }
  fb_mod_p_sqaured->set_m_h(pub_->fb_mod_P_sqaured.m_h);
  fb_mod_p_sqaured->set_m_t(pub_->fb_mod_P_sqaured.m_t);
  fb_mod_p_sqaured->set_m_w(pub_->fb_mod_P_sqaured.m_w);
  auto fb_mod_q_sqaured = response->mutable_fb_mod_q_sqaured();
  mpz_t2_mpz_type(fb_mod_q_sqaured->mutable_m_mod(), pub_->fb_mod_Q_sqaured.m_mod);
  for (int i = 0; i <= pub_->fb_mod_Q_sqaured.m_t; ++i) {
    auto m_table_g = fb_mod_q_sqaured->mutable_m_table_g()->Add();
    mpz_t2_mpz_type(m_table_g, pub_->fb_mod_Q_sqaured.m_table_G[i]);
  }
  fb_mod_q_sqaured->set_m_h(pub_->fb_mod_Q_sqaured.m_h);
  fb_mod_q_sqaured->set_m_t(pub_->fb_mod_Q_sqaured.m_t);
  fb_mod_q_sqaured->set_m_w(pub_->fb_mod_Q_sqaured.m_w);

  return Status::OK;
}

namespace {
#define GetEncryptedData(type, DATATYPE, process_stats)                                  \
  do { /* do nothing, waiting for data prepared. */                                      \
  } while (!type##s_.count(request->version()));                                         \
  if (type##s_.count(request->version() - 1)) { /* remove the last version if exists. */ \
    type##s_.erase(request->version() - 1);                                              \
  }                                                                                      \
  response->set_version(request->version());                                             \
  DEBUG << "response.version: " << response->version() << endl;                          \
  size_t size;                                                                           \
  DATATYPE type##s;                                                                      \
  tie(size, type##s) = type##s_[request->version()];                                     \
  auto encrypted_##type##s = response->mutable_encrypted_##type##s();                    \
  for (int i = 0; i < size; ++i) {                                                       \
    auto encrypted_##type = encrypted_##type##s->Add();                                  \
    process_stats                                                                        \
  }                                                                                      \
  return Status::OK;
}  // namespace

Status XgbServiceServer::GetEncryptedGradPairs(ServerContext* context,
                                               const GradPairsRequest* request,
                                               GradPairsResponse* response) {
  // GetEncryptedData(grad_pair, mpz_t, { mpz_t2_mpz_type(encrypted_grad_pair,
  // encrypted_grad_pairs[i]); });
  GetEncryptedData(grad_pair, vector<xgboost::EncryptedGradientPair>,
                   { mpz_t2_mpz_type(encrypted_grad_pair, grad_pairs[i]); });
}

Status XgbServiceServer::SendEncryptedSplits(ServerContext* context, const SplitsRequest* request,
                                             SplitsResponse* response) {
  splits_requests_.insert({request->nidx(), *request});
  finish_splits_[request->nidx()] = true;
  while (finish_splits_[request->nidx()]) {
  }  // wait for the label part

  if (best_splits_.count(request->nidx()) != 0) {
    // notify the data holder part: it's split is the best
    response->set_nidx(request->nidx());
    response->set_mask_id((best_splits_[request->nidx()].mask_id()));
    response->set_d_step(best_splits_[request->nidx()].d_step());
    response->set_default_left(best_splits_[request->nidx()].default_left());
    response->set_part_id(request->part_id());
  } else {
    // notify the data holder part: the label holder is the best
    response->set_default_left(entries_[request->nidx()].split.DefaultLeft());
    response->set_part_id(entries_[request->nidx()].split.part_id);
  }

  response->set_version(cur_version);

  if (cur_version == max_version) {
    finished_ = true;
  }

  return Status::OK;
}

Status XgbServiceServer::IsSplitEntryValid(ServerContext* context,
                                           const SplitEntryValidRequest* request,
                                           SplitEntryValidResponse* response) {
  response->set_is_valid(entries_[request->nidx()].IsValid(*train_param_, request->num_leaves()));
  response->set_version(cur_version);

  return Status::OK;
}

Status XgbServiceServer::GetLeftRightNodeSize(ServerContext* context,
                                              const LeftRightNodeSizeRequest* request,
                                              BlockInfo* response) {
  while (left_right_nodes_sizes_.count(request->nidx()) == 0) {
  }  // wait for the label part
  auto left_right_node_size = left_right_nodes_sizes_[request->nidx()];

  response->set_nidx(request->nidx());
  response->set_n_left(left_right_node_size.first);
  response->set_n_right(left_right_node_size.second);

  return Status::OK;
}

Status XgbServiceServer::SendLeftRightNodeSize(ServerContext* context, const BlockInfo* request,
                                               Response* response) {
  left_right_nodes_sizes_.insert({request->nidx(), {request->n_left(), request->n_right()}});

  return Status::OK;
}

template void XgbServiceServer::UpdateExpandEntry(
    CPUExpandEntry& entry,
    function<void(uint32_t, GradStats<double>&, GradStats<double>&, const SplitsRequest&)>
        update_grad_stats);
//=================================XgbServiceServer End===================================
