//
// Created by HqZhao on 2022/11/15.
//

#include "fl/opt_paillier.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

unordered_map<uint32_t, pair<uint32_t, uint32_t>> mapTo_nbits_lbits = {
    {1024, {432, 98}}, {2048, {448, 112}}, {3072, {512, 128}}, {7680, {768, 192}}};

uint32_t prob = 30;

void opt_paillier_mod_n_squared_crt(mpz_t& res, const mpz_t& base, const mpz_t& exp,
                                    const opt_public_key_t* pub, const opt_private_key_t* pri) {
  /* temp variable */
  mpz_t temp_base, cp, cq;
  mpz_inits(cq, cp, temp_base, nullptr);

  /* smaller exponentiations of base mod P^2, Q^2 */
  mpz_mod(temp_base, base, pri->P_squared);
  mpz_powm(cp, temp_base, exp, pri->P_squared);

  mpz_mod(temp_base, base, pri->Q_squared);
  mpz_powm(cq, temp_base, exp, pri->Q_squared);

  /* CRT to calculate base^exp mod n^2 */
  mpz_sub(cq, cq, cp);
  mpz_addmul(cp, cq, pri->P_squared_mul_P_squared_inverse);
  mpz_mod(res, cp, pub->n_squared);

  mpz_clears(cq, cp, temp_base, nullptr);
}

void opt_paillier_mod_n_squared_crt_fb(mpz_t& res, const mpz_t& exp, const opt_public_key_t* pub,
                                       const opt_private_key_t* pri) {
  /* temp variable */
  mpz_t cp, cq;
  mpz_inits(cq, cp, nullptr);

  /* smaller exponentiations of base mod P^2, Q^2 */
  fbpowmod_extend(pub->fb_mod_P_sqaured, cp, exp);

  fbpowmod_extend(pub->fb_mod_Q_sqaured, cq, exp);

  /* CRT to calculate base^exp mod n^2 */
  mpz_sub(cq, cq, cp);
  mpz_addmul(cp, cq, pri->P_squared_mul_P_squared_inverse);
  mpz_mod(res, cp, pub->n_squared);

  mpz_clears(cq, cp, nullptr);
}

void opt_paillier_keygen(opt_public_key_t** pub, opt_private_key_t** pri, uint32_t bitLength) {
  /* temp variable */
  mpz_t temp;
  mpz_t y;
  mpz_inits(y, temp, nullptr);

  /* allocate the new key structures */
  *pub = (opt_public_key_t*)malloc(sizeof(opt_public_key_t));
  *pri = (opt_private_key_t*)malloc(sizeof(opt_private_key_t));

  /* initialize some secret parameters */
  (*pub)->nbits = bitLength;
  (*pub)->lbits = mapTo_nbits_lbits[bitLength].first;

  /* initialize some integers */
  mpz_init((*pub)->h_s);
  mpz_init((*pub)->n);
  mpz_init((*pub)->n_squared);
  mpz_init((*pub)->half_n);

  mpz_init((*pri)->alpha);
  mpz_init((*pri)->beta);
  mpz_init((*pri)->p);
  mpz_init((*pri)->p_);
  mpz_init((*pri)->q);
  mpz_init((*pri)->q_);
  mpz_init((*pri)->P);
  mpz_init((*pri)->Q);
  mpz_init((*pri)->P_squared);
  mpz_init((*pri)->Q_squared);
  mpz_init((*pri)->double_alpha);
  mpz_init((*pri)->double_beta);
  mpz_init((*pri)->double_alpha_inverse);
  mpz_init((*pri)->P_mul_P_inverse);
  mpz_init((*pri)->P_squared_mul_P_squared_inverse);
  mpz_init((*pri)->double_p);
  mpz_init((*pri)->double_q);
  mpz_init((*pri)->Q_mul_double_p_inverse);
  mpz_init((*pri)->P_mul_double_q_inverse);

  uint32_t l_dividetwo = (*pub)->lbits / 2;
  uint32_t n_dividetwo = (*pub)->nbits / 2;

  do {
    aby_prng((*pri)->p_, n_dividetwo - l_dividetwo - 1);
    mpz_setbit((*pri)->p_, n_dividetwo - l_dividetwo - 1);
    mpz_nextprime((*pri)->p_, (*pri)->p_);

    aby_prng((*pri)->q_, n_dividetwo - l_dividetwo - 1);
    mpz_setbit((*pri)->q_, n_dividetwo - l_dividetwo - 1);
    mpz_nextprime((*pri)->q_, (*pri)->q_);

  } while (!mpz_cmp((*pri)->p_, (*pri)->q_));

  do {
    aby_prng((*pri)->p, l_dividetwo);
    mpz_setbit((*pri)->p, l_dividetwo);
    mpz_nextprime((*pri)->p, (*pri)->p);

    mpz_mul(temp, (*pri)->p, (*pri)->p_);
    mpz_mul_ui(temp, temp, 2);
    mpz_add_ui((*pri)->P, temp, 1);

    if (!mpz_probab_prime_p((*pri)->P, prob)) {
      continue;
    }
    break;

  } while (true);

  do {
    aby_prng((*pri)->q, l_dividetwo);
    mpz_setbit((*pri)->q, l_dividetwo);
    mpz_nextprime((*pri)->q, (*pri)->q);

    if (!mpz_cmp((*pri)->p, (*pri)->q)) {
      continue;
    }

    mpz_mul(temp, (*pri)->q, (*pri)->q_);
    mpz_mul_ui(temp, temp, 2);
    mpz_add_ui((*pri)->Q, temp, 1);

    if (!mpz_probab_prime_p((*pri)->Q, prob)) {
      continue;
    }
    break;

  } while (true);

  // alpha = p*q, beta = p_ * q_
  mpz_mul((*pri)->alpha, (*pri)->p, (*pri)->q);
  mpz_mul((*pri)->beta, (*pri)->p_, (*pri)->q_);

  // double_alpha = 2*alpha, double_beta = 2*beta
  mpz_mul_ui((*pri)->double_alpha, (*pri)->alpha, 2);
  mpz_mul_ui((*pri)->double_beta, (*pri)->beta, 2);

  // P_squared = P^2, Q_squared = Q^2
  mpz_mul((*pri)->P_squared, (*pri)->P, (*pri)->P);
  mpz_mul((*pri)->Q_squared, (*pri)->Q, (*pri)->Q);

  // double_p = p*2, double_q = q*2
  mpz_mul_ui((*pri)->double_p, (*pri)->p, 2);
  mpz_mul_ui((*pri)->double_q, (*pri)->q, 2);

  // n = P*Q, n_sqaured = n^2
  mpz_mul((*pub)->n, (*pri)->P, (*pri)->Q);
  mpz_mul((*pub)->n_squared, (*pub)->n, (*pub)->n);

  // half_n = n / 2, is plaintext domain
  mpz_div_ui((*pub)->half_n, (*pub)->n, 2);

  /* pick random y in Z_n^* */
  do {
    aby_prng(y, mpz_sizeinbase((*pub)->n, 2) + 128);
    mpz_mod(y, y, (*pub)->n);
    mpz_gcd(temp, y, (*pub)->n);
  } while (mpz_cmp_ui(temp, 1));

  // hs = (-y^(double_beta))^n mod n_squared
  mpz_powm((*pub)->h_s, y, (*pri)->double_beta, (*pub)->n);
  mpz_neg((*pub)->h_s, (*pub)->h_s);
  mpz_powm((*pub)->h_s, (*pub)->h_s, (*pub)->n, (*pub)->n_squared);

  // P_mul_P_inverse = P*(P^-1 mod Q)
  mpz_invert((*pri)->P_mul_P_inverse, (*pri)->P, (*pri)->Q);
  mpz_mul((*pri)->P_mul_P_inverse, (*pri)->P_mul_P_inverse, (*pri)->P);

  // P_squared_mul_P_squared_inverse = P^2 * (P^2)^-1 mod Q^2
  mpz_invert((*pri)->P_squared_mul_P_squared_inverse, (*pri)->P_squared, (*pri)->Q_squared);
  mpz_mul((*pri)->P_squared_mul_P_squared_inverse, (*pri)->P_squared_mul_P_squared_inverse,
          (*pri)->P_squared);

  // double_alpha^-1 mod n
  mpz_invert((*pri)->double_alpha_inverse, (*pri)->double_alpha, (*pub)->n);

  // Q_mul_double_p_inverse = (Q * double_p)^-1 mod P)
  mpz_mul((*pri)->Q_mul_double_p_inverse, (*pri)->Q, (*pri)->double_p);
  mpz_invert((*pri)->Q_mul_double_p_inverse, (*pri)->Q_mul_double_p_inverse, (*pri)->P);

  // P_mul_double_q_inverse = (P * double_q)^-1 mod Q)
  mpz_mul((*pri)->P_mul_double_q_inverse, (*pri)->P, (*pri)->double_q);
  mpz_invert((*pri)->P_mul_double_q_inverse, (*pri)->P_mul_double_q_inverse, (*pri)->Q);

  mpz_clears(temp, y, nullptr);

  /* init fixed-base */
  mpz_t base_P_sqaured, base_Q_sqaured;
  mpz_inits(base_P_sqaured, base_Q_sqaured, nullptr);
  mpz_mod(base_P_sqaured, (*pub)->h_s, (*pri)->P_squared);
  mpz_mod(base_Q_sqaured, (*pub)->h_s, (*pri)->Q_squared);
  fbpowmod_init_extend((*pub)->fb_mod_P_sqaured, base_P_sqaured, (*pri)->P_squared,
                       (*pub)->lbits + 1, 4);
  fbpowmod_init_extend((*pub)->fb_mod_Q_sqaured, base_Q_sqaured, (*pri)->Q_squared,
                       (*pub)->lbits + 1, 4);
  mpz_clears(base_P_sqaured, base_Q_sqaured, nullptr);
}

void opt_paillier_set_plaintext(mpz_t& mpz_plaintext, const char* plaintext,
                                const opt_public_key_t* pub, int radix) {
  mpz_set_str(mpz_plaintext, plaintext, radix);
  if (mpz_cmp_ui(mpz_plaintext, 0) < 0) {
    mpz_add(mpz_plaintext, mpz_plaintext, pub->n);
    mpz_mod(mpz_plaintext, mpz_plaintext, pub->n);
  }
}

void opt_paillier_get_plaintext(char*& plaintext, const mpz_t& mpz_plaintext,
                                const opt_public_key_t* pub, int radix) {
  mpz_t temp;
  mpz_init(temp);
  if (mpz_cmp(mpz_plaintext, pub->half_n) >= 0) {
    mpz_sub(temp, mpz_plaintext, pub->n);
    plaintext = mpz_get_str(nullptr, radix, temp);
  } else {
    plaintext = mpz_get_str(nullptr, radix, mpz_plaintext);
  }
  mpz_clear(temp);
}

bool validate_message(mpz_t& msg, opt_public_key_t* pub) {
  if (mpz_cmp_ui(msg, 0) < 0) {
    return false;
  }
  if (mpz_cmp(msg, pub->n) >= 0) {
    return false;
  }
  return true;
}

void opt_paillier_encrypt(mpz_t& res, const mpz_t& plaintext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri, const bool is_fb) {
  /* temp variable */
  mpz_t r;
  mpz_init(r);
  aby_prng(r, pub->lbits);
  // mpz_res = m*n
  mpz_mul(res, plaintext, pub->n);
  // mpz_res = (1+m*n)
  mpz_add_ui(res, res, 1);

  if (pri == nullptr) {
    // r = hs^r mod n^2
    mpz_powm(r, pub->h_s, r, pub->n_squared);
  } else {
    if (is_fb) {
      opt_paillier_mod_n_squared_crt_fb(r, r, pub, pri);
    } else {
      // r=hs^r mod n^2 => hs = hs mod p^2,q^2, r = r mod phi(n^2)
      opt_paillier_mod_n_squared_crt(r, pub->h_s, r, pub, pri);
    }
  }

  // mpz_res = (1+m*n)*hs^r mod n^2
  mpz_mul(res, res, r);
  mpz_mod(res, res, pub->n_squared);
  mpz_clear(r);
}

void opt_paillier_decrypt(mpz_t& res, const mpz_t& ciphertext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri, const bool is_crt) {
  if (is_crt) {
    /* temp variable and CRT variable */
    mpz_t temp_base, cp, cq;
    mpz_inits(cq, cp, temp_base, nullptr);

    // temp = c mod P^2
    mpz_mod(temp_base, ciphertext, pri->P_squared);
    // cp = temp_p^(2p) mod P^2
    mpz_powm(cp, temp_base, pri->double_p, pri->P_squared);
    // L(cp,P)
    mpz_sub_ui(cp, cp, 1);
    mpz_divexact(cp, cp, pri->P);
    // cp = cp * inv1 mod P
    mpz_mul(cp, cp, pri->Q_mul_double_p_inverse);
    mpz_mod(cp, cp, pri->P);

    // temp = c mod Q^2
    mpz_mod(temp_base, ciphertext, pri->Q_squared);
    // cq = temp_q^(2q) mod Q^2
    mpz_powm(cq, temp_base, pri->double_q, pri->Q_squared);
    // L(cq, Q)
    mpz_sub_ui(cq, cq, 1);
    mpz_divexact(cq, cq, pri->Q);
    // cq = cq * inv2 mod Q
    mpz_mul(cq, cq, pri->P_mul_double_q_inverse);
    mpz_mod(cq, cq, pri->Q);

    // cq = cq - cp
    mpz_sub(cq, cq, cp);
    // cp = cp + cq * (P * (P^-1 mod Q)) mod n
    mpz_addmul(cp, cq, pri->P_mul_P_inverse);
    mpz_mod(res, cp, pub->n);

    mpz_clears(cq, cp, temp_base, nullptr);
  } else {
    mpz_powm(res, ciphertext, pri->double_alpha, pub->n_squared);
    mpz_sub_ui(res, res, 1);
    mpz_divexact(res, res, pub->n);
    mpz_mul(res, res, pri->double_alpha_inverse);
    mpz_mod(res, res, pub->n);
  }
}

void opt_paillier_batch_encrypt(mpz_t* res, const mpz_t* plaintext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads, const bool is_fb) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_encrypt(res[i], plaintext[i], pub, pri, is_fb);  //
  });
}

void opt_paillier_batch_decrypt(mpz_t* res, const mpz_t* ciphertext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads, const bool is_crt) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_decrypt(res[i], ciphertext[i], pub, pri, is_crt);
  });
}

void opt_paillier_encrypt(mpz_t& res, const char* plaintext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri, int radix, const bool is_fb) {
  mpz_t temp;
  mpz_init(temp);
  opt_paillier_set_plaintext(temp, plaintext, pub, radix);
  opt_paillier_encrypt(res, temp, pub, pri, is_fb);
  mpz_clear(temp);
}

void opt_paillier_decrypt(char*& res, const mpz_t& ciphertext, const opt_public_key_t* pub,
                          const opt_private_key_t* pri, int radix, const bool is_crt) {
  mpz_t temp;
  mpz_init(temp);
  opt_paillier_decrypt(temp, ciphertext, pub, pri, is_crt);
  opt_paillier_get_plaintext(res, temp, pub, radix);
  mpz_clear(temp);
}

void opt_paillier_batch_encrypt(mpz_t* res, char** plaintext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads, int radix, const bool is_fb) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_encrypt(res[i], plaintext[i], pub, pri, radix, is_fb);  //
  });
}

void opt_paillier_batch_decrypt(char** res, const mpz_t* ciphertext, size_t size,
                                const opt_public_key_t* pub, const opt_private_key_t* pri,
                                int32_t n_threads, int radix, const bool is_crt) {
  xgboost::common::ParallelFor(size, n_threads, [&](int i) {
    opt_paillier_decrypt(res[i], ciphertext[i], pub, pri, radix, is_crt);
  });
}

void opt_paillier_add(mpz_t& res, const mpz_t& op1, const mpz_t& op2, const opt_public_key_t* pub) {
  mpz_mul(res, op1, op2);
  mpz_mod(res, res, pub->n_squared);
}

void opt_paillier_sub(mpz_t& res, const mpz_t& op1, const mpz_t& op2, const opt_public_key_t* pub) {
  mpz_set_si(res, -1);
  opt_paillier_constant_mul(res, op2, res, pub);
  mpz_mul(res, op1, res);
  mpz_mod(res, res, pub->n_squared);
}

void opt_paillier_batch_add(mpz_t& res, const mpz_t* ops, const size_t size,
                            const opt_public_key_t* pub, int32_t n_threads) {
  assert(size > 1);
  opt_paillier_add(res, ops[0], ops[1], pub);
  if (size > 2) {
    mutex g_mutex;
    xgboost::common::ParallelFor(size - 2, n_threads, [&](int i) {
      int j = i + 2;
      g_mutex.lock();
      opt_paillier_add(res, res, ops[j], pub);
      g_mutex.unlock();
    });
  }
}

void opt_paillier_constant_mul(mpz_t& res, const mpz_t& op1, const mpz_t& op2,
                               const opt_public_key_t* pub) {
  mpz_powm(res, op1, op2, pub->n_squared);
}

void opt_paillier_freepubkey(opt_public_key_t* pub) {
  mpz_clear(pub->h_s);
  mpz_clear(pub->n);
  mpz_clear(pub->n_squared);
  mpz_clear(pub->half_n);
  fbpowmod_end_extend(pub->fb_mod_P_sqaured);
  fbpowmod_end_extend(pub->fb_mod_Q_sqaured);
  free(pub);
  pub = nullptr;
}

void opt_paillier_freeprikey(opt_private_key_t* pri) {
  mpz_clear(pri->alpha);
  mpz_clear(pri->double_alpha_inverse);
  mpz_clear(pri->beta);
  mpz_clear(pri->double_alpha);
  mpz_clear(pri->double_beta);
  mpz_clear(pri->P);
  mpz_clear(pri->p);
  mpz_clear(pri->p_);
  mpz_clear(pri->P_mul_P_inverse);
  mpz_clear(pri->P_squared);
  mpz_clear(pri->P_squared_mul_P_squared_inverse);
  mpz_clear(pri->Q);
  mpz_clear(pri->q);
  mpz_clear(pri->q_);
  mpz_clear(pri->Q_squared);
  mpz_clear(pri->double_p);
  mpz_clear(pri->double_q);
  mpz_clear(pri->Q_mul_double_p_inverse);
  mpz_clear(pri->P_mul_double_q_inverse);
  free(pri);
  pri = nullptr;
}

//====================================datapack begin====================================
void init_crt(CrtMod** crtmod, const size_t crt_size, const mp_bitcnt_t mod_size) {
  (*crtmod) = (CrtMod*)malloc(sizeof(CrtMod));
  (*crtmod)->crt_size = crt_size;
  (*crtmod)->mod_size = mod_size;
  (*crtmod)->crt_mod = (mpz_t*)malloc(sizeof(mpz_t) * crt_size);
  (*crtmod)->crt_half_mod = (mpz_t*)malloc(sizeof(mpz_t) * crt_size);
  mpz_t cur;
  mpz_init(cur);
  aby_prng(cur, mod_size);
  mpz_setbit(cur, mod_size);

  for (size_t i = 0; i < crt_size; ++i) {
    mpz_init((*crtmod)->crt_mod[i]);
    mpz_init((*crtmod)->crt_half_mod[i]);

    mpz_nextprime((*crtmod)->crt_mod[i], cur);
    mpz_div_ui((*crtmod)->crt_half_mod[i], (*crtmod)->crt_mod[i], 2);
    mpz_set(cur, (*crtmod)->crt_mod[i]);
  }
  mpz_clear(cur);
}

void data_packing_crt(mpz_t& res, char** seq, const size_t seq_size, const CrtMod* crtmod,
                      const int radix) {
  data_packing_crt_t<const char*>(
      res, seq, seq_size, crtmod, nullptr,
      [](mpz_t& temp, const char* c, const opt_public_key_t* pub, const int radix) {
        mpz_set_str(temp, c, radix);
      },
      radix);
}

void data_retrieve_crt(char**& seq, const mpz_t& pack, const size_t data_size, const CrtMod* crtmod,
                       const opt_public_key_t* pub, const int radix) {
  data_retrieve_crt_t<char*>(
      seq, pack, data_size, crtmod, pub,
      [&](char*& p, const mpz_t& t, const opt_public_key_t* pub, const int radix) {
        opt_paillier_get_plaintext(p, t, pub, radix);
      },
      radix);
}

void free_crt(CrtMod* crtmod) {
  for (size_t i = 0; i < crtmod->crt_size; ++i) {
    mpz_clear(crtmod->crt_mod[i]);
    mpz_clear(crtmod->crt_half_mod[i]);
  }
  free(crtmod->crt_mod);
  crtmod->crt_mod = nullptr;
  free(crtmod->crt_half_mod);
  crtmod->crt_half_mod = nullptr;
  free(crtmod);
  crtmod = nullptr;
}
//====================================datapack end======================================