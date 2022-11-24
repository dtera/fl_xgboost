//
// Created by HqZhao on 2022/11/14.
//
#include <gtest/gtest.h>

#include <random>

#include "comm/grpc/GRPCComm.hpp"
#include "comm/grpc/XgbClient.h"
#include "comm/grpc/XgbServer.h"
#include "opt_paillier.h"

using namespace std;

uint32_t len_ = 10000;

mpz_t *mpz_ciphers_ = new mpz_t[len_];
mpz_t *mpz_res_ = new mpz_t[len_];
float *plains_f_ = new float[len_];
float *res_f_ = new float[len_];

uniform_int_distribution<long long> u_(-100000, 100000);
default_random_engine e_;

opt_public_key_t *pub_;
opt_private_key_t *pri_;
uint32_t bitLength_ = 1024;

TEST(grpc, xgb_server) {
  XgbServiceServer server;
  cout << "XgbServiceServer Running..." << endl;
  cout << "Do other things..." << endl;
  TIME_STAT(opt_paillier_keygen(&pub_, &pri_, bitLength_), KeyGen)

  repeat(
      [&](int i) {
        mpz_init(mpz_ciphers_[i]);
        plains_f_[i] = 1.0 * u_(e_) / 1000;
      },
      len_);

  opt_paillier_batch_encrypt_t(mpz_ciphers_, plains_f_, len_, pub_, pri_);
  // opt_paillier_batch_decrypt_t(res_f, mpz_ciphers, len, pub, pri);

  server.SendGradPairs(1, mpz_ciphers_, len_);

  sleep(1);
  XgbServiceClient client;
  for (int i = 1; i < 2; ++i) {
    mpz_t *encriptedGradPairs = new mpz_t[len_];
    client.GetEncriptedGradPairs(i, encriptedGradPairs);
    opt_paillier_batch_decrypt_t(res_f_, encriptedGradPairs, len_, pub_, pri_);
    for (int j = 0; j < len_; ++j) {
      DEBUG << "mpz_ciphers_[" << j << "]._mp_alloc: " << mpz_ciphers_[j]->_mp_alloc << endl;
      DEBUG << "mpz_ciphers_[" << j << "]._mp_size: " << mpz_ciphers_[j]->_mp_size << endl;
      DEBUG << "mpz_ciphers_[" << j << "]._mp_d: " << *mpz_ciphers_[j]->_mp_d << endl;

      char *c1, *c2;
      opt_paillier_get_plaintext(c1, mpz_ciphers_[j], pub_);
      opt_paillier_get_plaintext(c2, encriptedGradPairs[j], pub_);
      cout << "mpz_ciphers_[" << j << "]: " << c1 << endl;
      cout << "encriptedGradPairs[" << i << "]: " << c2 << endl;
      cout << "plains_f_[" << j << "]: " << plains_f_[j] << endl;
      cout << "res_f_[" << j << "]: " << res_f_[j] << endl;
      assert(abs(plains_f_[j] - res_f_[j]) < 0.000001);
    }
  }
  sleep(5);
  server.Shutdown();
}

TEST(grpc, xgb_client) {
  XgbServiceClient client;
  for (int i = 1; i < 2; ++i) {
    mpz_t *encriptedGradPairs = nullptr;
    client.GetEncriptedGradPairs(i, encriptedGradPairs);
  }
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
