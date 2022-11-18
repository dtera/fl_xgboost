// Copyright 2020 Tencent Inc.

#ifndef FL_RUNTIME_CORE_NATIVE_LIB_INCLUDE_PAILLIER_H_
#define FL_RUNTIME_CORE_NATIVE_LIB_INCLUDE_PAILLIER_H_

#include <gmp.h>
#include <stdint.h>

#include <vector>

#include "common/threading_utils.h"

using namespace std;
using namespace xgboost::common;

namespace fl {
namespace he {

/**
 * Structure of PrivateKey of Paillier
 */
struct PrivateKey {
  mpz_t p;
  mpz_t q;
  mpz_t q_square;
  mpz_t p_square;
  mpz_t p_inverse;
  mpz_t hp;
  mpz_t hq;
  mpz_t p_minus_one;
  mpz_t q_minus_one;
  mpz_t alpha;
  int scheme;
};

/**
 * Structure of PublicKey of Paillier
 */
struct PublicKey {
  int bits;
  int scheme;
  mpz_t n;
  mpz_t g;
  mpz_t n_square;
};

/**
 * using mpz_init initialize each element of PublicKey
 * @param pk, the pointer of PublicKey
 */
void initPublicKey(PublicKey *pk);

/**
 * calculate g, n_square given n, assumption that pk is initialized with mpz_init
 * @param pk, the pointer of PublicKey
 */
void initGivenModulus(PublicKey *pk);

/**
 * using mpz_init initialize each element of PrivateKey
 * @param sk, the pointer of PrivateKey
 */
void initPrivateKey(PrivateKey *sk);

/**
 * calculate other elements of PrivateKey given p, q and g
 * @param sk, the pointer of PrivateKey, given p, q already calculated
 * @param g, g from PublicKey
 * @param scheme, scheme value of paillier
 */
void initGivenPQG(PrivateKey *sk, const mpz_t &g, int scheme);

/**
 * using mpz_clear clear each element of PublicKey
 * @param pk, the pointer of PublicKey
 */
void clearPublicKey(PublicKey *pk);

/**
 * using mpz_clear clear each element of PrivateKey
 * @param sk, the pointer of PrivateKey
 */
void clearPrivateKey(PrivateKey *sk);

/**
 * generate a prime (probably) with bit length ``bitLength``
 * @param prime, the generated prime
 * @param state, the gmp random state
 * @param bitLength, the bit length of the prime
 */
void probableRandomPrime(mpz_t prime, gmp_randstate_t state, uint64_t bitLength);

/* lfunction, L(u) = \frac{u - 1}{d} */
void lfunc(mpz_t output, mpz_t u, const mpz_t &d);

/* hfunc for paillier decryption */
void hfunc(mpz_t output, const mpz_t &g, const mpz_t &x, const mpz_t &x_minus_one,
           const mpz_t &x_square);

/**
 * generate DSA keys with ``bitLength``, the results are stored in ``p``, ``g`` and ``alpha``
 * @param p, result p
 * @param g, result g
 * @param alpha, result alpha
 * @param bitLength, the length of p
 */
void generateDSAKeys(mpz_t p, mpz_t g, mpz_t alpha, int bitLength);

/**
 * Chinese Reminder Theorem, a faster version for paillier decryption.
 */
void crt(mpz_t output, mpz_t mp, mpz_t mq, const PrivateKey &sk);

/**
 * Chinese Reminder Theorem, a standard version, for generating paillier3 keys
 */
void crt(mpz_t output, mpz_t a, mpz_t p, mpz_t b, mpz_t q);

/**
 * generate paillier private/public keys with scheme3
 * @param pk, the pointer to PublicKey
 * @param sk, the pointer to PrivateKey
 * @param bitLength, the number of bits for ``n``
 * @param schema, the schema of key
 */
void generatePaillierKeys(PublicKey *pk, PrivateKey *sk, int bitLength, int schema = 1);

/**
 * Decrypting a batch of ciphertexts with paillier scheme3
 * @param plains, the array of result plaintexts
 * @param ciphers, the array of input ciphertexts
 * @param size, the number of ciphertexts needed to be decrypted
 * @param sk, the PrivateKey of paillier3
 */
void batchDecrypt(mpz_t *plains, mpz_t *ciphers, size_t size, const PrivateKey &sk,
                  int32_t n_threads = 10);

void paillierAdd(mpz_t &res, const mpz_t &op1, const mpz_t &op2, const PublicKey *pk);

void paillierConstantMul(mpz_t &res, const mpz_t &op1, const mpz_t &op2, const PublicKey *pk);

/**
 * A batch version of Paillier3 Encryptor. When encrypting, we use a pre-computed noises
 * and pre-computed powers of ``g`` to accelerate the speed of encrypting.
 */
class PaillierBatchEncryptor {
 public:
  PaillierBatchEncryptor() = default;

  PaillierBatchEncryptor(PublicKey pk, int n_pre_noise, int n_noise);

  ~PaillierBatchEncryptor();

  /**
   * encrypt a batch of plaintexts
   * @param ciphers, the array to hold the encrypted results.
   * @param plains, the input plaintexts to be encrypted.
   * @param size, the number of elements to be encrypted.
   */
  void encrypt(mpz_t *ciphers, mpz_t *plains, size_t size);

  /**
   * @return a const reference of publicKey
   */
  const PublicKey &getPublicKey();

 private:
  /**
   * batch encryption method using scheme3.
   */
  void encrypt3(mpz_t *ciphers, mpz_t *plains, size_t size);

  /**
   * batch encryption using scheme1.
   */
  void encrypt1(mpz_t *ciphers, mpz_t *plains, size_t size);

  /**
   * initialize paillier1
   */
  void initialize1();

  /**
   * initialize paillier3
   */
  void initialize3();

  /**
   * generate a noise using pre-computed noises
   * @param noise, the mpz_t value to hold the noise
   */
  void generateRandomNoise(mpz_t noise);

  /* PublicKey of paillier3 */
  PublicKey pk;
  /* array to hold pre-computed noises */
  mpz_t *noises;
  /* the number of pre-computed noises */
  size_t n_pre_noise;
  /* the number of noises to generate a noise */
  size_t n_noise;
};

}  // namespace he
}  // namespace fl

#endif  // FL_RUNTIME_CORE_NATIVE_LIB_INCLUDE_PAILLIER_H_
