//
// Created by HqZhao on 2022/11/15.
//
#ifndef OPT_PAILLIER_H
#define OPT_PAILLIER_H

#include <math.h>
#include <omp.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

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
  /* for precomputing of CRT */
  mpz_t P_squared_mul_P_squared_inverse;  // P^2 * ((P^2)^-1 mod Q^2)

  /* fixed-base parameters */
  fb_instance fb_mod_P_sqaured;
  fb_instance fb_mod_Q_sqaured;

  friend std::ostream& operator<<(std::ostream& os, const opt_public_key_t& k) {
    os << "nbits: " << k.nbits << std::endl;
    os << "lbits: " << k.lbits << std::endl;

    os << "[mpz_t]n: " << std::endl;
    os << "\tn._mp_alloc: " << k.n->_mp_alloc << std::endl;
    os << "\tn._mp_size: " << k.n->_mp_size << std::endl;
    os << "\tn._mp_d: " << *k.n->_mp_d << std::endl;

    os << "[mpz_t]half_n: " << std::endl;
    os << "\thalf_n._mp_alloc: " << k.half_n->_mp_alloc << std::endl;
    os << "\thalf_n._mp_size: " << k.half_n->_mp_size << std::endl;
    os << "\thalf_n._mp_d: " << *k.half_n->_mp_d << std::endl;

    os << "[mpz_t]n_squared: " << std::endl;
    os << "\tn_squared._mp_alloc: " << k.n_squared->_mp_alloc << std::endl;
    os << "\tn_squared._mp_size: " << k.n_squared->_mp_size << std::endl;
    os << "\tn_squared._mp_d: " << *k.n_squared->_mp_d << std::endl;

    os << "[mpz_t]h_s: " << std::endl;
    os << "\th_s._mp_alloc: " << k.h_s->_mp_alloc << std::endl;
    os << "\th_s._mp_size: " << k.h_s->_mp_size << std::endl;
    os << "\th_s._mp_d: " << *k.h_s->_mp_d << std::endl;

    os << "[fb_instance]fb_mod_P_sqaured: " << std::endl;
    os << "\t[mpz_t]m_mod: " << std::endl;
    os << "\t\tm_mod._mp_alloc: " << k.fb_mod_P_sqaured.m_mod->_mp_alloc << std::endl;
    os << "\t\tm_mod._mp_size: " << k.fb_mod_P_sqaured.m_mod->_mp_size << std::endl;
    os << "\t\tm_mod._mp_d: " << *k.fb_mod_P_sqaured.m_mod->_mp_d << std::endl;
    os << "\t[mpz_t]m_table_G: " << std::endl;
    os << "\t\tm_table_G._mp_alloc: " << (*k.fb_mod_P_sqaured.m_table_G)->_mp_alloc << std::endl;
    os << "\t\tm_table_G._mp_size: " << (*k.fb_mod_P_sqaured.m_table_G)->_mp_size << std::endl;
    os << "\t\tm_table_G._mp_d: " << *(*k.fb_mod_P_sqaured.m_table_G)->_mp_d << std::endl;
    os << "\tm_h: " << k.fb_mod_P_sqaured.m_h << std::endl;
    os << "\tm_t: " << k.fb_mod_P_sqaured.m_t << std::endl;
    os << "\tm_w: " << k.fb_mod_P_sqaured.m_w << std::endl;

    os << "[fb_instance]fb_mod_Q_sqaured: " << std::endl;
    os << "\t[mpz_t]m_mod: " << std::endl;
    os << "\t\tm_mod._mp_alloc: " << k.fb_mod_Q_sqaured.m_mod->_mp_alloc << std::endl;
    os << "\t\tm_mod._mp_size: " << k.fb_mod_Q_sqaured.m_mod->_mp_size << std::endl;
    os << "\t\tm_mod._mp_d: " << *k.fb_mod_Q_sqaured.m_mod->_mp_d << std::endl;
    os << "\t[mpz_t]m_table_G: " << std::endl;
    os << "\t\tm_table_G._mp_alloc: " << (*k.fb_mod_Q_sqaured.m_table_G)->_mp_alloc << std::endl;
    os << "\t\tm_table_G._mp_size: " << (*k.fb_mod_Q_sqaured.m_table_G)->_mp_size << std::endl;
    os << "\t\tm_table_G._mp_d: " << *(*k.fb_mod_Q_sqaured.m_table_G)->_mp_d << std::endl;
    os << "\tm_h: " << k.fb_mod_Q_sqaured.m_h << std::endl;
    os << "\tm_t: " << k.fb_mod_Q_sqaured.m_t << std::endl;
    os << "\tm_w: " << k.fb_mod_Q_sqaured.m_w << std::endl;
    return os;
  }
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
  /* for precomputing of CRT
     P^2 * ((P^2)^-1 mod Q^2) */
  mpz_t P_squared_mul_P_squared_inverse;
  //  P * (P^-1 mod Q)
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

void opt_paillier_mod_n_squared_crt_fb(mpz_t& res, const mpz_t& exp, const opt_public_key_t* pub);

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
                                int32_t n_threads = omp_get_num_procs(), const bool is_fb = true);

void opt_paillier_batch_decrypt(mpz_t* res, const mpz_t* ciphertext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads = omp_get_num_procs(), const bool is_crt = true);

void opt_paillier_encrypt(mpz_t& res, const char* plaintext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri = nullptr, int radix = 10,
                          const bool is_fb = true);

void opt_paillier_decrypt(char*& res, const mpz_t& ciphertext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri, int radix = 10, const bool is_crt = true);

void opt_paillier_batch_encrypt(mpz_t* res, char** plaintext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri = nullptr,
                                int32_t n_threads = omp_get_num_procs(), int radix = 10,
                                const bool is_fb = true);

void opt_paillier_batch_decrypt(char** res, const mpz_t* ciphertext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads = omp_get_num_procs(), int radix = 10,
                                const bool is_crt = true);

void opt_paillier_add(mpz_t& res, const mpz_t& op1, const mpz_t& op2, const opt_public_key_t* pub);

void opt_paillier_sub(mpz_t& res, const mpz_t& op1, const mpz_t& op2, const opt_public_key_t* pub);

void opt_paillier_batch_add(mpz_t& res, const mpz_t* ops, const size_t size,
                            const opt_public_key_t* pub, int32_t n_threads = omp_get_num_procs());

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
                            const opt_private_key_t* pri = nullptr, int precision = 8,
                            int radix = 10, const bool is_fb = true) {
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
                                  const opt_private_key_t* pri = nullptr,
                                  int32_t n_threads = omp_get_num_procs(), int precision = 8,
                                  int radix = 10, const bool is_fb = true) {
  ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_encrypt_t(res[i], plaintext[i], pub, pri, precision, radix, is_fb);  //
  });
}

template <typename PLAIN_TYPE>
void opt_paillier_batch_decrypt_t(PLAIN_TYPE& res, const mpz_t* ciphertext, size_t size,
                                  const opt_public_key_t* pub, const opt_private_key_t* pri,
                                  int32_t n_threads = omp_get_num_procs(), int precision = 8,
                                  int radix = 10, const bool is_crt = true) {
  ParallelFor(size, n_threads, [&](int i) {
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
extern std::mutex mtx;

#endif  // OPT_PAILLIER_H