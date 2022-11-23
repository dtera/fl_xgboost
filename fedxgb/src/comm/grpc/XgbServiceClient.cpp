//
// Created by HqZhao on 2022/11/23.
//

#include "XgbServiceClient.h"

XgbServiceClient::XgbServiceClient(const uint32_t port, const string& host)
    : stub_(XgbService::NewStub(
          grpc::CreateChannel(host + ":" + to_string(port), grpc::InsecureChannelCredentials()))) {
  grad_thread_.reset(new thread(bind(&XgbServiceClient::GradThread, this)));
  splits_thread_.reset(new thread(bind(&XgbServiceClient::SplitsThread, this)));
  grad_stream_ = stub_->AsyncGetEncriptedGradPairs(
      &grad_context_, &grad_cq_, reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));
  splits_stream_ = stub_->AsyncGetSplits(&splits_context_, &splits_cq_,
                                         reinterpret_cast<void*>(XgbCommType::SPLITS_CONNECT));
}

void XgbServiceClient::AsyncRequestNextMessage(XgbCommType t) {
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

void XgbServiceClient::GradThread() { GrpcThread(grad, GRAD) }

void XgbServiceClient::SplitsThread() { GrpcThread(splits, SPLITS) }

bool XgbServiceClient::AsyncReq(const uint32_t version, XgbCommType t) {
  cout << "** Sending request: " << version << endl;
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
    splitsRequest.set_version(version);
    splits_stream_->Write(splitsRequest, reinterpret_cast<void*>(XgbCommType::SPLITS_WRITE));
  } else {
    LOG(FATAL) << "Unexpected type: " << static_cast<int>(t) << endl;
    GPR_ASSERT(false);
  }

  return true;
}

void XgbServiceClient::Stop() {
  DEBUG << "Shutting down client...." << endl;
  // grad_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
  // splits_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
  grad_cq_.Shutdown();
  grad_thread_->join();
  splits_cq_.Shutdown();
  splits_thread_->join();
}
