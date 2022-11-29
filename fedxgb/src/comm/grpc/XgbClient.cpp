//
// Created by HqZhao on 2022/11/23.
//

#include "comm/grpc/XgbClient.h"

XgbServiceAsyncClient::XgbServiceAsyncClient(const uint32_t port, const string& host)
    : stub_(XgbService::NewStub(
          grpc::CreateChannel(host + ":" + to_string(port), grpc::InsecureChannelCredentials()))) {
  grad_thread_.reset(new thread(bind(&XgbServiceAsyncClient::GradThread, this)));
  splits_thread_.reset(new thread(bind(&XgbServiceAsyncClient::SplitsThread, this)));
  grad_stream_ = stub_->AsyncGetEncriptedGradPairs_(
      &grad_context_, &grad_cq_, reinterpret_cast<void*>(XgbCommType::GRAD_CONNECT));
  splits_stream_ = stub_->AsyncGetEncriptedSplits_(
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
    splitsRequest.set_version(version);
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
XgbServiceClient::XgbServiceClient(const uint32_t port, const string& host, int32_t n_threads)
    : n_threads_(n_threads) {
  channel_args_.SetMaxReceiveMessageSize(-1);
  channel_args_.SetMaxReceiveMessageSize(-1);
  stub_ = XgbService::NewStub(grpc::CreateCustomChannel(
      host + ":" + to_string(port), grpc::InsecureChannelCredentials(), channel_args_));
}

#define GetRpcRes(ReqType, processResStatments)                                               \
  ReqType##Request request;                                                                   \
  ReqType##Response response;                                                                 \
  request.set_version(version);                                                               \
                                                                                              \
  ClientContext context;                                                                      \
  auto status = stub_->GetEncripted##ReqType(&context, request, &response);                   \
  if (status.ok()) {                                                                          \
    DEBUG << "** RPC request success! " << endl;                                              \
    processResStatments                                                                       \
  } else {                                                                                    \
    cerr << "code: " << status.error_code() << ", error: " << status.error_message() << endl; \
  }

void XgbServiceClient::GetEncriptedGradPairs(const uint32_t& version, mpz_t* encriptedGradPairs) {
  // mutex mtx;
  GetRpcRes(GradPairs, {
    auto egps = response.encripted_grad_pairs();
    // encriptedGradPairs = new mpz_t[egps.size()];
    xgboost::common::ParallelFor(egps.size(), n_threads_, [&](int i) {
      encriptedGradPairs[i]->_mp_alloc = egps[i]._mp_alloc();
      encriptedGradPairs[i]->_mp_size = egps[i]._mp_size();
      encriptedGradPairs[i]->_mp_d = new mp_limb_t[egps[i]._mp_d().size()];
      for (int j = 0; j < egps[i]._mp_d().size(); ++j) {
        encriptedGradPairs[i]->_mp_d[j] = egps[i]._mp_d()[j];
      }
    });
  });
  DEBUG << "response.version: " << response.version() << endl;
  DEBUG << "gradPairs[0]._mp_alloc: " << encriptedGradPairs[0]->_mp_alloc << endl;
  DEBUG << "gradPairs[0]._mp_size: " << encriptedGradPairs[0]->_mp_size << endl;
  DEBUG << "gradPairs[0]._mp_d: " << *encriptedGradPairs[0]->_mp_d << endl;
}

void XgbServiceClient::GetEncriptedSplits(const uint32_t& version,
                                          XgbEncriptedSplit* encriptedSplits) {
  GetRpcRes(Splits, {
    auto ess = response.encripted_splits();
    // encriptedSplits = make_shared<XgbEncriptedSplit*>(new XgbEncriptedSplit[ess.size()]);
    xgboost::common::ParallelFor(ess.size(), n_threads_, [&](int i) {
      encriptedSplits[i].mask_id = ess[i].mask_id();

      auto t = ess[i].encripted_grad_pair_sum();
      encriptedSplits[i].encripted_grad_pair_sum->_mp_alloc = t._mp_alloc();
      encriptedSplits[i].encripted_grad_pair_sum->_mp_size = t._mp_size();
      auto mp_d = t._mp_d();
      encriptedSplits[i].encripted_grad_pair_sum->_mp_d = new mp_limb_t[mp_d.size()];
      for (int j = 0; j < mp_d.size(); ++j) {
        encriptedSplits[i].encripted_grad_pair_sum->_mp_d[j] = mp_d[j];
      }
    });
  });
}
//=================================XgbServiceClient End===================================