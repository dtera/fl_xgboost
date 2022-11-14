//
// Created by HqZhao on 2022/11/14.
//
#include <gtest/gtest.h>
#include <helib/helib.h>

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

TEST(demo, paillier) {
  PublicKey pk;
  PrivateKey sk;
  generatePaillierKeys1(&pk, &sk, 1024);

  BatchPaillierPublicKey bpk(pk, 1, 1);
  mpz_t *plains = new mpz_t[1];
  mpz_t *ciphers = new mpz_t[1];
  mpz_set_d(*plains, 11233243);
  cout << mpz_get_d(plains[0]) << endl;
  bpk.encrypt(ciphers, plains, 1);
  mpz_t *dec_txts = new mpz_t[1];
  batchDecrypt(dec_txts, ciphers, 1, sk);
  cout << mpz_get_d(dec_txts[0]) << endl;
}
