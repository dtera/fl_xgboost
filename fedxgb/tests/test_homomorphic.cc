//
// Created by HqZhao on 2022/11/14.
//
#include <common/timer.h>
#include <gtest/gtest.h>
//#include <helib/helib.h>

#include <algorithm>
#include <iostream>
#include <random>
#include <string>

#include "opt_paillier.h"
#include "paillier.h"

using namespace std;
using namespace angel::fl;

uint32_t len = 100;
mpz_t *mpz_plains = new mpz_t[len];
mpz_t *mpz_ciphers = new mpz_t[len];
mpz_t *mpz_res = new mpz_t[len];
char **plains = new char *[len];
char **res = new char *[len];
double *plains_d = new double[len];
double *res_d = new double[len];
mpz_t mpz_plain_test;
mpz_t mpz_cipher_test;
mpz_t mpz_decrypt_test;
mpz_t mpz_temp;

default_random_engine e;
uniform_int_distribution<long long> u(-100000, 100000);

opt_public_key_t *pub;
opt_private_key_t *pri;
uint32_t bitLength = 1024;

void for_out(std::function<void(int)> fn, size_t n = len) {
  repeat(
      fn, len,
      []() {
        srandom(0);
        mpz_inits(mpz_temp, mpz_plain_test, mpz_cipher_test, mpz_decrypt_test, nullptr);
        // cout << "=============================================================" << endl;
      },
      []() { mpz_clears(mpz_temp, mpz_plain_test, mpz_cipher_test, mpz_decrypt_test, nullptr); });

  //  delete[] mpz_plains;
  //  delete[] mpz_ciphers;
  //  delete[] res;
}

TEST(homomorphic, test) {
  cout << "is_same: " << is_same<unsigned int, uint32_t>() << endl;
  float a = 123;
  cout << "to_string: " << to_string(a).c_str() << endl;
  mpz_init(mpz_temp);
  mpz_set_d(mpz_temp, -123.21);
  mpf_t ft;
  mpf_init(ft);
  mpf_set_d(ft, -123.21);
  cout << "mpz_temp: " << mpz_get_d(mpz_temp) << endl;
  cout << "mpz_temp._mp_size: " << mpz_temp->_mp_size << endl;
  cout << "ft: " << mpf_get_d(ft) << endl;
  cout << "pow: " << pow(10, 8) << endl;
  cout << "std::is_standard_layout<T>: " << is_standard_layout<EncryptedType<float>>::value << endl;
}

/*TEST(homomorphic, helib) {
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
}*/

TEST(homomorphic, paillier) {
  for (int i = 0; i < 1; ++i) {
    PublicKey pk;
    PrivateKey sk;
    TIME_STAT(generatePaillierKeys1(&pk, &sk, bitLength); BatchPaillierPublicKey bpk(pk, 1, 1);
              , KeyGen)

    for_out([&](int i) {
      mpz_set_ui(mpz_plains[i], u(e));
      // cout << mpz_get_ui(mpz_plains[i]) << endl;
    });

    bpk.encrypt(mpz_ciphers, mpz_plains, len);
    batchDecrypt(mpz_res, mpz_ciphers, len, sk);
    for_out([&](int i) {
      /*cout << endl << "Plaintext = " << mpz_get_ui(mpz_plains[i]) << endl;
      cout << "Ciphertext = " << mpz_get_ui(mpz_ciphers[i]) << endl;
      cout << "Result = " << mpz_get_ui(res[i]) << endl;*/
      assert(mpz_get_ui(mpz_res[i]) == mpz_get_ui(mpz_plains[i]));
    });

    /*paillierAdd(res[0], mpz_ciphers[0], mpz_ciphers[1], &pk);
    cout << "===============================================" << endl;
    auto t1 = mpz_get_ui(mpz_plains[0]);
    auto t2 = mpz_get_ui(mpz_plains[1]);
    cout << "t1: " << t1 << endl;
    cout << "t2: " << t2 << endl;
    auto t = t1 + t2;
    cout << "t1 + t2: " << t << endl;
    batchDecrypt(res, res, 1, sk, 1);
    cout << "res: " << mpz_get_ui(res[0]) << endl;
    assert(mpz_get_ui(res[0]) == t);*/
  }
}

TEST(homomorphic, opt_paillier) {
  TIME_STAT(opt_paillier_keygen(&pub, &pri, bitLength), KeyGen)

  for_out([&](int i) {
    auto ue = u(e);
    string t = to_string(ue);
    plains[i] = new char[32];
    res[i] = new char[32];
    mpz_init(mpz_ciphers[i]);
    move(t.begin(), t.end(), plains[i]);

    plains_d[i] = 1.0 * ue / 1000;
  });

  opt_paillier_batch_encrypt(mpz_ciphers, plains, len, pub);
  opt_paillier_batch_decrypt(res, mpz_ciphers, len, pub, pri);

  for_out([&](int i) {
    /*cout << "plains[" << i << "]: " << plains[i] << endl;
    cout << "res[" << i << "]: " << res[i] << endl;*/
    assert(0 == strcmp(plains[i], res[i]));
  });

  for (int i = 0; i < len / 2; ++i) {
    opt_paillier_add(mpz_temp, mpz_ciphers[i], mpz_ciphers[i + (len / 2)], pub);
    opt_paillier_decrypt(mpz_temp, mpz_temp, pub, pri);
    auto t1 = atoi(plains[i]);
    auto t2 = atoi(plains[i + (len / 2)]);
    cout << "=============================================================" << endl;
    cout << "t1: " << t1 << endl;
    cout << "t2: " << t2 << endl;
    auto t = t1 + t2;
    cout << "t1 + t2: " << t << endl;
    cout << "t1 - t2: " << t1 - t2 << endl;
    char *o;
    opt_paillier_get_plaintext(o, mpz_temp, pub);
    cout << "add out1: " << o << endl;
    assert(atoi(o) == t);

    opt_paillier_sub(mpz_temp, mpz_ciphers[i], mpz_ciphers[i + (len / 2)], pub);
    opt_paillier_decrypt(mpz_temp, mpz_temp, pub, pri);
    opt_paillier_get_plaintext(o, mpz_temp, pub);
    cout << "sub out2: " << o << endl;

    assert(atoi(o) == t1 - t2);
  }

  opt_paillier_batch_encrypt_t(mpz_ciphers, plains_d, len, pub);
  opt_paillier_batch_decrypt_t(res_d, mpz_ciphers, len, pub, pri);

  for_out([&](int i) {
    // cout << "plains_d[" << i << "]: " << plains_d[i] << endl;
    // cout << "res_d[" << i << "]: " << res_d[i] << endl;
    assert(abs(plains_d[i] - res_d[i]) < 0.000001);
  });

  size_t size = 10;
  opt_paillier_batch_add(mpz_temp, mpz_ciphers, size, pub);
  double d1, d2 = 0;
  opt_paillier_decrypt_t(d1, mpz_temp, pub, pri);
  for (int i = 0; i < size; ++i) {
    d2 += plains_d[i];
    cout << "plains_d[" << i << "]: " << plains_d[i] << endl;
  }
  cout << "d1: " << d1 << endl;
  cout << "d2: " << d2 << endl;

  opt_paillier_freepubkey(pub);
  opt_paillier_freeprikey(pri);
}

TEST(homomorphic, opt_paillier_data_pack) {
  TIME_STAT(opt_paillier_keygen(&pub, &pri, bitLength), KeyGen)

  CrtMod *crtmod;
  size_t data_size = 10;
  init_crt(&crtmod, data_size, mapTo_nbits_lbits[bitLength].second);
  char **nums;
  char **test;
  double *nums_d = new double[data_size];
  double *test_d = new double[data_size];
  nums = (char **)malloc(sizeof(char *) * data_size);
  for (int i = 0; i < len; ++i) {
    for (int j = 0; j < data_size; ++j) {
      nums[j] = new char[32];
      auto ue = u(e);
      auto t = to_string(ue);
      nums_d[j] = 1.0 * ue / 1000;
      move(t.begin(), t.end(), nums[j]);
    }
    data_packing_crt(mpz_temp, nums, data_size, crtmod);
    opt_paillier_encrypt(mpz_cipher_test, mpz_temp, pub, pri);
    opt_paillier_decrypt(mpz_decrypt_test, mpz_cipher_test, pub, pri);
    data_retrieve_crt(test, mpz_decrypt_test, data_size, crtmod, pub);

    char *o1, *o2;
    opt_paillier_get_plaintext(o1, mpz_temp, pub);
    cout << "====================================================" << endl;
    cout << "pack: " << o1 << endl;
    opt_paillier_get_plaintext(o2, mpz_decrypt_test, pub);
    cout << "decrypt_pack: " << o2 << endl;
    for (int j = 0; j < data_size; ++j) {
      cout << "nums[" << j << "]: " << nums[j] << endl;
      cout << "test[" << j << "]: " << test[j] << endl;
      assert(0 == strcmp(test[j], nums[j]));
    }

    data_packing_crt_t<double>(mpz_temp, nums_d, data_size, crtmod, pub);
    opt_paillier_encrypt(mpz_cipher_test, mpz_temp, pub, pri);
    opt_paillier_decrypt(mpz_decrypt_test, mpz_cipher_test, pub, pri);
    data_retrieve_crt_t<double>(test_d, mpz_decrypt_test, data_size, crtmod, pub);

    opt_paillier_get_plaintext(o1, mpz_temp, pub);
    cout << "====================================================" << endl;
    cout << "pack: " << o1 << endl;
    opt_paillier_get_plaintext(o2, mpz_decrypt_test, pub);
    cout << "decrypt_pack: " << o2 << endl;
    for (int j = 0; j < data_size; ++j) {
      cout << "nums_d[" << j << "]: " << nums_d[j] << endl;
      cout << "test_d[" << j << "]: " << test_d[j] << endl;
      assert(abs(test_d[j] - nums_d[j]) < 0.000001);
    }
  }
}

TEST(homomorphic, opt_paillier_op) {
  TIME_STAT(opt_paillier_keygen(&pub, &pri, bitLength), KeyGen)

  mpz_t plain_test1;
  mpz_t plain_test2;
  mpz_t cipher_test1;
  mpz_t cipher_test2;
  mpz_t decrypt_test;
  mpz_inits(plain_test1, plain_test2, cipher_test1, cipher_test2, decrypt_test, nullptr);

  for (int i = 1; i <= len; ++i) {
    cout << "==============================================================" << endl;
    string op1 = to_string(u(e));
    opt_paillier_set_plaintext(plain_test1, op1.c_str(), pub);
    opt_paillier_encrypt(cipher_test1, plain_test1, pub, pri);

    string op2 = to_string(u(e));
    opt_paillier_set_plaintext(plain_test2, op2.c_str(), pub);
    opt_paillier_encrypt(cipher_test2, plain_test2, pub, pri);

    // opt_paillier_constant_mul(cipher_test, cipher_test, plain_test2, pub);
    opt_paillier_add(cipher_test1, cipher_test1, cipher_test2, pub);

    mpz_add(plain_test1, plain_test1, plain_test2);
    mpz_mod(plain_test1, plain_test1, pub->n);

    opt_paillier_decrypt(decrypt_test, cipher_test1, pub, pri);

    printf("Text1 = %s\n", op1.c_str());
    gmp_printf("Ciphertext = %Zd\n", cipher_test1);
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

TEST(homomorphic, EncryptedType) {
  TIME_STAT(opt_paillier_keygen(&pub, &pri, bitLength), KeyGen)
  EncryptedType<float>::pub = pub;

  float f1 = 13.3, f2 = 15.6;
  mpz_t mpz_t1, mpz_t2;
  mpz_inits(mpz_t1, mpz_t2, nullptr);
  opt_paillier_encrypt_t(mpz_t1, f1, pub);
  opt_paillier_encrypt_t(mpz_t2, f2, pub);
  cout << "==============================================" << endl;
  EncryptedType et1(13.3);
  EncryptedType et2(15.6);
  opt_paillier_decrypt(et1.data_, et1.data_, pub, pri);
  opt_paillier_decrypt(et2.data_, et2.data_, pub, pri);
  cout << "et1: " << et1 << endl;
  cout << "et2: " << et2 << endl;
  et1.SetCipherData(mpz_t1);
  et2.SetCipherData(mpz_t2);
  cout << "==============================================" << endl;
  auto et = et1 + et2;
  opt_paillier_decrypt(et.data_, et.data_, pub, pri);
  cout << "et1 + et2: " << et << endl;
  et2 += et1;
  opt_paillier_decrypt(et2.data_, et2.data_, pub, pri);
  cout << "et2 += et1: " << et2 << endl;
  et2.SetData(f2);
  cout << "==============================================" << endl;
  et = et1 - et2;
  opt_paillier_decrypt(et.data_, et.data_, pub, pri);
  cout << "et1 - et2: " << et << endl;
  et1 -= et2;
  opt_paillier_decrypt(et1.data_, et1.data_, pub, pri);
  cout << "et1 -= et2: " << et1 << endl;
  et1.SetData(f1);
  cout << "==============================================" << endl;
  et = et2 * 2;
  opt_paillier_decrypt(et.data_, et.data_, pub, pri);
  cout << "et2 * 2: " << et << endl;
  et2 *= 2;
  opt_paillier_decrypt(et2.data_, et2.data_, pub, pri);
  cout << "et2 *= 2: " << et2 << endl;
  et2.SetData(f2);
  cout << "==============================================" << endl;
  et = et2 / 2;
  opt_paillier_decrypt(et.data_, et.data_, pub, pri);
  cout << "et2 / 2: " << et << endl;
  et2 /= 2;
  opt_paillier_decrypt(et2.data_, et2.data_, pub, pri);
  cout << "et2 /= 2: " << et2 << endl;

  mpz_clears(mpz_t1, mpz_t2, nullptr);
}
