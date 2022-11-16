//
// Created by HqZhao on 2022/11/15.
//

#ifndef DEMO_UTILS_H
#define DEMO_UTILS_H
#include <gmp.h>

#include <chrono>
#include <cstdint>

struct fb_instance {
  mpz_t m_mod;
  mpz_t* m_table_G;
  size_t m_h;
  size_t m_t;
  size_t m_w;
};

void fbpowmod_init_extend(fb_instance& fb_ins, const mpz_t base, const mpz_t mod, size_t bitsize,
                          size_t winsize);

void fbpowmod_extend(const fb_instance& fb_ins, mpz_t result, const mpz_t exp);

void fbpowmod_end_extend(fb_instance& fb_ins);

#ifdef WIN32
#define SleepMiliSec(x) Sleep(x)
#else
#define SleepMiliSec(x) usleep((x) << 10)
#endif

#define two_pow(e) (((uint64_t)1) << (e))

#define pad_to_power_of_two(e) (((uint64_t)1) << (ceil_log2(e)))

/*compute (a-b) mod (m+1) as: b > a ? (m) - (b-1) + a : a - b	*/
#define MOD_SUB(a, b, m) ((((b) > (a)) ? (m) - ((b)-1) + a : a - b))

#define ceil_divide(x, y) ((((x) + (y)-1) / (y)))
#define bits_in_bytes(bits) (ceil_divide((bits), 8))
#define pad_to_multiple(x, y) (ceil_divide(x, y) * (y))

#define PadToRegisterSize(x) (PadToMultiple(x, OTEXT_BLOCK_SIZE_BITS))
#define PadToMultiple(x, y) (ceil_divide(x, y) * (y))

// this is bad, fix occurrences of ceil_log2 and replace by ceil_log2_min1 where log(1) = 1 is
// necessary. For all else use ceil_log2_real
uint32_t ceil_log2(int bits);

uint32_t ceil_log2_min1(int bits);

uint32_t ceil_log2_real(int bits);

uint32_t floor_log2(int bits);

/**
 * returns a 4-byte value from dev/random
 */
uint32_t aby_rand();

/**
 * returns a random mpz_t with bitlen len generated from dev/urandom
 */
void aby_prng(mpz_t rnd, mp_bitcnt_t len);

#define TIME_STAT(statments, name)                                                      \
  auto start = chrono::high_resolution_clock::now();                                    \
  statments;                                                                            \
  auto end = chrono::high_resolution_clock::now();                                      \
  double cost = 1.0 * chrono::duration_cast<chrono::microseconds>(end - start).count(); \
  cout << #name << " costs: " << cost / 1000.0 << " ms." << endl;

#endif  // DEMO_UTILS_H
