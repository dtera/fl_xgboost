//
// Created by HqZhao on 2022/11/23.
//

#include "XgbServiceServer.h"

XgbServiceServer::XgbServiceServer(const uint32_t port, const string& host)
    : server_address_(host + ":" + to_string(port)) {}

void XgbServiceServer::AsyncWaitForRequest(XgbCommType t) {
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
void XgbServiceServer::AsyncSendResponse(XgbCommType t) {
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

void XgbServiceServer::setGradPairsResponse(GradPairsResponse& gradPairsResponse) {
  cout << "grad_request_: " << grad_request_.SerializeAsString() << endl;
}

void XgbServiceServer::setSplitsResponse(SplitsResponse& splitsResponse) {
  cout << "splits_request_: " << splits_request_.SerializeAsString() << endl;
}

void XgbServiceServer::GrpcThread() {
  while (true) {
    void* tag = nullptr;
    bool ok = false;
    if (!cq_->Next(&tag, &ok)) {
      break;
    }
    if (ok) {
      DEBUG << endl << "**** Processing completion queue tag " << tag << endl;
      switch (static_cast<XgbCommType>(reinterpret_cast<size_t>(tag))) {
        case XgbCommType::GRAD_CONNECT:
          DEBUG << "Grad Service connected." << endl;
          AsyncWaitForRequest(XgbCommType::GRAD_READ);
          break;
        case XgbCommType::GRAD_READ:
          DEBUG << "Read grad pairs." << endl;
          AsyncSendResponse(XgbCommType::GRAD_WRITE);
          break;
        case XgbCommType::GRAD_WRITE:
          DEBUG << "Sending grad pairs(async)." << endl;
          AsyncWaitForRequest(XgbCommType::GRAD_READ);
          break;
        case XgbCommType::SPLITS_CONNECT:
          DEBUG << "Splits Service connected." << endl;
          AsyncWaitForRequest(XgbCommType::SPLITS_READ);
          break;
        case XgbCommType::SPLITS_READ:
          DEBUG << "Read splits." << endl;
          AsyncSendResponse(XgbCommType::SPLITS_WRITE);
          break;
        case XgbCommType::SPLITS_WRITE:
          DEBUG << "Sending splits(async)." << endl;
          AsyncWaitForRequest(XgbCommType::SPLITS_READ);
          break;
        case XgbCommType::DONE:
          DEBUG << "Server disconnecting." << endl;
          is_running_ = false;
          break;
        case XgbCommType::FINISH:
          DEBUG << "Server quit." << endl;
          break;
        default:
          cerr << "Unexpected tag." << tag << endl;
          //GPR_ASSERT(false);
      }
    }
  }
}

bool XgbServiceServer::IsRunning() { return is_running_; }

void XgbServiceServer::Start() {
  ServerBuilder builder;
  builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
  builder.RegisterService(&service_);
  cq_ = builder.AddCompletionQueue();
  server_ = builder.BuildAndStart();

  grad_stream_.reset(new ServerAsyncReaderWriter<GradPairsResponse, GradPairsRequest>(&context_));
  service_.RequestGetEncriptedGradPairs(&context_, grad_stream_.get(), cq_.get(), cq_.get(),
                                        reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));

  splits_stream_.reset(new ServerAsyncReaderWriter<SplitsResponse, SplitsRequest>(&context_));
  service_.RequestGetSplits(&context_, splits_stream_.get(), cq_.get(), cq_.get(),
                            reinterpret_cast<void*>(XgbCommType::SPLITS_CONNECT));

  // This is important as the server should know when the client is done.
  context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(XgbCommType::DONE));
  grpc_thread_.reset(new thread((bind(&XgbServiceServer::GrpcThread, this))));

  cout << "Server listening on " << server_address_ << endl;
}

void XgbServiceServer::Stop() {
  is_running_ = false;
  cout << "Shutting down server...." << endl;
  server_->Shutdown();
  cq_->Shutdown();
  grpc_thread_->join();
}
