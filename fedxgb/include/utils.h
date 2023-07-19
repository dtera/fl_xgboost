//
// Created by HqZhao on 2022/11/15.
//

#ifndef DEMO_UTILS_H
#define DEMO_UTILS_H

#include <gmp.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>

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

#define TIME_STAT(statments, name)                                                        \
  {                                                                                       \
    auto start = std::chrono::high_resolution_clock::now();                               \
    statments;                                                                            \
    auto end = std::chrono::high_resolution_clock::now();                                 \
    double cost =                                                                         \
        1.0 * std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(); \
    std::cout << #name << " costs: " << cost / 1000.0 << " ms." << std::endl;             \
  }

void repeat(
    std::function<void(int)> fn, std::size_t n, std::function<void()> before = []() {},
    std::function<void()> after = []() {});

/**
 * OpenMP schedule
 */
struct Sched {
  enum {
    kAuto,
    kDynamic,
    kStatic,
    kGuided,
  } sched;
  size_t chunk{0};

  Sched static Auto() { return Sched{kAuto}; }

  Sched static Dyn(size_t n = 0) { return Sched{kDynamic, n}; }

  Sched static Static(size_t n = 0) { return Sched{kStatic, n}; }

  Sched static Guided() { return Sched{kGuided}; }
};

/*!
 * \brief OMP Exception class catches, saves and rethrows exception from OMP blocks
 */
class OMPException {
 private:
  // exception_ptr member to store the exception
  std::exception_ptr omp_exception_;
  // mutex to be acquired during catch to set the exception_ptr
  std::mutex mutex_;

 public:
  /*!
   * \brief Parallel OMP blocks should be placed within Run to save exception
   */
  template <typename Function, typename... Parameters>
  void Run(Function f, Parameters... params) {
    try {
      f(params...);
    } catch (std::runtime_error& ex) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!omp_exception_) {
        omp_exception_ = std::current_exception();
      }
    } catch (std::exception& ex) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!omp_exception_) {
        omp_exception_ = std::current_exception();
      }
    }
  }

  /*!
   * \brief should be called from the main thread to rethrow the exception
   */
  void Rethrow() {
    if (this->omp_exception_) std::rethrow_exception(this->omp_exception_);
  }
};

template <typename Index, typename Func>
void ParallelFor(Index size, int32_t n_threads, Sched sched, Func fn) {
#if defined(_MSC_VER)
  // msvc doesn't support unsigned integer as openmp index.
  using OmpInd = std::conditional_t<std::is_signed<Index>::value, Index, omp_ulong>;
#else
  using OmpInd = Index;
#endif
  OmpInd length = static_cast<OmpInd>(size);
  assert(n_threads >= 1);

  OMPException exc;
  switch (sched.sched) {
    case Sched::kAuto: {
#pragma omp parallel for num_threads(n_threads)
      for (OmpInd i = 0; i < length; ++i) {
        exc.Run(fn, i);
      }
      break;
    }
    case Sched::kDynamic: {
      if (sched.chunk == 0) {
#pragma omp parallel for num_threads(n_threads) schedule(dynamic)
        for (OmpInd i = 0; i < length; ++i) {
          exc.Run(fn, i);
        }
      } else {
#pragma omp parallel for num_threads(n_threads) schedule(dynamic, sched.chunk)
        for (OmpInd i = 0; i < length; ++i) {
          exc.Run(fn, i);
        }
      }
      break;
    }
    case Sched::kStatic: {
      if (sched.chunk == 0) {
#pragma omp parallel for num_threads(n_threads) schedule(static)
        for (OmpInd i = 0; i < length; ++i) {
          exc.Run(fn, i);
        }
      } else {
#pragma omp parallel for num_threads(n_threads) schedule(static, sched.chunk)
        for (OmpInd i = 0; i < length; ++i) {
          exc.Run(fn, i);
        }
      }
      break;
    }
    case Sched::kGuided: {
#pragma omp parallel for num_threads(n_threads) schedule(guided)
      for (OmpInd i = 0; i < length; ++i) {
        exc.Run(fn, i);
      }
      break;
    }
  }
  exc.Rethrow();
}

template <typename Index, typename Func>
void ParallelFor(Index size, int32_t n_threads, Func fn) {
  ParallelFor(size, n_threads, Sched::Static(), fn);
}

#endif  // DEMO_UTILS_H
