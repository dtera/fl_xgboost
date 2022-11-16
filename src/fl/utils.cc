//
// Created by HqZhao on 2022/11/15.
//
#include "fl/utils.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>

#define POWMOD_DEBUG 0

#if POWMOD_DEBUG
#include <cstdio>
#endif

#define ROUNDUP(a, b) ((a)-1) / (b) + 1

/**
 * @brief arbitrary sizes of slide-window
 *
 * functions:
 *    fbpowmod_init_extend -> init a fixed-base instance
 *    fbpowmod_extend      -> implemention of powmod with fixed-base
 *    fbpowmod_end_extend  -> free memory after malloc
 */
void fbpowmod_init_extend(fb_instance& fb_ins, const mpz_t base, const mpz_t mod, size_t maxbits,
                          size_t winsize) {
  fb_ins.m_w = winsize;
  fb_ins.m_h = (1 << winsize);
  fb_ins.m_t = ROUNDUP(maxbits, winsize);
  fb_ins.m_table_G = (mpz_t*)malloc(sizeof(mpz_t) * (fb_ins.m_t + 1));
  mpz_t m_bi;
  mpz_init(m_bi);
  mpz_init(fb_ins.m_mod);
  mpz_set(fb_ins.m_mod, mod);
  for (size_t i = 0; i <= fb_ins.m_t; ++i) {
    mpz_init(fb_ins.m_table_G[i]);
    mpz_ui_pow_ui(m_bi, (uint32_t)fb_ins.m_h, i);
    mpz_powm(fb_ins.m_table_G[i], base, m_bi, mod);
  }
  mpz_clear(m_bi);
}

void fbpowmod_extend(const fb_instance& fb_ins, mpz_t result, const mpz_t exp) {
  uint32_t* m_e = (uint32_t*)malloc(sizeof(uint32_t) * (fb_ins.m_t + 1));
  mpz_t m_exp, temp;
  mpz_inits(m_exp, temp, nullptr);
  mpz_set(m_exp, exp);
  size_t t = 0;
  for (; mpz_cmp_ui(m_exp, 0) > 0; ++t) {
    // mpz_tdiv_r_2exp(temp, m_exp, fb_ins.m_w);
    m_e[t] = mpz_mod_ui(temp, m_exp, fb_ins.m_h);
    mpz_tdiv_q_2exp(m_exp, m_exp, fb_ins.m_w);
    // m_e[t] = mpz_get_ui(temp);
  }
  mpz_set_ui(temp, 1);
  mpz_set_ui(result, 1);
  for (size_t j = fb_ins.m_h - 1; j >= 1; --j) {
    for (size_t i = 0; i < t; ++i) {
      if (m_e[i] == j) {
        mpz_mul(temp, temp, fb_ins.m_table_G[i]);
        mpz_mod(temp, temp, fb_ins.m_mod);
      }
    }
    mpz_mul(result, result, temp);
    mpz_mod(result, result, fb_ins.m_mod);
  }
  mpz_clears(m_exp, temp, nullptr);
  free(m_e);
  m_e = nullptr;
}

void fbpowmod_end_extend(fb_instance& fb_ins) {
  for (size_t i = 0; i <= fb_ins.m_t; ++i) {
    mpz_clear(fb_ins.m_table_G[i]);
  }
  mpz_clear(fb_ins.m_mod);
  free(fb_ins.m_table_G);
  fb_ins.m_table_G = nullptr;
}

// this is bad, fix occurrences of ceil_log2 and replace by ceil_log2_min1 where log(1) = 1 is
// necessary. For all else use ceil_log2_real
uint32_t ceil_log2(int bits) {
  if (bits == 1) return 1;
  int targetlevel = 0, bitstemp = bits;
  while (bitstemp >>= 1) ++targetlevel;
  return targetlevel + ((1 << targetlevel) < bits);
}

uint32_t ceil_log2_min1(int bits) {
  if (bits <= 1) return 1;
  int targetlevel = 0, bitstemp = bits;
  while (bitstemp >>= 1) ++targetlevel;
  return targetlevel + ((1 << targetlevel) < bits);
}

uint32_t ceil_log2_real(int bits) {
  if (bits == 1) return 0;
  int targetlevel = 0, bitstemp = bits;
  while (bitstemp >>= 1) ++targetlevel;
  return targetlevel + ((1 << targetlevel) < bits);
}

uint32_t floor_log2(int bits) {
  if (bits == 1) return 1;
  int targetlevel = 0;
  while (bits >>= 1) ++targetlevel;
  return targetlevel;
}

/**
 * returns a 4-byte value from dev/random
 */
uint32_t aby_rand() {
  int frandom = open("/dev/random", O_RDONLY);
  if (frandom < 0) {
    std::cerr << "Error in opening /dev/random: utils.h:aby_rand()" << std::endl;
    exit(1);
  } else {
    char data[4];
    size_t len = 0;
    while (len < sizeof data) {
      ssize_t result = read(frandom, data + len, (sizeof data) - len);
      if (result < 0) {
        std::cerr << "Error in generating random number: utils.h:aby_rand()" << std::endl;
        exit(1);
      }
      len += result;
    }
    close(frandom);
    return *((uint32_t*)data);
  }
  return 0;
}

/**
 * returns a random mpz_t with bitlen len generated from dev/urandom
 */
void aby_prng(mpz_t rnd, mp_bitcnt_t bitlen) {
  size_t byte_count = ceil_divide(bitlen, 8);
  char* data;

  int furandom = open("/dev/urandom", O_RDONLY);
  if (furandom < 0) {
    std::cerr << "Error in opening /dev/urandom: utils.cpp:aby_prng()" << std::endl;
    exit(1);
  } else {
    data = (char*)malloc(sizeof(*data) * byte_count);
    size_t len = 0;
    while (len < byte_count) {
      ssize_t result = read(furandom, data + len, byte_count - len);
      if (result < 0) {
        std::cerr << "Error in generating random number: utils.cpp:aby_prng()" << std::endl;
        exit(1);
      }
      len += result;
    }
    close(furandom);
  }

  mpz_import(rnd, byte_count, 1, sizeof(*data), 0, 0, data);

  // set MSBs to zero, if we are not working on full bytes
  if (bitlen % 8) {
    for (uint8_t i = 0; i < 8 - bitlen % 8; ++i) {
      mpz_clrbit(rnd, byte_count * 8 - i - 1);
    }
  }

  free(data);
}