//
// Created by HqZhao on 2022/11/14.
//
#include <common/timer.h>
#include <gtest/gtest.h>
#include <helib/helib.h>

#include <algorithm>
#include <iostream>
#include <random>
#include <string>

#include "opt_paillier.h"
#include "paillier.h"

using namespace std;
using namespace fl::he;

TEST(demo, hello) { cout << "Hello Test" << endl; }

TEST(demo, helib) {
  cout << "=========Homomorphic encryption Begin=======" << endl;
  helib::Context context =
      helib::ContextBuilder<helib::CKKS>().m(16 * 1024).bits(119).precision(20).c(2).build();
  cout << "securityLevel=" << context.securityLevel() << "\n";
  // Get the number of slots, n.  Note that for CKKS, we always have n=m/4.
  long n = context.getNSlots();

  helib::SecKey secretKey(context);
  secretKey.GenSecKey();
  const helib::PubKey &publicKey = secretKey;

  vector<double> v0(n);
  for (long i = 0; i < n; i++) v0[i] = sin(2.0 * helib::PI * i / n);

  helib::PtxtArray p0(context, v0);
  helib::Ctxt c0(publicKey);
  p0.encrypt(c0);

  helib::PtxtArray p1(context);
  p1.random();
  helib::Ctxt c1(publicKey);
  p1.encrypt(c1);

  helib::PtxtArray p2(context);
  p2.random();
  helib::Ctxt c2(publicKey);
  p2.encrypt(c2);

  // Now we homorphically compute c3 = c0*c1 + c2*1.5:
  helib::Ctxt c3 = c0;
  c3 *= c1;
  helib::Ctxt c4 = c2;
  c4 *= 1.5;
  c3 += c4;

  // When this is done, if we denote the i-th slot of a ciphertext c by c[i],
  // then we have c3[i] = c0[i]*c1[i] + c2[i]*1.5 for i = 0..n-1.
  helib::PtxtArray pp3(context);
  pp3.decrypt(c3, secretKey);

  vector<double> v3;
  pp3.store(v3);

  helib::PtxtArray p3 = p0;
  p3 *= p1;
  helib::PtxtArray p4 = p2;
  p4 *= 1.5;
  p3 += p4;

  double distance = Distance(p3, pp3);
  cout << "distance=" << distance << "\n";
  cout << "=========Homomorphic encryption End=========" << endl;
}

uint32_t len = 1000;
mpz_t *plains = new mpz_t[len];
mpz_t *ciphers = new mpz_t[len];
mpz_t *res = new mpz_t[len];
default_random_engine e;
uniform_int_distribution<long long> u(-100, 1000);
xgboost::common::Monitor monitor_;

opt_public_key_t *pub;
opt_private_key_t *pri;
uint32_t bitLength = 1024;

void init(std::function<void(int)> fn) {
  srandom(0);
  for (int i = 0; i < len; ++i) {
    fn(i);
  }
}

void out(std::function<void(int)> fn) {
  // cout << "====================================" << endl;
  for (int i = 0; i < len; ++i) {
    fn(i);
  }
  //  delete[] plains;
  //  delete[] ciphers;
  //  delete[] res;
}

void batchEnc() {
  PublicKey pk;
  PrivateKey sk;
  TIME_STAT(generatePaillierKeys(&pk, &sk, bitLength); PaillierBatchEncryptor bpk(pk, 1, 1);
            , KeyGen)

  init([&](int i) {
    mpz_set_ui(plains[i], u(e));
    // cout << mpz_get_ui(plains[i]) << endl;
  });

  bpk.encrypt(ciphers, plains, len);
  batchDecrypt(res, ciphers, len, sk);
  out([&](int i) {
    /*cout << endl << "Plaintext = " << mpz_get_ui(plains[i]) << endl;
    cout << "Ciphertext = " << mpz_get_ui(ciphers[i]) << endl;
    cout << "Result = " << mpz_get_ui(res[i]) << endl;*/
    assert(mpz_get_ui(res[i]) == mpz_get_ui(plains[i]));
  });

  /*paillierAdd(res[0], ciphers[0], ciphers[1], &pk);
  cout << "===============================================" << endl;
  auto t1 = mpz_get_ui(plains[0]);
  auto t2 = mpz_get_ui(plains[1]);
  cout << "t1: " << t1 << endl;
  cout << "t2: " << t2 << endl;
  auto t = t1 + t2;
  cout << "t1 + t2: " << t << endl;
  batchDecrypt(res, res, 1, sk, 1);
  cout << "res: " << mpz_get_ui(res[0]) << endl;
  assert(mpz_get_ui(res[0]) == t);*/
}

TEST(demo, paillier) {
  for (int i = 0; i < 1; ++i) {
    batchEnc();
  }
}

TEST(demo, opt_paillier) {
  TIME_STAT(opt_paillier_keygen(&pub, &pri, bitLength), KeyGen)

  init([&](int i) {
    string op1 = to_string(u(e));
    opt_paillier_set_plaintext(plains[i], op1.c_str(), pub);
  });

  opt_paillier_batch_encrypt(ciphers, plains, len, pub, pri);
  opt_paillier_batch_decrypt(res, ciphers, len, pub, pri);

  out([&](int i) {
    char *p, *o;
    opt_paillier_get_plaintext(p, plains[i], pub);
    opt_paillier_get_plaintext(o, res[i], pub);
    /*// printf("Plaintext0 = %s\n", mpz_get_str(nullptr, 10, plains[i]));
    // printf("Result0 = %s\n", mpz_get_str(nullptr, 10, res[i]));
    printf("\nPlaintext = %s\n", p);
    cout << "Ciphertext = " << mpz_get_ui(ciphers[i]) << endl;
    printf("Result = %s\n", o);*/

    assert(0 == mpz_cmp(res[i], plains[i]));
  });

  mpz_t out;
  mpz_init(out);
  for (int i = 0; i < len / 2; ++i) {
    opt_paillier_add(out, ciphers[i], ciphers[i + (len / 2)], pub);
    cout << "===============================================" << endl;
    char *o;
    opt_paillier_get_plaintext(o, plains[i], pub);
    auto t1 = atoi(o);
    opt_paillier_get_plaintext(o, plains[i + (len / 2)], pub);
    auto t2 = atoi(o);
    cout << "t1: " << t1 << endl;
    cout << "t2: " << t2 << endl;
    auto t = t1 + t2;
    cout << "t1 + t2: " << t << endl;

    opt_paillier_decrypt(out, out, pub, pri);
    opt_paillier_get_plaintext(o, out, pub);
    cout << "out: " << o << endl;

    assert(atoi(o) == t);
  }

  opt_paillier_freepubkey(pub);
  opt_paillier_freeprikey(pri);
}

TEST(demo, opt_paillier_op) {
  opt_paillier_keygen(&pub, &pri, bitLength);

  int round = 1000;
  mpz_t plain_test1;
  mpz_t plain_test2;
  mpz_t cipher_test1;
  mpz_t cipher_test2;
  mpz_t decrypt_test;
  mpz_init(plain_test1);
  mpz_init(plain_test2);
  mpz_init(cipher_test1);
  mpz_init(cipher_test2);
  mpz_init(decrypt_test);

  for (int i = 1; i <= round; ++i) {
    cout << "==============================================================" << endl;
    string op1 = to_string(u(e));
    opt_paillier_set_plaintext(plain_test1, op1.c_str(), pub);
    opt_paillier_encrypt(cipher_test1, plain_test1, pub, pri);
    printf("Text1 = %s\n", op1.c_str());
    gmp_printf("Ciphertext = %Zd\n", cipher_test1);

    string op2 = to_string(u(e));
    opt_paillier_set_plaintext(plain_test2, op2.c_str(), pub);
    opt_paillier_encrypt(cipher_test2, plain_test2, pub, pri);

    // opt_paillier_constant_mul(cipher_test, cipher_test, plain_test2, pub);
    opt_paillier_add(cipher_test1, cipher_test1, cipher_test2, pub);

    mpz_add(plain_test1, plain_test1, plain_test2);
    mpz_mod(plain_test1, plain_test1, pub->n);

    opt_paillier_decrypt(decrypt_test, cipher_test1, pub, pri);

    printf("Text2 = %s\n", op2.c_str());
    char *out;
    opt_paillier_get_plaintext(out, decrypt_test, pub);
    printf("Plaintext = %s\n", out);

    assert(0 == mpz_cmp(decrypt_test, plain_test1));
  }
  mpz_clears(decrypt_test, cipher_test1, cipher_test2, plain_test1, plain_test2, nullptr);
  opt_paillier_freepubkey(pub);
  opt_paillier_freeprikey(pri);
}
