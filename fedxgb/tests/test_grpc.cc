//
// Created by HqZhao on 2022/11/14.
//
#include <gtest/gtest.h>

#include <random>

#include "comm/grpc/GRPCComm.hpp"
#include "comm/grpc/XgbServiceRegistry.h"
#include "opt_paillier.h"

using namespace std;

uint32_t len_ = 1000;

mpz_t *mpz_ciphers_ = new mpz_t[len_];
mpz_t *mpz_res_ = new mpz_t[len_];
float *plains_f_ = new float[len_];
float *res_f_ = new float[len_];
mpz_t *res_encrypted_grad_pairs_ = new mpz_t[len_];
XgbEncryptedSplit *encrypted_splits_ = new XgbEncryptedSplit[len_];
XgbEncryptedSplit *res_encrypted_splits_ = new XgbEncryptedSplit[len_];
vector<xgboost::EncryptedGradientPair> encrypted_grad_pairs;
vector<xgboost::EncryptedGradientPair> res_encrypted_grad_pairs;
vector<xgboost::GradientPair> res_grad_pairs;

uniform_int_distribution<long long> u_(-100000, 100000);
default_random_engine e_;

opt_public_key_t *pub_;
opt_private_key_t *pri_;
uint32_t bitLength_ = 1024;

TEST(grpc, xgb_server) {
  std::unique_ptr<XgbServiceServer> server = FIND_XGB_SERVICE(XgbServiceServer);
  server->Start();
  cout << "XgbServiceServer Running..." << endl;
  TIME_STAT(opt_paillier_keygen(&pub_, &pri_, bitLength_), KeyGen)
  EncryptedType<>::pub = pub_;
  repeat(
      [&](int i) {
        mpz_init(mpz_ciphers_[i]);
        auto t = u_(e_);
        plains_f_[i] = 1.0 * t / 1000;
        encrypted_splits_[i].mask_id = to_string(t);
      },
      len_);

  opt_paillier_batch_encrypt_t(mpz_ciphers_, plains_f_, len_, pub_);
  encrypted_grad_pairs.resize(len_);
  res_encrypted_grad_pairs.resize(len_);
  res_grad_pairs.resize(len_);
  repeat(
      [&](int i) {
        mpz_set(encrypted_splits_[i].encrypted_grad_pair_sum.grad, mpz_ciphers_[i]);
        encrypted_grad_pairs[i].Add(EncryptedType(mpz_ciphers_[i]), EncryptedType(mpz_ciphers_[i]));
        encrypted_grad_pairs[i].SetGrad(EncryptedType(mpz_ciphers_[i]));
        encrypted_grad_pairs[i].SetHess(EncryptedType(mpz_ciphers_[i]));
      },
      len_);
  server->SendGradPairs(encrypted_grad_pairs);
  server->SendSplits(encrypted_splits_, len_);
  server->SendPubKey(pub_);
  cout << *pub_ << endl;
  // sleep(30000);

  std::unique_ptr<XgbServiceClient> client = FIND_XGB_SERVICE(XgbServiceClient);
  client->Start();
  for (int i = 1; i < 2; ++i) {
    client->GetEncryptedGradPairs(i, res_encrypted_grad_pairs);
    client->GetEncryptedSplits(i, res_encrypted_splits_);
    opt_paillier_batch_decrypt(res_grad_pairs, res_encrypted_grad_pairs, pub_, pri_);
    for (int j = 0; j < len_; ++j) {
      char *c1, *c2, *c3, *c4;
      opt_paillier_get_plaintext(c1, mpz_ciphers_[j], pub_);
      opt_paillier_get_plaintext(c2, res_encrypted_grad_pairs[j].GetGrad().data_, pub_);
      cout << "\nmpz_ciphers_[" << j << "]: " << c1 << endl;
      cout << "res_grad_pairs_[" << j << "]: " << c2 << endl;
      cout << "plains_f_[" << j << "]: " << plains_f_[j] << endl;
      cout << "res_f_[" << j << "]: " << res_grad_pairs[j] << endl;
      assert(abs(plains_f_[j] - res_grad_pairs[j].GetGrad()) < 0.000001);
      cout << "==========================================================" << endl;
      opt_paillier_get_plaintext(c3, encrypted_splits_[j].encrypted_grad_pair_sum.grad, pub_);
      opt_paillier_get_plaintext(c4, res_encrypted_splits_[j].encrypted_grad_pair_sum.grad, pub_);
      cout << "encrypted_splits_[" << j << "]: " << c3 << endl;
      cout << "res_encrypted_splits_[" << j << "]: " << c4 << endl;
    }
  }
  server->Shutdown();
}

TEST(grpc, xgb_client) {
  XgbServiceClient client;
  opt_public_key_t *pub;
  client.GetPubKey(&pub);
  cout << *pub << endl;
}

TEST(grpc, xgb_async_server) {
  XgbServiceAsyncServer server;
  while (server.IsRunning()) {
  }
  // sleep(1);
  server.Stop();
}

TEST(grpc, xgb_async_client) {
  XgbServiceAsyncClient client;
  for (int i = 1; i < 100; ++i) {
    client.AsyncReq(i);
  }
  client.Stop();
}

TEST(grpc, server) {
  GRPCServer<float> grpcServer;
  Message<float> message;
  message.msg_type = MessageType::BestSplit;
  message.content = 123;
  grpcServer.send(message);
  sleep(1);
  grpcServer.close();
}

TEST(grpc, client) {
  GRPCClient grpcClient;
  Message<float> *message = nullptr;
  grpcClient.AsyncSendMessage(MsgType::BestSplit);
  grpcClient.receive<float>(message, [&](float &t, const grpccomm::ListValue &listValue) {
    cout << "listValue: " << listValue.SerializeAsString() << endl;
    for (int i = 0; i < listValue.list_value_size(); ++i) {
      cout << listValue.list_value(i).scala_msg().float_value() << endl;
    }
  });
  sleep(1);
  grpcClient.close();
}
