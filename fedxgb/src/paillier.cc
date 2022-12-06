// Copyright 2020 Tencent Inc.

#include "paillier.h"

#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include <random>
#include <stdexcept>
#include <vector>

namespace angel {
namespace fl {

#define DEFAULT_ALPHA_BITS 448

void initPrivateKey(PrivateKey *sk) {
  mpz_init(sk->p);
  mpz_init(sk->q);
  mpz_init(sk->p_square);
  mpz_init(sk->q_square);
  mpz_init(sk->hp);
  mpz_init(sk->hq);
  mpz_init(sk->p_inverse);
  mpz_init(sk->alpha_p);
  mpz_init(sk->alpha_q);
  mpz_init(sk->alpha);
}

void initGivenModulus(PublicKey *pk) {
  // n_square = n * n
  mpz_mul(pk->n_square, pk->n, pk->n);
  // g = n + 1
  mpz_add_ui(pk->g, pk->n, 1);
  pk->bits = mpz_sizeinbase(pk->n, 2);
}

void initPublicKey(PublicKey *pk) {
  mpz_init(pk->n);
  mpz_init(pk->g);
  mpz_init(pk->n_square);
}

void initGivenPQG(PrivateKey *sk, const mpz_t &g, int scheme) {
  // generate private keys
  // p_square = p * p
  mpz_mul(sk->p_square, sk->p, sk->p);
  // q_square = q * q
  mpz_mul(sk->q_square, sk->q, sk->q);
  // p_inverse = p mod_inverse q
  mpz_invert(sk->p_inverse, sk->p, sk->q);

  if (scheme == 1) {
    // p_minus_one = p - 1, q_minus_one = q - 1
    mpz_sub_ui(sk->alpha_p, sk->p, 1);
    mpz_sub_ui(sk->alpha_q, sk->q, 1);
    // hp = hfunc(p, p_square)
    hfunc(sk->hp, g, sk->p, sk->alpha_p, sk->p_square);
    // hq = hfunc(q, q_square)
    hfunc(sk->hq, g, sk->q, sk->alpha_q, sk->q_square);
  } else if (scheme == 3) {
    //  hfunc(sk->hp, g, sk->p, sk->alpha, sk->p_square); // ((g^{alpha} mod p_square - 1) / p)^-1
    //  mod p hfunc(sk->hq, g, sk->q, sk->alpha, sk->q_square); // ((g^{alpha} mod q_square - 1) /
    //  q)^-1 mod q
    mpz_mul(sk->hp, sk->q, sk->alpha_p);
    // mpz_mul(sk->hp, sk->q, sk->alpha);
    mpz_mod(sk->hp, sk->hp, sk->p_square);
    mpz_invert(sk->hp, sk->hp, sk->p_square);

    mpz_mul(sk->hq, sk->p, sk->alpha_q);
    // mpz_mul(sk->hq, sk->p, sk->alpha);
    mpz_mod(sk->hq, sk->hq, sk->q_square);
    mpz_invert(sk->hq, sk->hq, sk->q_square);
  } else {
    throw std::logic_error("scheme should be 1 or 3");
  }
}

void clearPrivateKey(PrivateKey *sk) {
  mpz_clear(sk->p);
  mpz_clear(sk->q);
  mpz_clear(sk->p_square);
  mpz_clear(sk->q_square);
  mpz_clear(sk->hp);
  mpz_clear(sk->hq);
  mpz_clear(sk->p_inverse);
  mpz_clear(sk->alpha_p);
  mpz_clear(sk->alpha_q);
  mpz_clear(sk->alpha);
}

void clearPublicKey(PublicKey *pk) {
  mpz_clear(pk->n);
  mpz_clear(pk->g);
  mpz_clear(pk->n_square);
}

void probableRandomPrime(mpz_t prime, gmp_randstate_t state, uint64_t bitLength) {
  do {
    // generate a random number in the interval [0, 2^(length-1) )
    mpz_urandomb(prime, state, bitLength - 1);
    // shift number to the interval [2^(length-1), 2^length )
    mpz_setbit(prime, bitLength - 1);
  } while (!mpz_probab_prime_p(prime, 50));
}

void crt(mpz_t output, mpz_t mp, mpz_t mq, const PrivateKey &sk) {
  mpz_sub(output, mq, mp);
  mpz_mul(output, output, sk.p_inverse);
  mpz_mod(output, output, sk.q);
  mpz_mul(output, output, sk.p);
  mpz_add(output, mp, output);
}

void crt(mpz_t output, mpz_t a, mpz_t p, mpz_t b, mpz_t q) {
  mpz_t p_inverse, q_inverse;
  mpz_init(p_inverse);
  mpz_init(q_inverse);

  mpz_invert(p_inverse, p, q);
  mpz_invert(q_inverse, q, p);

  mpz_t t;
  mpz_init(t);
  mpz_mul(t, a, q_inverse);
  mpz_mul(t, t, q);

  mpz_mul(output, b, p_inverse);
  mpz_mul(output, output, p);
  mpz_add(output, output, t);
  mpz_mul(t, p, q);
  mpz_mod(output, output, t);

  mpz_clear(p_inverse);
  mpz_clear(q_inverse);
  mpz_clear(t);
}

// compute L(u) = \frac{u - 1}{d}
void lfunc(mpz_t output, mpz_t input, const mpz_t &d) {
  mpz_sub_ui(output, input, 1);
  mpz_tdiv_q(output, output, d);
}

// compute H(u) = (\frac{g^(x-1) mod x^2 - 1}{x})^-1 mod x
void hfunc(mpz_t output, const mpz_t &g, const mpz_t &x, const mpz_t &x_minus_one,
           const mpz_t &x_square) {
  mpz_powm(output, g, x_minus_one, x_square);
  lfunc(output, output, x);
  mpz_invert(output, output, x);
}

void generatePaillierKeys1(PublicKey *pk, PrivateKey *sk, int bitLength) {
  gmp_randstate_t state;
  srand((unsigned)time(NULL));
  gmp_randinit_default(state);
  gmp_randseed_ui(state, rand() * rand());

  initPublicKey(pk);
  initPrivateKey(sk);
  uint64_t primeLen = bitLength / 2;
  // generate p, q, n
  do {
    probableRandomPrime(sk->p, state, primeLen);
    probableRandomPrime(sk->q, state, primeLen);
    while (!mpz_cmp(sk->p, sk->q)) probableRandomPrime(sk->q, state, primeLen);
    mpz_mul(pk->n, sk->p, sk->q);
  } while (mpz_sizeinbase(pk->n, 2) != bitLength);

  initGivenModulus(pk);
  initGivenPQG(sk, pk->g, 1);
  pk->scheme = 1;
  sk->scheme = 1;
  gmp_randclear(state);
}

void probableRandomPrime256(mpz_t prime, gmp_randstate_t state, size_t high_one_size) {
  do {
    mpz_urandomb(prime, state, 256);
    mpz_setbit(prime, 0);

    for (size_t i = 0; i < high_one_size; i++) {
      mpz_setbit(prime, 255 - i);
    }

  } while (!mpz_probab_prime_p(prime, 50));
}

void probableRandomPrime(mpz_t prime, mpz_t alpha, mpz_t beta, mpz_t prime256,
                         gmp_randstate_t state, uint64_t alpha_size, uint64_t bitLength) {
  size_t prime_len = mpz_sizeinbase(prime256, 2);

  mpz_t mask;
  mpz_init(mask);

  mpz_set_ui(mask, 15);
  mpz_mul_2exp(mask, mask, bitLength - 4);

  do {
    mpz_urandomb(alpha, state, alpha_size);
    mpz_setbit(alpha, 0);
    mpz_setbit(alpha, alpha_size - 1);

    mpz_urandomb(beta, state, bitLength - prime_len - alpha_size - 1);
    mpz_setbit(beta, 0);
    mpz_setbit(beta, bitLength - prime_len - alpha_size - 2);

    mpz_mul(prime, beta, prime256);
    mpz_mul(prime, prime, alpha);

    mpz_add(prime, prime, prime);
    mpz_add_ui(prime, prime, 1);

  } while (mpz_cmp(prime, mask) <= 0 || !mpz_probab_prime_p(prime, 50));

  mpz_clear(mask);
}

void generatePaillierKeys3(PublicKey *pk, PrivateKey *sk, int bitLength) {
  gmp_randstate_t state;
  srand((unsigned)time(NULL));
  gmp_randinit_default(state);
  gmp_randseed_ui(state, rand() * rand());

  size_t alpha_size = DEFAULT_ALPHA_BITS / 2;

  initPublicKey(pk);
  initPrivateKey(sk);

  mpz_t alpha1, alpha2;
  mpz_t beta1, beta2;
  mpz_t beta;

  mpz_init(alpha1);
  mpz_init(alpha2);
  mpz_init(beta1);
  mpz_init(beta2);
  mpz_init(beta);

  do {
    probableRandomPrime256(sk->hp, state, 16);
    probableRandomPrime256(sk->hq, state, 16);
  } while (mpz_cmp(sk->hp, sk->hq) == 0);

  do {
    probableRandomPrime(sk->p, sk->alpha_p, beta1, sk->hp, state, alpha_size, bitLength / 2);
    probableRandomPrime(sk->q, sk->alpha_q, beta2, sk->hq, state, alpha_size, bitLength / 2);

    mpz_mul(sk->alpha, sk->alpha_p, sk->alpha_q);
    mpz_mul(beta, beta1, beta2);

    mpz_gcd(alpha1, sk->alpha, beta);
    mpz_mul(pk->n, sk->p, sk->q);

    //    printf("bit length = %d, gcd = %d \n", mpz_sizeinbase(pk->n, 2), mpz_cmp_ui(alpha1, 1));
  } while (mpz_sizeinbase(pk->n, 2) != bitLength || mpz_cmp_ui(alpha1, 1) != 0);

  mpz_add(sk->alpha, sk->alpha, sk->alpha);
  mpz_add(sk->alpha_p, sk->alpha_p, sk->alpha_p);
  mpz_add(sk->alpha_q, sk->alpha_q, sk->alpha_q);

  mpz_mul(beta, beta, sk->hp);
  mpz_mul(beta, beta, sk->hq);
  mpz_add(beta, beta, beta);

  mpz_urandomb(pk->g, state, bitLength);
  //    mpz_powm(pk->g, pk->g, beta, pk->n);
  //    mpz_sub(pk->g, pk->n, pk->g);

  // create public key
  mpz_mul(pk->n_square, pk->n, pk->n);
  pk->scheme = 3;

  // create private key
  initGivenPQG(sk, pk->g, 3);
  sk->scheme = 3;

  mpz_powm(alpha1, pk->g, beta, sk->p_square);
  mpz_powm(alpha2, pk->g, beta, sk->q_square);
  crt(pk->g, alpha1, sk->p_square, alpha2, sk->q_square);
  mpz_sub(pk->g, pk->n, pk->g);

  pk->bits = mpz_sizeinbase(pk->n, 2);

  mpz_t t;
  mpz_init(t);

  // check1: the order of g is alpha mod n
  mpz_powm(t, pk->g, sk->alpha, pk->n);
  assert(mpz_cmp_ui(t, 1) == 0);

  mpz_clear(alpha1);
  mpz_clear(alpha2);
  mpz_clear(beta1);
  mpz_clear(beta2);
  mpz_clear(beta);
  mpz_clear(t);
  gmp_randclear(state);
}

inline void decrypt(mpz_t plain, mpz_t cipher, mpz_t mp, mpz_t mq, const PrivateKey &sk) {
  // mp
  mpz_powm_sec(mp, cipher, sk.alpha_p, sk.p_square);
  lfunc(mp, mp, sk.p);
  mpz_mul(mp, mp, sk.hp);
  mpz_mod(mp, mp, sk.p);

  // mq
  mpz_powm_sec(mq, cipher, sk.alpha_q, sk.q_square);
  lfunc(mq, mq, sk.q);
  mpz_mul(mq, mq, sk.hq);
  mpz_mod(mq, mq, sk.q);

  crt(plain, mp, mq, sk);
}

void batchDecrypt(mpz_t *plains, mpz_t *ciphers, size_t size, const PrivateKey &sk) {
  mpz_t mp, mq;
#ifndef WITH_OPENMP
  mpz_init(mp);
  mpz_init(mq);
#else
#pragma omp parallel private(mp, mq)
#endif
  {
#ifdef WITH_OPENMP
    mpz_init(mp);
    mpz_init(mq);
#endif
#ifdef WITH_OPENMP
#pragma omp for
#endif
    for (size_t i = 0; i < size; i++) decrypt(plains[i], ciphers[i], mp, mq, sk);

#ifdef WITH_OPENMP
    mpz_clear(mp);
    mpz_clear(mq);
#endif
  }
#ifndef WITH_OPENMP
  mpz_clear(mp);
  mpz_clear(mq);
#endif
}

BatchPaillierPublicKey::BatchPaillierPublicKey(angel::fl::PublicKey pk, int n_pre_noise,
                                               int n_noise)
    : pk(pk), n_pre_noise(n_pre_noise), n_noise(n_noise) {
  assert(pk.scheme == 1 || pk.scheme == 3);
  if (pk.scheme == 3) {
    noises = new mpz_t[DEFAULT_ALPHA_BITS * 4];
    for (size_t i = 0; i < DEFAULT_ALPHA_BITS * 4; i++) mpz_init(noises[i]);

    initialize3();
  } else {
    noises = new mpz_t[n_pre_noise];
    for (size_t i = 0; i < n_pre_noise; i++) mpz_init(noises[i]);

    initialize1();
  }
}

BatchPaillierPublicKey::~BatchPaillierPublicKey() {
  if (noises != nullptr) {
    if (pk.scheme == 3) {
      for (size_t i = 0; i < DEFAULT_ALPHA_BITS * 4; i++) mpz_clear(noises[i]);
    } else {
      for (size_t i = 0; i < n_pre_noise; i++) mpz_clear(noises[i]);
    }

    delete[] (noises);
  }
  clearPublicKey(&pk);
}

void BatchPaillierPublicKey::initialize3() {
  // generate pre-computed random noises
  mpz_powm(pk.g, pk.g, pk.n, pk.n_square);

  mpz_set_ui(noises[0], 1);
  mpz_set(noises[1], pk.g);
  for (int j = 2; j < 16; j++) {
    mpz_mul(noises[j], noises[j - 1], noises[1]);
    mpz_mod(noises[j], noises[j], pk.n_square);
  }

  for (int i = 1; i < DEFAULT_ALPHA_BITS / 4; i++) {
    mpz_powm_ui(noises[i * 16], noises[(i - 1) * 16], 16, pk.n_square);
    mpz_powm_ui(noises[i * 16 + 1], noises[(i - 1) * 16 + 1], 16, pk.n_square);
    for (int j = 2; j < 16; j++) {
      mpz_mul(noises[i * 16 + j], noises[i * 16 + j - 1], noises[i * 16 + 1]);
      mpz_mod(noises[i * 16 + j], noises[i * 16 + j], pk.n_square);
    }
  }
}

void BatchPaillierPublicKey::initialize1() {
  gmp_randstate_t state;
  srand((unsigned)time(NULL));
  gmp_randinit_default(state);
  gmp_randseed_ui(state, rand() * rand());

#ifdef WITH_OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < n_pre_noise; i++) {
    do {
      mpz_urandomb(noises[i], state, pk.bits);
    } while (mpz_cmp(noises[i], pk.n) >= 0);  // make sure r < n
    mpz_powm(noises[i], noises[i], pk.n, pk.n_square);
  }

  gmp_randclear(state);
}

void BatchPaillierPublicKey::generateRandomNoise(mpz_t noise) {
  // generate n_noise * 4 random bytes, every 4 bytes represents a random integer
  auto gen_rand_bytes = [&](unsigned char *arr, size_t len) {
    std::mt19937_64 gen(std::random_device{}());
    std::uniform_int_distribution<unsigned int> dist(0, 255);

    for (size_t i = 0; i < len; i++) {
      arr[i] = dist(gen);
    }
  };

  if (pk.scheme == 3) {
    unsigned char rand_bytes[DEFAULT_ALPHA_BITS / 4] = {0};
    gen_rand_bytes(rand_bytes, DEFAULT_ALPHA_BITS / 4);

    for (size_t i = 0; i < DEFAULT_ALPHA_BITS / 4; i++) {
      mpz_mul(noise, noise, noises[i * 16 + (rand_bytes[i] & 0xf)]);
      mpz_mod(noise, noise, pk.n_square);
    }
  } else {
    unsigned char rand_bytes[50] = {0};
    gen_rand_bytes(rand_bytes, n_noise * 4);
    uint32_t *rand = (uint32_t *)rand_bytes;

    for (size_t i = 0; i < n_noise; i++) {
      mpz_mul(noise, noise, noises[rand[i] % n_pre_noise]);
      mpz_mod(noise, noise, pk.n_square);
    }
  }
}

void BatchPaillierPublicKey::encrypt3(mpz_t *ciphers, mpz_t *plains, size_t size) {
  mpz_t noise;
#ifndef WITH_OPENMP
  mpz_init(noise);
#else
#pragma omp parallel private(noise)
#endif
  {
#ifdef WITH_OPENMP
    mpz_init(noise);
#pragma omp for
#endif
    for (size_t i = 0; i < size; i++) {
      mpz_set_ui(noise, 1);
      generateRandomNoise(noise);
      mpz_mul(ciphers[i], plains[i], pk.n);
      mpz_mod(ciphers[i], ciphers[i], pk.n_square);
      mpz_add_ui(ciphers[i], ciphers[i], 1);
      mpz_mul(ciphers[i], ciphers[i], noise);
      mpz_mod(ciphers[i], ciphers[i], pk.n_square);
    }
#ifdef WITH_OPENMP
    mpz_clear(noise);
#endif
  }
#ifndef WITH_OPENMP
  mpz_clear(noise);
#endif
}

void BatchPaillierPublicKey::encrypt1(mpz_t *ciphers, mpz_t *plains, size_t size) {
  mpz_t noise;
#ifndef WITH_OPENMP
  mpz_init(noise);
#else
#pragma omp parallel private(noise)
#endif
  {
#ifdef WITH_OPENMP
    mpz_init(noise);
#pragma omp for
#endif
    for (int i = 0; i < size; i++) {
      mpz_set_ui(noise, 1);
      generateRandomNoise(noise);
      mpz_mul(ciphers[i], pk.n, plains[i]);
      mpz_add_ui(ciphers[i], ciphers[i], 1);
      mpz_mod(ciphers[i], ciphers[i], pk.n_square);
      mpz_mul(ciphers[i], ciphers[i], noise);
      mpz_mod(ciphers[i], ciphers[i], pk.n_square);
    }
#ifdef WITH_OPENMP
    mpz_clear(noise);
#endif
  }
#ifndef WITH_OPENMP
  mpz_clear(noise);
#endif
}

void BatchPaillierPublicKey::encrypt(mpz_t *ciphers, mpz_t *plains, size_t size) {
  if (pk.scheme == 1)
    encrypt1(ciphers, plains, size);
  else
    encrypt3(ciphers, plains, size);
}

const PublicKey &BatchPaillierPublicKey::getPublicKey() { return pk; }

}  // namespace fl
}  // namespace angel
