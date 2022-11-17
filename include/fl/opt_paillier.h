//
// Created by HqZhao on 2022/11/15.
//
#ifndef DEMO_OPT_PAILLIER_H
#define DEMO_OPT_PAILLIER_H

#include <math.h>

#include <string>
#include <unordered_map>

#include "common/threading_utils.h"
#include "utils.h"

using namespace std;

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
    if (mpz_cmp_ui(mpz_plaintext, 0) < 0) {
      mpz_add(mpz_plaintext, mpz_plaintext, pub->n);
      mpz_mod(mpz_plaintext, mpz_plaintext, pub->n);
    }
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
    plaintext = atoi(temp);
  } else if (std::is_same<PLAIN_TYPE, uint32_t>()) {
    plaintext = atol(temp);
  } else if (std::is_same<PLAIN_TYPE, int64_t>() || std::is_same<PLAIN_TYPE, uint64_t>()) {
    plaintext = atoll(temp);
  } else if (std::is_same<PLAIN_TYPE, double>() || std::is_same<PLAIN_TYPE, float>()) {
    plaintext = atof(temp) / pow(10, precision);
  } else {
    return;
  }
}

template <typename PLAIN_TYPE>
void opt_paillier_encrypt_t(mpz_t& res, const PLAIN_TYPE& plaintext, const opt_public_key_t* pub,
                            const opt_private_key_t* pri, int radix = 10, int precision = 8,
                            const bool is_fb = true) {
  mpz_t temp;
  mpz_init(temp);
  opt_paillier_set_plaintext_t(temp, plaintext, pub, radix, precision);
  opt_paillier_encrypt(res, temp, pub, pri, is_fb);
}

template <typename PLAIN_TYPE>
void opt_paillier_decrypt_t(PLAIN_TYPE& res, const mpz_t& ciphertext, const opt_public_key_t* pub,
                            const opt_private_key_t* pri, int radix = 10, int precision = 8,
                            const bool is_crt = true) {
  mpz_t temp;
  mpz_init(temp);
  opt_paillier_set_plaintext_t(temp, res, pub, radix, precision);
  opt_paillier_decrypt(temp, ciphertext, pub, pri, is_crt);
}

template <typename PLAIN_TYPE>
void opt_paillier_batch_encrypt_t(mpz_t* res, const PLAIN_TYPE* plaintext, size_t size,
                                  const opt_public_key_t* pub, const opt_private_key_t* pri,
                                  int32_t n_threads = 10, int radix = 10, int precision = 8,
                                  const bool is_fb = true) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_encrypt(res[i], plaintext[i], pub, pri, radix, precision, is_fb);  //
  });
}

template <typename PLAIN_TYPE>
void opt_paillier_batch_decrypt_t(PLAIN_TYPE* res, const mpz_t* ciphertext, size_t size,
                                  const opt_public_key_t* pub, const opt_private_key_t* pri,
                                  int32_t n_threads = 10, int radix = 10, int precision = 8,
                                  const bool is_crt = true) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_decrypt(res[i], ciphertext[i], pub, pri, radix, precision, is_crt);
  });
}

//====================================datapack begin====================================
struct CrtMod {
  mpz_t* crt_half_mod;
  mpz_t* crt_mod;
  size_t crt_size;
  mp_bitcnt_t mod_size;
};

void init_crt(CrtMod** crtmod, const size_t crt_size, const mp_bitcnt_t mod_size);

void data_packing_crt(mpz_t res, char** seq, const size_t seq_size, const CrtMod* crtmod,
                      const int radix = 10);

void data_retrieve_crt(char**& seq, const mpz_t pack, const CrtMod* crtmod, const size_t data_size,
                       const opt_public_key_t* pub, const int radix = 10);

void free_crt(CrtMod* crtmod);
//====================================datapack end======================================

#endif  // DEMO_OPT_PAILLIER_H