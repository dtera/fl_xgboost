//
// Created by HqZhao on 2022/11/15.
//
#ifndef DEMO_OPT_PAILLIER_H
#define DEMO_OPT_PAILLIER_H

#include <math.h>

#include <string>
#include <unordered_map>

#include "../../src/common/threading_utils.h"
#include "utils.h"

extern std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> mapTo_nbits_lbits;
extern uint32_t prob;

/**
 * @brief TYPES
 *
 * struct opt_public_key
 *
 */
struct opt_public_key_t {
  unsigned int nbits;
  unsigned int lbits;
  mpz_t n;       // n=P*Q
  mpz_t half_n;  // plaintext domain
  /* for precomputing */
  mpz_t n_squared;  // n=n^2
  mpz_t h_s;        // h_s=(-y^2b)^n mod n^2

  /* fixed-base parameters */
  fb_instance fb_mod_P_sqaured;
  fb_instance fb_mod_Q_sqaured;
};
/**
 * @brief TYPES
 *
 * struct opt_private_key
 *
 */
struct opt_private_key_t {
  mpz_t p;      // Len(p) = lbits/2, prime
  mpz_t q;      // Len(q) = Len(p), prime
  mpz_t p_;     // Len(p_) = nbits/2-lbits/2-1, odd number
  mpz_t q_;     // L(q_) = L(p_), odd number
  mpz_t alpha;  // alpha = p*q
  mpz_t beta;   // beta = p_*q_
  mpz_t P;      // P = 2*p*p_+1
  mpz_t Q;      // Q = 2*q*q_+1
  /* for precomputing of encryption and decryption */
  mpz_t P_squared;
  mpz_t Q_squared;
  mpz_t double_alpha;
  mpz_t double_beta;
  mpz_t double_alpha_inverse;
  /* for precomputing of CRT */
  // P^2 * ((P^2)^-1 mod Q^2)
  mpz_t P_squared_mul_P_squared_inverse;
  // P * (P^-1 mod Q)
  mpz_t P_mul_P_inverse;
  /* for precomputing of advance */
  mpz_t double_p;
  mpz_t double_q;
  mpz_t Q_mul_double_p_inverse;
  mpz_t P_mul_double_q_inverse;
};

/**
 * @brief Assistant functions that CRT, Fixed-base
 *
 */
void opt_paillier_mod_n_squared_crt(mpz_t& res, const mpz_t& base, const mpz_t& exp,
                                    const opt_public_key_t* pub, const opt_private_key_t* pri);

void opt_paillier_mod_n_squared_crt_fb(mpz_t& res, const mpz_t& exp, const opt_public_key_t* pub,
                                       const opt_private_key_t* pri);

/**
 * @brief Optimizations of Paillier cryptosystem functions
 *
 */
void opt_paillier_keygen(opt_public_key_t** pub, opt_private_key_t** pri, uint32_t bitLength);

void opt_paillier_set_plaintext(mpz_t& mpz_plaintext, const char* plaintext,
                                const opt_public_key_t* pub, int radix = 10);

void opt_paillier_get_plaintext(char*& plaintext, const mpz_t& mpz_plaintext,
                                const opt_public_key_t* pub, int radix = 10);

bool validate_message(mpz_t& msg, opt_public_key_t* pub);

void opt_paillier_encrypt(mpz_t& res, const mpz_t& plaintext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri = nullptr, const bool is_fb = true);

void opt_paillier_decrypt(mpz_t& res, const mpz_t& ciphertext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri, const bool is_crt = true);

void opt_paillier_batch_encrypt(mpz_t* res, const mpz_t* plaintext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri = nullptr,
                                int32_t n_threads = 10, const bool is_fb = true);

void opt_paillier_batch_decrypt(mpz_t* res, const mpz_t* ciphertext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads = 10, const bool is_crt = true);

void opt_paillier_encrypt(mpz_t& res, const char* plaintext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri = nullptr, int radix = 10,
                          const bool is_fb = true);

void opt_paillier_decrypt(char*& res, const mpz_t& ciphertext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri, int radix = 10, const bool is_crt = true);

void opt_paillier_batch_encrypt(mpz_t* res, char** plaintext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri = nullptr,
                                int32_t n_threads = 10, int radix = 10, const bool is_fb = true);

void opt_paillier_batch_decrypt(char** res, const mpz_t* ciphertext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads = 10, int radix = 10, const bool is_crt = true);

void opt_paillier_add(mpz_t& res, const mpz_t& op1, const mpz_t& op2, const opt_public_key_t* pub);

void opt_paillier_sub(mpz_t& res, const mpz_t& op1, const mpz_t& op2, const opt_public_key_t* pub);

void opt_paillier_batch_add(mpz_t& res, const mpz_t* ops, const size_t size,
                            const opt_public_key_t* pub, int32_t n_threads = 10);

void opt_paillier_constant_mul(mpz_t& res, const mpz_t& op1, const mpz_t& op2,
                               const opt_public_key_t* pub);

/**
 * @brief memory manager
 *
 */
void opt_paillier_freepubkey(opt_public_key_t* pub);

void opt_paillier_freeprikey(opt_private_key_t* pri);

template <class PLAIN_TYPE>
void opt_paillier_set_plaintext_t(mpz_t& mpz_plaintext, const PLAIN_TYPE& plaintext,
                                  const opt_public_key_t* pub, int precision = 8, int radix = 10) {
  if (std::is_same<PLAIN_TYPE, double>() || std::is_same<PLAIN_TYPE, float>()) {
    mpz_set_d(mpz_plaintext, plaintext * pow(10, precision));
  } else {
    opt_paillier_set_plaintext(mpz_plaintext, std::to_string(plaintext).c_str(), pub, radix);
  }
}

template <typename PLAIN_TYPE>
void opt_paillier_get_plaintext_t(PLAIN_TYPE& plaintext, const mpz_t& mpz_plaintext,
                                  const opt_public_key_t* pub, int precision = 8, int radix = 10) {
  char* temp;
  opt_paillier_get_plaintext(temp, mpz_plaintext, pub, radix);

  if (std::is_same<PLAIN_TYPE, char>() || std::is_same<PLAIN_TYPE, short>() ||
      std::is_same<PLAIN_TYPE, int>() || std::is_same<PLAIN_TYPE, uint8_t>() ||
      std::is_same<PLAIN_TYPE, uint16_t>()) {
    plaintext = std::atoi(temp);
  } else if (std::is_same<PLAIN_TYPE, uint32_t>()) {
    plaintext = std::atol(temp);
  } else if (std::is_same<PLAIN_TYPE, int64_t>() || std::is_same<PLAIN_TYPE, uint64_t>()) {
    plaintext = atoll(temp);
  } else if (std::is_same<PLAIN_TYPE, double>() || std::is_same<PLAIN_TYPE, float>()) {
    plaintext = std::atof(temp) / pow(10, precision);
  } else {
    return;
  }
}

template <typename PLAIN_TYPE>
void opt_paillier_encrypt_t(mpz_t& res, const PLAIN_TYPE& plaintext, const opt_public_key_t* pub,
                            const opt_private_key_t* pri, int precision = 8, int radix = 10,
                            const bool is_fb = true) {
  mpz_t temp;
  mpz_init(temp);
  opt_paillier_set_plaintext_t(temp, plaintext, pub, precision, radix);
  opt_paillier_encrypt(res, temp, pub, pri, is_fb);
  mpz_clear(temp);
}

template <typename PLAIN_TYPE>
void opt_paillier_decrypt_t(PLAIN_TYPE& res, const mpz_t& ciphertext, const opt_public_key_t* pub,
                            const opt_private_key_t* pri, int precision = 8, int radix = 10,
                            const bool is_crt = true) {
  mpz_t temp;
  mpz_init(temp);
  opt_paillier_decrypt(temp, ciphertext, pub, pri, is_crt);
  opt_paillier_get_plaintext_t(res, temp, pub, precision, radix);
  mpz_clear(temp);
}

template <typename PLAIN_TYPE>
void opt_paillier_batch_encrypt_t(mpz_t* res, const PLAIN_TYPE& plaintext, size_t size,
                                  const opt_public_key_t* pub,
                                  const opt_private_key_t* pri = nullptr, int32_t n_threads = 10,
                                  int radix = 10, int precision = 8, const bool is_fb = true) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_encrypt_t(res[i], plaintext[i], pub, pri, precision, radix, is_fb);  //
  });
}

template <typename PLAIN_TYPE>
void opt_paillier_batch_decrypt_t(PLAIN_TYPE& res, const mpz_t* ciphertext, size_t size,
                                  const opt_public_key_t* pub, const opt_private_key_t* pri,
                                  int32_t n_threads = 10, int radix = 10, int precision = 8,
                                  const bool is_crt = true) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_decrypt_t(res[i], ciphertext[i], pub, pri, precision, radix, is_crt);
  });
}

template <typename PLAIN_TYPE>
void opt_paillier_constant_mul_t(mpz_t& res, const mpz_t& op1, const PLAIN_TYPE& op2,
                                 const opt_public_key_t* pub) {
  mpz_t temp;
  mpz_init(temp);
  opt_paillier_set_plaintext_t(temp, op2, pub);
  opt_paillier_constant_mul(res, op1, temp, pub);
}

//====================================datapack begin====================================
struct CrtMod {
  mpz_t* crt_half_mod;
  mpz_t* crt_mod;
  size_t crt_size;
  mp_bitcnt_t mod_size;
};

void init_crt(CrtMod** crtmod, const size_t crt_size, const mp_bitcnt_t mod_size);

void data_packing_crt(mpz_t& res, char** seq, const size_t seq_size, const CrtMod* crtmod,
                      const int radix = 10);

void data_retrieve_crt(char**& seq, const mpz_t& pack, const size_t data_size, const CrtMod* crtmod,
                       const opt_public_key_t* pub, const int radix = 10);

void free_crt(CrtMod* crtmod);

template <typename PLAIN_TYPE>
void data_packing_crt_t(
    mpz_t res, const PLAIN_TYPE* seq, const size_t seq_size, const CrtMod* crtmod,
    const opt_public_key_t* pub,
    std::function<void(mpz_t&, const PLAIN_TYPE&, const opt_public_key_t*, const int)> fn =
        [](mpz_t& t, const PLAIN_TYPE& p, const opt_public_key_t* pub, const int radix) {
          opt_paillier_set_plaintext_t(t, p, pub, radix);
        },
    const int radix = 10) {
  if (seq_size > crtmod->crt_size) {
    throw "size of packing is more than crt's";
  }
  mpz_set_ui(res, 0);
  mpz_t cur, muls, coef;
  mpz_inits(cur, muls, coef, nullptr);
  mpz_set_ui(muls, 1);
  for (size_t i = 0; i < seq_size; ++i) {
    mpz_mul(muls, muls, crtmod->crt_mod[i]);
  }
  for (size_t i = 0; i < seq_size; ++i) {
    fn(cur, seq[i], pub, radix);
    if (mpz_cmp_ui(cur, 0) < 0) {
      mpz_add(cur, cur, crtmod->crt_mod[i]);
      mpz_mod(cur, cur, crtmod->crt_mod[i]);
    }
    mpz_divexact(coef, muls, crtmod->crt_mod[i]);
    mpz_mul(cur, cur, coef);
    mpz_invert(coef, coef, crtmod->crt_mod[i]);
    mpz_mul(cur, cur, coef);
    mpz_add(res, res, cur);
    mpz_mod(res, res, muls);
  }
  mpz_clears(cur, muls, coef, nullptr);
}

template <typename PLAIN_TYPE>
void data_retrieve_crt_t(
    PLAIN_TYPE*& seq, const mpz_t& pack, const size_t data_size, const CrtMod* crtmod,
    const opt_public_key_t* pub,
    std::function<void(PLAIN_TYPE&, const mpz_t&, const opt_public_key_t*, const int)> fn =
        [](PLAIN_TYPE& p, const mpz_t& t, const opt_public_key_t* pub, const int radix) {
          opt_paillier_get_plaintext_t(p, t, pub, radix);
        },
    const int radix = 10) {
  if (data_size > crtmod->crt_size) {
    throw "size of packing is more than crt's";
  }
  seq = (PLAIN_TYPE*)malloc(sizeof(PLAIN_TYPE) * data_size);
  mpz_t cur;
  mpz_init(cur);
  for (size_t i = 0; i < data_size; ++i) {
    mpz_mod(cur, pack, crtmod->crt_mod[i]);
    if (mpz_cmp(cur, crtmod->crt_half_mod[i]) >= 0) {
      mpz_sub(cur, cur, crtmod->crt_mod[i]);
    }
    fn(seq[i], cur, pub, radix);
  }
  mpz_clear(cur);
}

//====================================datapack end======================================

//====================================EncryptedType begin===============================
class EncryptedType {
 public:
  // encrypted data
  mpz_t data_;
  // public key
  opt_public_key_t* pub_;

  XGBOOST_DEVICE EncryptedType(opt_public_key_t* pub) {
    mpz_init(data_);
    pub_ = pub;
  }

  XGBOOST_DEVICE EncryptedType(mpz_t& data, opt_public_key_t* pub) : EncryptedType(pub) {
    mpz_set(data_, data);
  }

  EncryptedType(const EncryptedType& g) = default;

  XGBOOST_DEVICE EncryptedType& operator+=(const EncryptedType& et) {
    opt_paillier_add(data_, data_, et.data_, pub_);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator+(const EncryptedType& et) const {
    EncryptedType g(pub_);
    opt_paillier_add(g.data_, data_, et.data_, pub_);
    return g;
  }

  XGBOOST_DEVICE EncryptedType& operator-=(const EncryptedType& et) {
    opt_paillier_sub(data_, data_, et.data_, pub_);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator-(const EncryptedType& et) const {
    EncryptedType g(pub_);
    opt_paillier_sub(g.data_, data_, et.data_, pub_);
    return g;
  }

  XGBOOST_DEVICE EncryptedType& operator*=(float multiplier) {
    opt_paillier_constant_mul_t(data_, data_, multiplier, pub_);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator*(float multiplier) const {
    EncryptedType g(pub_);
    opt_paillier_constant_mul_t(g.data_, data_, multiplier, pub_);
    return g;
  }

  XGBOOST_DEVICE EncryptedType& operator/=(float divisor) {
    opt_paillier_constant_mul_t(data_, data_, 1 / divisor, pub_);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator/(float divisor) const {
    EncryptedType g(pub_);
    opt_paillier_constant_mul_t(g.data_, data_, 1 / divisor, pub_);
    return g;
  }

  XGBOOST_DEVICE bool operator==(const EncryptedType& et) const {
    return mpz_cmp(this->data_, et.data_) == 0;
  }

  XGBOOST_DEVICE explicit EncryptedType(int value) {
    mpz_t temp;
    mpz_init(temp);
    opt_paillier_set_plaintext_t(temp, value, pub_);
    *this = EncryptedType(temp, pub_);
  }

  friend std::ostream& operator<<(std::ostream& os, const EncryptedType& g) {
    char* o;
    opt_paillier_get_plaintext(o, g.data_, g.pub_);
    os << o;
    return os;
  }
};
//====================================EncryptedType end=================================
#endif  // DEMO_OPT_PAILLIER_H