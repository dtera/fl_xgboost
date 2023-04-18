//
// Created by HqZhao on 2023/04/18.
//

#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>
#include <iostream>
#include <string>
#include <vector>

#include "comm/pulsar/PulsarClient.hpp"

using namespace std;

int n = 100000;

TEST(pulsar, send) {
  PulsarClient pulsarClient;
  mpz_t mpz_temp, mpz_res;
  mpz_inits(mpz_temp, mpz_res);
  for (int i = 0; i < n; ++i) {
    mpz_set_d(mpz_temp, 100 + i);
    cout << "mpz_temp: " << mpz_get_d(mpz_temp) << endl;
    cout << "mpz_temp._mp_size: " << mpz_temp->_mp_size << endl;
    pulsarClient.send<mpz_t, xgbcomm::MpzType>(
        "xxx_" + to_string(i), mpz_temp,
        [&](xgbcomm::MpzType *m, const mpz_t &data) { mpz_t2_mpz_type(m, data); });
  }
}

TEST(pulsar, receive) {
  PulsarClient pulsarClient;
  mpz_t mpz_res;
  mpz_inits(mpz_res);

  for (int i = 0; i < n; ++i) {
    pulsarClient.receive<mpz_t, xgbcomm::MpzType>(
        "xxx_" + to_string(i), mpz_res,
        [&](mpz_t &data, const xgbcomm::MpzType &m) { mpz_type2_mpz_t(data, m); });
    cout << endl << "mpz_res: " << mpz_get_d(mpz_res) << endl;
    cout << "mpz_res._mp_size: " << mpz_res->_mp_size << endl;
  }
}

TEST(pulsar, sendGradPairs) {
  PulsarClient pulsarClient;
  vector<GradPair> grad_pairs;
  for (int i = 0; i < n; ++i) {
    GradPair p;
    // mpz_inits(p.grad, p.hess);
    grad_pairs.emplace_back(p);
  }
  pulsarClient.send<vector<GradPair>, xgbcomm::GradPairsResponse>(
      "grad_pair", grad_pairs, [&](xgbcomm::GradPairsResponse *m, const vector<GradPair> &data) {
        for (auto grad_pair : grad_pairs) {
          cout << "grad_pair: " << grad_pair << endl;
          auto encrypted_grad_pair = m->mutable_encrypted_grad_pairs()->Add();
          mpz_t2_mpz_type(encrypted_grad_pair, grad_pair);
        }
      });
}

TEST(pulsar, receiveGradPairs) {
  PulsarClient pulsarClient;
  vector<GradPair> grad_pairs;

  pulsarClient.receive<vector<GradPair>, xgbcomm::GradPairsResponse>(
      "grad_pair", grad_pairs, [&](vector<GradPair> &data, const xgbcomm::GradPairsResponse &m) {
        for (auto encrypted_grad_pair : m.encrypted_grad_pairs()) {
          GradPair p;
          mpz_type2_mpz_t(p, encrypted_grad_pair);
          data.emplace_back(p);
        }
      });

  for (auto grad_pair : grad_pairs) {
    cout << "grad_pair: " << grad_pair << endl;
  }
}
