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

void XgbServiceClient::GetPubKey(opt_public_key_t** pub) {
  *pub = (opt_public_key_t*)malloc(sizeof(opt_public_key_t));
  RpcRequest(Request, GetPubKey, PubKeyResponse, , {
    (*pub)->nbits = response.nbits();
    (*pub)->lbits = response.lbits();
    mpz_type2_mpz_t((*pub)->n, response.n());
    mpz_type2_mpz_t((*pub)->half_n, response.half_n());
    mpz_type2_mpz_t((*pub)->n_squared, response.n_squared());
    mpz_type2_mpz_t((*pub)->h_s, response.h_s());
    mpz_type2_mpz_t((*pub)->P_squared_mul_P_squared_inverse,
                    response.p_squared_mul_p_squared_inverse());
    auto mod_p_sqaured = response.fb_mod_p_sqaured();
    mpz_type2_mpz_t((*pub)->fb_mod_P_sqaured.m_mod, mod_p_sqaured.m_mod());
    (*pub)->fb_mod_P_sqaured.m_table_G = new mpz_t[mod_p_sqaured.m_t() + 1];
    for (int i = 0; i <= mod_p_sqaured.m_t(); ++i) {
      mpz_type2_mpz_t((*pub)->fb_mod_P_sqaured.m_table_G[i], mod_p_sqaured.m_table_g(i));
    }
    (*pub)->fb_mod_P_sqaured.m_h = mod_p_sqaured.m_h();
    (*pub)->fb_mod_P_sqaured.m_t = mod_p_sqaured.m_t();
    (*pub)->fb_mod_P_sqaured.m_w = mod_p_sqaured.m_w();
    auto mod_q_sqaured = response.fb_mod_q_sqaured();
    mpz_type2_mpz_t((*pub)->fb_mod_Q_sqaured.m_mod, mod_q_sqaured.m_mod());
    (*pub)->fb_mod_Q_sqaured.m_table_G = new mpz_t[mod_q_sqaured.m_t() + 1];
    for (int i = 0; i <= mod_q_sqaured.m_t(); ++i) {
      mpz_type2_mpz_t((*pub)->fb_mod_Q_sqaured.m_table_G[i], mod_q_sqaured.m_table_g(i));
    }
    (*pub)->fb_mod_Q_sqaured.m_h = mod_q_sqaured.m_h();
    (*pub)->fb_mod_Q_sqaured.m_t = mod_q_sqaured.m_t();
    (*pub)->fb_mod_Q_sqaured.m_w = mod_q_sqaured.m_w();
  });
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

void XgbServiceClient::SendEncryptedSplits(
    SplitsRequest& splits_request,
    function<void(xgboost::bst_feature_t, xgboost::bst_bin_t)> record_bin_id_fid) {
  RpcRequest_(splits_request, SendEncryptedSplits, SplitsResponse, {
    if (splits_request.encrypted_splits().empty() && !response.mask_id().empty()) {
      // TODO: decrypt the feature id and bin id from mask id
      vector<string> ids;
      boost::split(ids, response.mask_id(), boost::is_any_of("_"));
      record_bin_id_fid(atoi(ids[0].c_str()), atoi(ids[1].c_str()));
    }
  });
}

void XgbServiceClient::GetEncryptedSplits(XgbEncryptedSplit* encryptedSplits) {
  RpcRequest(SplitsRequest, SendEncryptedSplits, SplitsResponse, request.set_version(cur_version),
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
//=================================XgbServiceClient End===================================