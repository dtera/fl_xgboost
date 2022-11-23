//
// Created by HqZhao on 2022/11/23.
//

#include "XgbServiceClient.h"

XgbServiceClient::XgbServiceClient(const uint32_t port, const string& host)
    : stub_(XgbService::NewStub(
          grpc::CreateChannel(host + ":" + to_string(port), grpc::InsecureChannelCredentials()))) {
  grpc_thread_.reset(new thread(bind(&XgbServiceClient::GrpcThread, this)));
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

void XgbServiceClient::GrpcThread() {
  while (true) {
    void* got_tag;
    bool ok = false;
    if (!cq_.Next(&got_tag, &ok)) {
      cerr << "Client stream closed." << endl;
      break;
    }
    if (ok) {
      DEBUG << endl << "**** Processing completion queue tag " << got_tag << endl;
      switch (static_cast<XgbCommType>(reinterpret_cast<long>(got_tag))) {
        case XgbCommType::GRAD_CONNECT:
        case XgbCommType::SPLITS_CONNECT:
          DEBUG << "Server connected." << endl;
          break;
        case XgbCommType::GRAD_READ:
        case XgbCommType::SPLITS_READ:
          DEBUG << "Read a new message." << endl;
          break;
        case XgbCommType::GRAD_WRITE:
          DEBUG << "Sending message(async)." << endl;
          AsyncRequestNextMessage(XgbCommType::GRAD_READ);
          break;
        case XgbCommType::SPLITS_WRITE:
          DEBUG << "Sending message(async)." << endl;
          AsyncRequestNextMessage(XgbCommType::SPLITS_READ);
          break;
        case XgbCommType::DONE:
          DEBUG << "Server disconnecting." << endl;
          break;
        case XgbCommType::FINISH:
          DEBUG << "Client finish; status = " << (finish_status_.ok() ? "ok" : "cancelled") << endl;
          context_.TryCancel();
          cq_.Shutdown();
          break;
        default:
          cerr << "Unexpected tag " << got_tag << endl;
          //GPR_ASSERT(false);
      }
    }
  }
}

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

void XgbServiceClient::Start(XgbCommType t) {
  switch (t) {
    case XgbCommType::GRAD_CONNECT:
      grad_stream_ = stub_->AsyncGetEncriptedGradPairs(
          &context_, &cq_, reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));
      break;
    case XgbCommType::SPLITS_CONNECT:
      splits_stream_ = stub_->AsyncGetSplits(&context_, &cq_,
                                             reinterpret_cast<void*>(XgbCommType::SPLITS_CONNECT));
      break;
    default:
      LOG(FATAL) << "Unexpected type: " << static_cast<int>(t) << endl;
      GPR_ASSERT(false);
  }
}

void XgbServiceClient::Stop() {
  DEBUG << "Shutting down client...." << endl;
  //grad_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
  //splits_stream_->WritesDone(reinterpret_cast<void*>(XgbCommType::DONE));
  grpc::Status status;
  cq_.Shutdown();
  grpc_thread_->join();
}
