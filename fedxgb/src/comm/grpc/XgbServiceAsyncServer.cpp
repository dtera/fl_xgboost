//
// Created by HqZhao on 2022/11/23.
//

#include "XgbServiceAsyncServer.h"

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
  service_.RequestGetEncriptedGradPairs(&grad_context_, grad_stream_.get(), grad_cq_.get(),
                                        grad_cq_.get(),
                                        reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));

  splits_stream_.reset(
      new ServerAsyncReaderWriter<SplitsResponse, SplitsRequest>(&splits_context_));
  service_.RequestGetSplits(&splits_context_, splits_stream_.get(), splits_cq_.get(),
                            splits_cq_.get(), reinterpret_cast<void*>(XgbCommType::SPLITS_CONNECT));

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
