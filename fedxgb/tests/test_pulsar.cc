//
// Created by HqZhao on 2023/04/18.
//

#include <gtest/gtest.h>

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
    pulsarClient.Send<mpz_t, xgbcomm::MpzType>(
        "xxx_" + to_string(i), mpz_temp,
        [&](xgbcomm::MpzType *m, const mpz_t &data) { mpz_t2_mpz_type(m, data); });
  }
}

TEST(pulsar, receive) {
  PulsarClient pulsarClient;
  mpz_t mpz_res;
  mpz_inits(mpz_res);

  for (int i = 0; i < n; ++i) {
    pulsarClient.Receive<mpz_t, xgbcomm::MpzType>(
        "xxx_" + to_string(i), mpz_res,
        [&](mpz_t &data, const xgbcomm::MpzType &m) { mpz_type2_mpz_t(data, m); });
    cout << endl << "mpz_res: " << mpz_get_d(mpz_res) << endl;
    cout << "mpz_res._mp_size: " << mpz_res->_mp_size << endl;
  }
}

TEST(pulsar, BatchSend) {
  PulsarClient pulsarClient;
  vector<int> grad_pairs;
  for (int i = 0; i < n; ++i) {
    grad_pairs.emplace_back(i);
  }
  pulsarClient.BatchSend<int, xgbcomm::Request>(
      "grad_pair12", grad_pairs, [&](xgbcomm::Request *m, const int &data) { m->set_idx(data); },
      true);
}

TEST(pulsar, BatchReceive) {
  PulsarClient pulsarClient;
  vector<int> grad_pairs;
  grad_pairs.resize(n);

  pulsarClient.BatchReceive<int, xgbcomm::Request>(
      "grad_pair12", grad_pairs, [&](int &data, const xgbcomm::Request &m) { data = m.idx(); },
      true);

  for (int i = 0; i < n; ++i) {
    if (i % 1000 == 0) {
      cout << "grad_pair: " << grad_pairs[i] << endl;
    }
  }
}

TEST(pulsar, test) {
  char buff[13];
  time_t now = time(NULL);
  strftime(buff, 13, "%Y%m%d%H%M", localtime(&now));
  cout << std::string(buff).substr(0, 11) << endl;
}