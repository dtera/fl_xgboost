/*!
 * Copyright (c) 2015 by Contributors
 * \file base.h
 * \brief defines configuration macros of xgboost.
 */
#ifndef XGBOOST_BASE_H_
#define XGBOOST_BASE_H_

#include <dmlc/base.h>
#include <dmlc/omp.h>
#include <gmp.h>

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "opt_paillier.h"

/*!
 * \brief string flag for R library, to leave hooks when needed.
 */
#ifndef XGBOOST_STRICT_R_MODE
#define XGBOOST_STRICT_R_MODE 0
#endif  // XGBOOST_STRICT_R_MODE

/*!
 * \brief Whether always log console message with time.
 *  It will display like, with timestamp appended to head of the message.
 *  "[21:47:50] 6513x126 matrix with 143286 entries loaded from
 * ../data/agaricus.txt.train"
 */
#ifndef XGBOOST_LOG_WITH_TIME
#define XGBOOST_LOG_WITH_TIME 1
#endif  // XGBOOST_LOG_WITH_TIME

/*!
 * \brief Whether customize the logger outputs.
 */
#ifndef XGBOOST_CUSTOMIZE_LOGGER
#define XGBOOST_CUSTOMIZE_LOGGER XGBOOST_STRICT_R_MODE
#endif  // XGBOOST_CUSTOMIZE_LOGGER

/*!
 * \brief Whether to customize global PRNG.
 */
#ifndef XGBOOST_CUSTOMIZE_GLOBAL_PRNG
#define XGBOOST_CUSTOMIZE_GLOBAL_PRNG XGBOOST_STRICT_R_MODE
#endif  // XGBOOST_CUSTOMIZE_GLOBAL_PRNG

/*!
 * \brief Check if alignas(*) keyword is supported. (g++ 4.8 or higher)
 */
#if defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || __GNUC__ > 4)
#define XGBOOST_ALIGNAS(X) alignas(X)
#else
#define XGBOOST_ALIGNAS(X)
#endif  // defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || __GNUC__ > 4)

#if defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || __GNUC__ > 4) && \
    !defined(__CUDACC__) && !defined(__sun) && !defined(sun)
#include <parallel/algorithm>
#define XGBOOST_PARALLEL_SORT(X, Y, Z) __gnu_parallel::sort((X), (Y), (Z))
#define XGBOOST_PARALLEL_STABLE_SORT(X, Y, Z) __gnu_parallel::stable_sort((X), (Y), (Z))
#elif defined(_MSC_VER) && (!__INTEL_COMPILER)
#include <ppl.h>
#define XGBOOST_PARALLEL_SORT(X, Y, Z) concurrency::parallel_sort((X), (Y), (Z))
#define XGBOOST_PARALLEL_STABLE_SORT(X, Y, Z) std::stable_sort((X), (Y), (Z))
#else
#define XGBOOST_PARALLEL_SORT(X, Y, Z) std::sort((X), (Y), (Z))
#define XGBOOST_PARALLEL_STABLE_SORT(X, Y, Z) std::stable_sort((X), (Y), (Z))
#endif  // GLIBC VERSION

#if defined(__GNUC__)
#define XGBOOST_EXPECT(cond, ret) __builtin_expect((cond), (ret))
#else
#define XGBOOST_EXPECT(cond, ret) (cond)
#endif  // defined(__GNUC__)

/*!
 * \brief Tag function as usable by device
 */
#if defined(__CUDA__) || defined(__NVCC__)
#define XGBOOST_DEVICE __host__ __device__
#else
#define XGBOOST_DEVICE
#endif  // defined (__CUDA__) || defined(__NVCC__)

#if defined(__CUDA__) || defined(__CUDACC__)
#define XGBOOST_HOST_DEV_INLINE XGBOOST_DEVICE __forceinline__
#define XGBOOST_DEV_INLINE __device__ __forceinline__
#else
#define XGBOOST_HOST_DEV_INLINE
#define XGBOOST_DEV_INLINE
#endif  // defined(__CUDA__) || defined(__CUDACC__)

// These check are for Makefile.
#if !defined(XGBOOST_MM_PREFETCH_PRESENT) && !defined(XGBOOST_BUILTIN_PREFETCH_PRESENT)
/* default logic for software pre-fetching */
#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_AMD64))) || defined(__INTEL_COMPILER)
// Enable _mm_prefetch for Intel compiler and MSVC+x86
#define XGBOOST_MM_PREFETCH_PRESENT
#define XGBOOST_BUILTIN_PREFETCH_PRESENT
#elif defined(__GNUC__)
// Enable __builtin_prefetch for GCC
#define XGBOOST_BUILTIN_PREFETCH_PRESENT
#endif  // GUARDS

#endif  // !defined(XGBOOST_MM_PREFETCH_PRESENT) && !defined()

//====================================EncryptedType begin===============================
template <class T = float>
class EncryptedType {
 public:
  // encrypted data
  mpz_t data_;
  uint32_t mul_cnt_{0};
  static opt_public_key_t* pub;

  XGBOOST_DEVICE EncryptedType() : EncryptedType(0) {}

  XGBOOST_DEVICE EncryptedType(const mpz_t& data) {
    mpz_init(data_);
    SetCipherData(data);
  }

  XGBOOST_DEVICE EncryptedType(const EncryptedType& g) {
    mpz_init(data_);
    SetCipherData(g.data_);
  }

  template <class PLAIN_TYPE>
  XGBOOST_DEVICE explicit EncryptedType(PLAIN_TYPE value) {
    mpz_init(data_);
    if (pub != nullptr) {
      opt_paillier_encrypt_t(data_, value, pub);
    }
  }

  template <class PLAIN_TYPE>
  XGBOOST_DEVICE void SetData(PLAIN_TYPE value) {
    if (pub != nullptr) {
      opt_paillier_encrypt_t(data_, value, pub);
      mul_cnt_ = 0;
    }
  }

  XGBOOST_DEVICE void SetCipherData(const mpz_t& data) {
    mpz_set(data_, data);
    mul_cnt_ = 0;
  }

  XGBOOST_DEVICE EncryptedType& operator+=(const EncryptedType<float>& et) {
    opt_paillier_add(data_, data_, et.data_, pub);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType& operator+=(const EncryptedType<double>& et) {
    opt_paillier_add(data_, data_, et.data_, pub);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator+(const EncryptedType<float>& et) const {
    EncryptedType g;
    opt_paillier_add(g.data_, data_, et.data_, pub);
    return g;
  }

  XGBOOST_DEVICE EncryptedType operator+(const EncryptedType<double>& et) const {
    EncryptedType g;
    opt_paillier_add(g.data_, data_, et.data_, pub);
    return g;
  }

  XGBOOST_DEVICE EncryptedType& operator-=(const EncryptedType<float>& et) {
    opt_paillier_sub(data_, data_, et.data_, pub);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType& operator-=(const EncryptedType<double>& et) {
    opt_paillier_sub(data_, data_, et.data_, pub);
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator-(const EncryptedType<float>& et) const {
    EncryptedType g;
    opt_paillier_sub(g.data_, data_, et.data_, pub);
    return g;
  }

  XGBOOST_DEVICE EncryptedType operator-(const EncryptedType<double>& et) const {
    EncryptedType g;
    opt_paillier_sub(g.data_, data_, et.data_, pub);
    return g;
  }

  XGBOOST_DEVICE EncryptedType& operator*=(float multiplier) {
    opt_paillier_constant_mul_t(data_, data_, multiplier, pub);
    mul_cnt_++;
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator*(float multiplier) const {
    EncryptedType g;
    opt_paillier_constant_mul_t(g.data_, data_, multiplier, pub);
    g.mul_cnt_++;
    return g;
  }

  XGBOOST_DEVICE EncryptedType& operator/=(float divisor) {
    opt_paillier_constant_mul_t(data_, data_, 1 / divisor, pub);
    mul_cnt_++;
    return *this;
  }

  XGBOOST_DEVICE EncryptedType operator/(float divisor) const {
    EncryptedType g;
    opt_paillier_constant_mul_t(g.data_, data_, 1 / divisor, pub);
    g.mul_cnt_++;
    return g;
  }

  XGBOOST_DEVICE bool operator==(const EncryptedType& et) const {
    return mpz_cmp(this->data_, et.data_) == 0;
  }

  friend std::ostream& operator<<(std::ostream& os, const EncryptedType& g) {
    if (std::is_same<T, char>() || std::is_same<T, short>() || std::is_same<T, int>() ||
        std::is_same<T, uint8_t>() || std::is_same<T, uint16_t>() || std::is_same<T, uint32_t>() ||
        std::is_same<T, int64_t>() || std::is_same<T, uint64_t>() || std::is_same<T, double>() ||
        std::is_same<T, float>()) {
      T o;
      opt_paillier_get_plaintext_t(o, g.data_, pub, 8 * (g.mul_cnt_ + 1));
      os << o;
    } else {
      char* o;
      opt_paillier_get_plaintext(o, g.data_, pub);
      os << o;
    }

    return os;
  }
};

template <class T>
opt_public_key_t* EncryptedType<T>::pub = nullptr;
//====================================EncryptedType end=================================

/*! \brief namespace of xgboost*/
namespace xgboost {

/*! \brief unsigned integer type used for feature index. */
using bst_uint = uint32_t;  // NOLINT
/*! \brief integer type. */
using bst_int = int32_t;  // NOLINT
/*! \brief unsigned long integers */
using bst_ulong = uint64_t;  // NOLINT
/*! \brief float type, used for storing statistics */
using bst_float = float;  // NOLINT
/*! \brief Categorical value type. */
using bst_cat_t = int32_t;  // NOLINT
/*! \brief Type for data column (feature) index. */
using bst_feature_t = uint32_t;  // NOLINT
/*! \brief Type for histogram bin index. */
using bst_bin_t = int32_t;  // NOLINT
/*! \brief Type for data row index.
 *
 * Be careful `std::size_t' is implementation-defined.  Meaning that the binary
 * representation of DMatrix might not be portable across platform.  Booster model should
 * be portable as parameters are floating points.
 */
using bst_row_t = std::size_t;  // NOLINT
/*! \brief Type for tree node index. */
using bst_node_t = int32_t;  // NOLINT
/*! \brief Type for ranking group index. */
using bst_group_t = uint32_t;  // NOLINT

namespace detail {
/*! \brief Implementation of gradient statistics pair. Template specialisation
 * may be used to overload different gradients types e.g. low precision, high
 * precision, integer, floating point. */
template <typename T>
class GradientPairInternal {
  /*! \brief gradient statistics */
  T grad_;
  /*! \brief second order gradient statistics */
  T hess_;

 public:
  using ValueT = T;

  XGBOOST_DEVICE inline void SetGrad(T g) { grad_ = g; }

  XGBOOST_DEVICE inline void SetHess(T h) { hess_ = h; }

  inline void Add(const ValueT& grad, const ValueT& hess) {
    grad_ += grad;
    hess_ += hess;
  }

  inline void EncryptedAdd(const EncryptedType<float>& grad, const EncryptedType<float>& hess) {
    grad_ += grad;
    hess_ += hess;
  }

  inline static void Reduce(GradientPairInternal<T>& a,
                            const GradientPairInternal<T>& b) {  // NOLINT(*)
    a += b;
  }

  XGBOOST_DEVICE GradientPairInternal() : grad_(0), hess_(0) {}

  XGBOOST_DEVICE GradientPairInternal(T grad, T hess) {
    SetGrad(grad);
    SetHess(hess);
  }

  // Copy constructor if of same value type, marked as default to be trivially_copyable
  GradientPairInternal(const GradientPairInternal<T>& g) = default;

  // Copy constructor if different value type - use getters and setters to
  // perform conversion
  template <typename T2>
  XGBOOST_DEVICE explicit GradientPairInternal(const GradientPairInternal<T2>& g) {
    SetGrad(g.GetGrad());
    SetHess(g.GetHess());
  }

  XGBOOST_DEVICE T GetGrad() const { return grad_; }
  XGBOOST_DEVICE T GetHess() const { return hess_; }

  XGBOOST_DEVICE GradientPairInternal<T>& operator+=(const GradientPairInternal<T>& rhs) {
    grad_ += rhs.grad_;
    hess_ += rhs.hess_;
    return *this;
  }

  XGBOOST_DEVICE GradientPairInternal<T> operator+(const GradientPairInternal<T>& rhs) const {
    GradientPairInternal<T> g;
    g.grad_ = grad_ + rhs.grad_;
    g.hess_ = hess_ + rhs.hess_;
    return g;
  }

  XGBOOST_DEVICE GradientPairInternal<T>& operator-=(const GradientPairInternal<T>& rhs) {
    grad_ -= rhs.grad_;
    hess_ -= rhs.hess_;
    return *this;
  }

  XGBOOST_DEVICE GradientPairInternal<T> operator-(const GradientPairInternal<T>& rhs) const {
    GradientPairInternal<T> g;
    g.grad_ = grad_ - rhs.grad_;
    g.hess_ = hess_ - rhs.hess_;
    return g;
  }

  XGBOOST_DEVICE GradientPairInternal<T>& operator*=(float multiplier) {
    grad_ *= multiplier;
    hess_ *= multiplier;
    return *this;
  }

  XGBOOST_DEVICE GradientPairInternal<T> operator*(float multiplier) const {
    GradientPairInternal<T> g;
    g.grad_ = grad_ * multiplier;
    g.hess_ = hess_ * multiplier;
    return g;
  }

  XGBOOST_DEVICE GradientPairInternal<T>& operator/=(float divisor) {
    grad_ /= divisor;
    hess_ /= divisor;
    return *this;
  }

  XGBOOST_DEVICE GradientPairInternal<T> operator/(float divisor) const {
    GradientPairInternal<T> g;
    g.grad_ = grad_ / divisor;
    g.hess_ = hess_ / divisor;
    return g;
  }

  XGBOOST_DEVICE bool operator==(const GradientPairInternal<T>& rhs) const {
    return grad_ == rhs.grad_ && hess_ == rhs.hess_;
  }

  XGBOOST_DEVICE explicit GradientPairInternal(int value) {
    *this = GradientPairInternal<T>(static_cast<T>(value), static_cast<T>(value));
  }

  friend std::ostream& operator<<(std::ostream& os, const GradientPairInternal<T>& g) {
    os << g.GetGrad() << "/" << g.GetHess();
    return os;
  }
};
}  // namespace detail

template <typename T>
using GradientPairT = detail::GradientPairInternal<T>;

/*! \brief gradient statistics pair usually needed in gradient boosting */
using GradientPair = detail::GradientPairInternal<float>;
/*! \brief High precision gradient statistics pair */
using GradientPairPrecise = detail::GradientPairInternal<double>;

/*! \brief encrypted gradient statistics pair usually needed in gradient boosting */
using EncryptedGradientPair = detail::GradientPairInternal<EncryptedType<float>>;
/*! \brief High precision encrypted gradient statistics pair usually needed in gradient boosting */
using EncryptedGradientPairPrecise = detail::GradientPairInternal<EncryptedType<double>>;

/*! \brief Fixed point representation for high precision gradient pair. Has a different interface so
 * we don't accidentally use it in gain calculations.*/
class GradientPairInt64 {
  using T = int64_t;
  T grad_ = 0;
  T hess_ = 0;

 public:
  using ValueT = T;

  XGBOOST_DEVICE GradientPairInt64(T grad, T hess) : grad_(grad), hess_(hess) {}
  GradientPairInt64() = default;

  // Copy constructor if of same value type, marked as default to be trivially_copyable
  GradientPairInt64(const GradientPairInt64& g) = default;

  XGBOOST_DEVICE T GetQuantisedGrad() const { return grad_; }
  XGBOOST_DEVICE T GetQuantisedHess() const { return hess_; }

  XGBOOST_DEVICE GradientPairInt64& operator+=(const GradientPairInt64& rhs) {
    grad_ += rhs.grad_;
    hess_ += rhs.hess_;
    return *this;
  }

  XGBOOST_DEVICE GradientPairInt64 operator+(const GradientPairInt64& rhs) const {
    GradientPairInt64 g;
    g.grad_ = grad_ + rhs.grad_;
    g.hess_ = hess_ + rhs.hess_;
    return g;
  }

  XGBOOST_DEVICE GradientPairInt64& operator-=(const GradientPairInt64& rhs) {
    grad_ -= rhs.grad_;
    hess_ -= rhs.hess_;
    return *this;
  }

  XGBOOST_DEVICE GradientPairInt64 operator-(const GradientPairInt64& rhs) const {
    GradientPairInt64 g;
    g.grad_ = grad_ - rhs.grad_;
    g.hess_ = hess_ - rhs.hess_;
    return g;
  }

  XGBOOST_DEVICE bool operator==(const GradientPairInt64& rhs) const {
    return grad_ == rhs.grad_ && hess_ == rhs.hess_;
  }
  friend std::ostream& operator<<(std::ostream& os, const GradientPairInt64& g) {
    os << g.GetQuantisedGrad() << "/" << g.GetQuantisedHess();
    return os;
  }
};

using Args = std::vector<std::pair<std::string, std::string>>;

/*! \brief small eps gap for minimum split decision. */
constexpr bst_float kRtEps = 1e-6f;

/*! \brief define unsigned long for openmp loop */
using omp_ulong = dmlc::omp_ulong;  // NOLINT
/*! \brief define unsigned int for openmp loop */
using bst_omp_uint = dmlc::omp_uint;  // NOLINT
/*! \brief Type used for representing version number in binary form.*/
using XGBoostVersionT = int32_t;

/*!
 * \brief define compatible keywords in g++
 *  Used to support g++-4.6 and g++4.7
 */
#if DMLC_USE_CXX11 && defined(__GNUC__) && !defined(__clang_version__)
#if __GNUC__ == 4 && __GNUC_MINOR__ < 8
#define override
#define final
#endif  // __GNUC__ == 4 && __GNUC_MINOR__ < 8
#endif  // DMLC_USE_CXX11 && defined(__GNUC__) && !defined(__clang_version__)
}  // namespace xgboost

template <typename T>
void opt_paillier_encrypt(xgboost::detail::GradientPairInternal<EncryptedType<T>>& res,
                          const xgboost::detail::GradientPairInternal<T>& plaintext,
                          const opt_public_key_t* pub, const opt_private_key_t* pri = nullptr,
                          int precision = 8, int radix = 10, const bool is_fb = true) {
  mpz_t t1, t2;
  mpz_inits(t1, t2, nullptr);
  opt_paillier_encrypt_t(t1, plaintext.GetGrad(), pub, pri, precision, radix, is_fb);
  res.SetGrad(EncryptedType<T>(t1));
  opt_paillier_encrypt_t(t2, plaintext.GetHess(), pub, pri, precision, radix, is_fb);
  res.SetHess(EncryptedType<T>(t2));
  mpz_clears(t1, t2, nullptr);
}

template <typename T>
void opt_paillier_decrypt(xgboost::detail::GradientPairInternal<T>& res,
                          const xgboost::detail::GradientPairInternal<EncryptedType<T>>& ciphertext,
                          const opt_public_key_t* pub, const opt_private_key_t* pri,
                          int precision = 8, int radix = 10, const bool is_crt = true) {
  T t1, t2;
  opt_paillier_decrypt_t(t1, ciphertext.GetGrad().data_, pub, pri, precision, radix, is_crt);
  opt_paillier_decrypt_t(t2, ciphertext.GetHess().data_, pub, pri, precision, radix, is_crt);
  res.SetGrad(t1);
  res.SetHess(t2);
}

template <typename T>
void opt_paillier_batch_encrypt(
    std::vector<xgboost::detail::GradientPairInternal<EncryptedType<T>>>& res,
    const std::vector<xgboost::detail::GradientPairInternal<T>>& plaintexts,
    const opt_public_key_t* pub, const opt_private_key_t* pri = nullptr,
    int32_t n_threads = omp_get_num_procs(), int precision = 8, int radix = 10,
    const bool is_fb = true) {
  ParallelFor(plaintexts.size(), n_threads, [&](int i) {
    opt_paillier_encrypt(res[i], plaintexts[i], pub, pri, precision, radix, is_fb);
  });
}

template <typename T>
void opt_paillier_batch_decrypt(
    std::vector<xgboost::detail::GradientPairInternal<T>>& res,
    const std::vector<xgboost::detail::GradientPairInternal<EncryptedType<T>>>& ciphertexts,
    const opt_public_key_t* pub, const opt_private_key_t* pri,
    int32_t n_threads = omp_get_num_procs(), int precision = 8, int radix = 10,
    const bool is_crt = true) {
  ParallelFor(ciphertexts.size(), n_threads, [&](int i) {
    opt_paillier_decrypt(res[i], ciphertexts[i], pub, pri, precision, radix, is_crt);
  });
}

#endif  // XGBOOST_BASE_H_
