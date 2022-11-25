//
// Created by HqZhao on 2022/11/23.
//

#include "XgbServer.h"

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
  service_.RequestGetEncriptedGradPairs_(&grad_context_, grad_stream_.get(), grad_cq_.get(),
                                         grad_cq_.get(),
                                         reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));

  splits_stream_.reset(
      new ServerAsyncReaderWriter<SplitsResponse, SplitsRequest>(&splits_context_));
  service_.RequestGetEncriptedSplits_(&splits_context_, splits_stream_.get(), splits_cq_.get(),
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
XgbServiceServer::XgbServiceServer(const uint32_t port, const string& host)
    : server_address_(host + ":" + to_string(port)) {
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
  cout << "Server listening on " << server_address_ << endl;
  server_->Wait();
}

void XgbServiceServer::Shutdown() {
  server_->Shutdown();
  xgb_thread_->join();
}

void XgbServiceServer::SendGradPairs(const uint32_t version, mpz_t* grad_pairs, size_t size) {
  grad_pairs_.insert({version, {size, grad_pairs}});
}

void XgbServiceServer::SendSplits(const uint32_t version, XgbEncriptedSplit* splits, size_t size) {
  splits_.insert({version, {size, splits}});
}

#define GetEncriptedData(type, DATATYPE, process_stats)                                  \
  do { /* do nothing, waiting for data prepared. */                                      \
  } while (!type##s_.count(request->version()));                                         \
  if (type##s_.count(request->version() - 1)) { /* remove the last version if exists. */ \
    type##s_.erase(request->version() - 1);                                              \
  }                                                                                      \
  response->set_version(request->version());                                             \
  DEBUG << "response.version: " << response->version() << endl;                          \
  size_t size;                                                                           \
  DATATYPE* type##s;                                                                     \
  tie(size, type##s) = type##s_[request->version()];                                     \
  auto encripted_##type##s = response->mutable_encripted_##type##s();                    \
  for (int i = 0; i < size; ++i) {                                                       \
    auto encripted_##type = encripted_##type##s->Add();                                  \
    process_stats                                                                        \
  }                                                                                      \
  return Status::OK;

Status XgbServiceServer::GetEncriptedGradPairs(ServerContext* context,
                                               const GradPairsRequest* request,
                                               GradPairsResponse* response) {
  GetEncriptedData(grad_pair, mpz_t, {
    encripted_grad_pair->set__mp_alloc(grad_pairs[i]->_mp_alloc);
    encripted_grad_pair->set__mp_size(grad_pairs[i]->_mp_size);
    auto mp = grad_pairs[i]->_mp_d;
    for (int j = 0; j < grad_pairs[i]->_mp_size; ++j) {
      auto t = encripted_grad_pair->mutable__mp_d()->Add();
      *t = mp[j];
    }
  });
}

Status XgbServiceServer::GetEncriptedSplits(ServerContext* context, const SplitsRequest* request,
                                            SplitsResponse* response) {
  GetEncriptedData(split, XgbEncriptedSplit, {
    encripted_split->set_mask_id(splits[i].mask_id);
    auto encripted_grad_pair_sum = encripted_split->mutable_encripted_grad_pair_sum();
    encripted_grad_pair_sum->set__mp_alloc(splits[i].encripted_grad_pair_sum->_mp_alloc);
    encripted_grad_pair_sum->set__mp_size(splits[i].encripted_grad_pair_sum->_mp_size);
    auto mp = splits[i].encripted_grad_pair_sum->_mp_d;
    for (int j = 0; j < splits[i].encripted_grad_pair_sum->_mp_size; ++j) {
      auto t = encripted_grad_pair_sum->mutable__mp_d()->Add();
      *t = mp[j];
    }
  });
}

//=================================XgbServiceServer End===================================
