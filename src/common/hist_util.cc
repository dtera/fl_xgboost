/*!
 * Copyright 2017-2020 by Contributors
 * \file hist_util.cc
 */
#include "hist_util.h"

#include <dmlc/timer.h>

#include <vector>

#include "../common/common.h"
#include "column_matrix.h"
#include "quantile.h"
#include "xgboost/base.h"

#if defined(XGBOOST_MM_PREFETCH_PRESENT)
#include <xmmintrin.h>
#define PREFETCH_READ_T0(addr) _mm_prefetch(reinterpret_cast<const char *>(addr), _MM_HINT_T0)
#elif defined(XGBOOST_BUILTIN_PREFETCH_PRESENT)
#define PREFETCH_READ_T0(addr) __builtin_prefetch(reinterpret_cast<const char *>(addr), 0, 3)
#else  // no SW pre-fetching available; PREFETCH_READ_T0 is no-op
#define PREFETCH_READ_T0(addr) \
  do {                         \
  } while (0)
#endif  // defined(XGBOOST_MM_PREFETCH_PRESENT)

namespace xgboost {
namespace common {

HistogramCuts::HistogramCuts() { cut_ptrs_.HostVector().emplace_back(0); }

HistogramCuts SketchOnDMatrix(DMatrix *m, int32_t max_bins, int32_t n_threads, bool use_sorted,
                              Span<float> const hessian) {
  HistogramCuts out;
  auto const &info = m->Info();
  std::vector<bst_row_t> reduced(info.num_col_, 0);
  for (auto const &page : m->GetBatches<SparsePage>()) {
    auto const &entries_per_column =
        CalcColumnSize(data::SparsePageAdapterBatch{page.GetView()}, info.num_col_, n_threads,
                       [](auto) { return true; });
    CHECK_EQ(entries_per_column.size(), info.num_col_);
    for (size_t i = 0; i < entries_per_column.size(); ++i) {
      reduced[i] += entries_per_column[i];
    }
  }

  if (!use_sorted) {
    HostSketchContainer container(max_bins, m->Info().feature_types.ConstHostSpan(), reduced,
                                  HostSketchContainer::UseGroup(info), n_threads);
    for (auto const &page : m->GetBatches<SparsePage>()) {
      container.PushRowPage(page, info, hessian);
    }
    container.MakeCuts(&out);
  } else {
    SortedSketchContainer container{max_bins, m->Info().feature_types.ConstHostSpan(), reduced,
                                    HostSketchContainer::UseGroup(info), n_threads};
    for (auto const &page : m->GetBatches<SortedCSCPage>()) {
      container.PushColPage(page, info, hessian);
    }
    container.MakeCuts(&out);
  }

  return out;
}

/*!
 * \brief fill a histogram by zeros in range [begin, end)
 */
template <class H>
void InitilizeHistByZeroes(GHistRow<H> hist, size_t begin, size_t end) {
#if defined(XGBOOST_STRICT_R_MODE) && XGBOOST_STRICT_R_MODE == 1
  std::fill(hist.begin() + begin, hist.begin() + end, xgboost::GradientPairT<H>());
#else   // defined(XGBOOST_STRICT_R_MODE) && XGBOOST_STRICT_R_MODE == 1
  if (is_same<double, H>()) {
    memset(hist.data() + begin, '\0', (end - begin) * sizeof(xgboost::GradientPairT<H>));
  } else {
    std::for_each(hist.begin() + begin, hist.begin() + end, [&](auto &gp) { gp.Clear(); });
  }
#endif  // defined(XGBOOST_STRICT_R_MODE) && XGBOOST_STRICT_R_MODE == 1
}

/*!
 * \brief Increment hist as dst += add in range [begin, end)
 */
template <class H>
void IncrementHist(GHistRow<H> dst, const GHistRow<H> add, size_t begin, size_t end) {
  H *pdst = reinterpret_cast<H *>(dst.data());
  const H *padd = reinterpret_cast<const H *>(add.data());

  for (size_t i = 2 * begin; i < 2 * end; ++i) {
    pdst[i] += padd[i];
  }
}

/*!
 * \brief Copy hist from src to dst in range [begin, end)
 */
template <class H>
void CopyHist(GHistRow<H> dst, const GHistRow<H> src, size_t begin, size_t end) {
  H *pdst = reinterpret_cast<H *>(dst.data());
  const H *psrc = reinterpret_cast<const H *>(src.data());

  for (size_t i = 2 * begin; i < 2 * end; ++i) {
    pdst[i] = psrc[i];
  }
}

/*!
 * \brief Compute Subtraction: dst = src1 - src2 in range [begin, end)
 */
template <class H>
void SubtractionHist(GHistRow<H> dst, const GHistRow<H> src1, const GHistRow<H> src2, size_t begin,
                     size_t end) {
  H *pdst = reinterpret_cast<H *>(dst.data());
  const H *psrc1 = reinterpret_cast<const H *>(src1.data());
  const H *psrc2 = reinterpret_cast<const H *>(src2.data());

  for (size_t i = 2 * begin; i < 2 * end; ++i) {
    pdst[i] = psrc1[i] - psrc2[i];
  }
}

struct Prefetch {
 public:
  static constexpr size_t kCacheLineSize = 64;
  static constexpr size_t kPrefetchOffset = 10;

 private:
  static constexpr size_t kNoPrefetchSize =
      kPrefetchOffset + kCacheLineSize / sizeof(decltype(GHistIndexMatrix::row_ptr)::value_type);

 public:
  static size_t NoPrefetchSize(size_t rows) { return std::min(rows, kNoPrefetchSize); }

  template <typename T>
  static constexpr size_t GetPrefetchStep() {
    return Prefetch::kCacheLineSize / sizeof(T);
  }
};

constexpr size_t Prefetch::kNoPrefetchSize;

struct RuntimeFlags {
  const bool first_page;
  const bool read_by_column;
  const BinTypeSize bin_type_size;
};

template <bool _any_missing, bool _first_page = false, bool _read_by_column = false,
          typename BinIdxTypeName = uint8_t>
class GHistBuildingManager {
 public:
  constexpr static bool kAnyMissing = _any_missing;
  constexpr static bool kFirstPage = _first_page;
  constexpr static bool kReadByColumn = _read_by_column;
  using BinIdxType = BinIdxTypeName;

 private:
  template <bool new_first_page>
  struct SetFirstPage {
    using Type = GHistBuildingManager<kAnyMissing, new_first_page, kReadByColumn, BinIdxType>;
  };

  template <bool new_read_by_column>
  struct SetReadByColumn {
    using Type = GHistBuildingManager<kAnyMissing, kFirstPage, new_read_by_column, BinIdxType>;
  };

  template <typename NewBinIdxType>
  struct SetBinIdxType {
    using Type = GHistBuildingManager<kAnyMissing, kFirstPage, kReadByColumn, NewBinIdxType>;
  };

  using Type = GHistBuildingManager<kAnyMissing, kFirstPage, kReadByColumn, BinIdxType>;

 public:
  /* Entry point to dispatcher
   * This function check matching run time flags to compile time flags.
   * In case of difference, it creates a Manager with different template parameters
   *  and forward the call there.
   */
  template <typename Fn>
  static void DispatchAndExecute(const RuntimeFlags &flags, Fn &&fn) {
    if (flags.first_page != kFirstPage) {
      SetFirstPage<true>::Type::DispatchAndExecute(flags, std::forward<Fn>(fn));
    } else if (flags.read_by_column != kReadByColumn) {
      SetReadByColumn<true>::Type::DispatchAndExecute(flags, std::forward<Fn>(fn));
    } else if (flags.bin_type_size != sizeof(BinIdxType)) {
      DispatchBinType(flags.bin_type_size, [&](auto t) {
        using NewBinIdxType = decltype(t);
        SetBinIdxType<NewBinIdxType>::Type::DispatchAndExecute(flags, std::forward<Fn>(fn));
      });
    } else {
      fn(Type());
    }
  }
};

template <bool do_prefetch, class BuildingManager, typename T = float, typename H = double>
void RowsWiseBuildHistKernel(const std::vector<GradientPairT<T>> &gpair,
                             const RowSetCollection::Elem row_indices, const GHistIndexMatrix &gmat,
                             GHistRow<H> hist) {
  constexpr bool kAnyMissing = BuildingManager::kAnyMissing;
  constexpr bool kFirstPage = BuildingManager::kFirstPage;
  using BinIdxType = typename BuildingManager::BinIdxType;

  const size_t size = row_indices.Size();
  const size_t *rid = row_indices.begin;
  auto const *pgh = reinterpret_cast<const T *>(gpair.data());
  const BinIdxType *gradient_index = gmat.index.data<BinIdxType>();

  auto const &row_ptr = gmat.row_ptr.data();
  auto base_rowid = gmat.base_rowid;
  const uint32_t *offsets = gmat.index.Offset();
  auto get_row_ptr = [&](size_t ridx) {
    return kFirstPage ? row_ptr[ridx] : row_ptr[ridx - base_rowid];
  };
  auto get_rid = [&](size_t ridx) { return kFirstPage ? ridx : (ridx - base_rowid); };

  const size_t n_features =
      get_row_ptr(row_indices.begin[0] + 1) - get_row_ptr(row_indices.begin[0]);
  auto hist_data = reinterpret_cast<H *>(hist.data());
  const uint32_t two{2};  // Each element from 'gpair' and 'hist' contains
                          // 2 FP values: gradient and hessian.
                          // So we need to multiply each row-index/bin-index by 2
                          // to work with gradient pairs as a singe row FP array

  for (size_t i = 0; i < size; ++i) {
    const size_t icol_start = kAnyMissing ? get_row_ptr(rid[i]) : get_rid(rid[i]) * n_features;
    const size_t icol_end = kAnyMissing ? get_row_ptr(rid[i] + 1) : icol_start + n_features;

    const size_t row_size = icol_end - icol_start;
    const size_t idx_gh = two * rid[i];

    if (do_prefetch) {
      const size_t icol_start_prefetch =
          kAnyMissing ? get_row_ptr(rid[i + Prefetch::kPrefetchOffset])
                      : get_rid(rid[i + Prefetch::kPrefetchOffset]) * n_features;
      const size_t icol_end_prefetch = kAnyMissing
                                           ? get_row_ptr(rid[i + Prefetch::kPrefetchOffset] + 1)
                                           : icol_start_prefetch + n_features;

      PREFETCH_READ_T0(pgh + two * rid[i + Prefetch::kPrefetchOffset]);
      for (size_t j = icol_start_prefetch; j < icol_end_prefetch;
           j += Prefetch::GetPrefetchStep<uint32_t>()) {
        PREFETCH_READ_T0(gradient_index + j);
      }
    }
    const BinIdxType *gr_index_local = gradient_index + icol_start;

    // The trick with pgh_t buffer helps the compiler to generate faster binary.
    const T pgh_t[] = {pgh[idx_gh], pgh[idx_gh + 1]};
    for (size_t j = 0; j < row_size; ++j) {
      const uint32_t idx_bin =
          two * (static_cast<uint32_t>(gr_index_local[j]) + (kAnyMissing ? 0 : offsets[j]));
      H *hist_local = hist_data + idx_bin;
      *(hist_local) += pgh_t[0];
      *(hist_local + 1) += pgh_t[1];
    }
  }
}

template <class BuildingManager, typename T = float, typename H = double>
void ColsWiseBuildHistKernel(const std::vector<GradientPairT<T>> &gpair,
                             const RowSetCollection::Elem row_indices, const GHistIndexMatrix &gmat,
                             GHistRow<H> hist) {
  constexpr bool kAnyMissing = BuildingManager::kAnyMissing;
  constexpr bool kFirstPage = BuildingManager::kFirstPage;
  using BinIdxType = typename BuildingManager::BinIdxType;
  const size_t size = row_indices.Size();
  const size_t *rid = row_indices.begin;
  auto const *pgh = reinterpret_cast<const T *>(gpair.data());
  const BinIdxType *gradient_index = gmat.index.data<BinIdxType>();

  auto const &row_ptr = gmat.row_ptr.data();
  auto base_rowid = gmat.base_rowid;
  const uint32_t *offsets = gmat.index.Offset();
  auto get_row_ptr = [&](size_t ridx) {
    return kFirstPage ? row_ptr[ridx] : row_ptr[ridx - base_rowid];
  };
  auto get_rid = [&](size_t ridx) { return kFirstPage ? ridx : (ridx - base_rowid); };

  const size_t n_features = gmat.cut.Ptrs().size() - 1;
  const size_t n_columns = n_features;
  auto hist_data = reinterpret_cast<H *>(hist.data());
  const uint32_t two{2};  // Each element from 'gpair' and 'hist' contains
                          // 2 FP values: gradient and hessian.
                          // So we need to multiply each row-index/bin-index by 2
                          // to work with gradient pairs as a singe row FP array
  for (size_t cid = 0; cid < n_columns; ++cid) {
    const uint32_t offset = kAnyMissing ? 0 : offsets[cid];
    for (size_t i = 0; i < size; ++i) {
      const size_t row_id = rid[i];
      const size_t icol_start = kAnyMissing ? get_row_ptr(row_id) : get_rid(row_id) * n_features;
      const size_t icol_end = kAnyMissing ? get_row_ptr(rid[i] + 1) : icol_start + n_features;

      if (cid < icol_end - icol_start) {
        const BinIdxType *gr_index_local = gradient_index + icol_start;
        const uint32_t idx_bin = two * (static_cast<uint32_t>(gr_index_local[cid]) + offset);
        auto hist_local = hist_data + idx_bin;

        const size_t idx_gh = two * row_id;
        // The trick with pgh_t buffer helps the compiler to generate faster binary.
        const T pgh_t[] = {pgh[idx_gh], pgh[idx_gh + 1]};
        *(hist_local) += pgh_t[0];
        *(hist_local + 1) += pgh_t[1];
      }
    }
  }
}

template <class BuildingManager, typename T = float, typename H = double>
void BuildHistDispatch(const std::vector<GradientPairT<T>> &gpair,
                       const RowSetCollection::Elem row_indices, const GHistIndexMatrix &gmat,
                       GHistRow<H> hist) {
  if (BuildingManager::kReadByColumn) {
    ColsWiseBuildHistKernel<BuildingManager, T, H>(gpair, row_indices, gmat, hist);
  } else {
    const size_t nrows = row_indices.Size();
    const size_t no_prefetch_size = Prefetch::NoPrefetchSize(nrows);
    // if need to work with all rows from bin-matrix (e.g. root node)
    const bool contiguousBlock =
        (row_indices.begin[nrows - 1] - row_indices.begin[0]) == (nrows - 1);

    if (contiguousBlock) {
      // contiguous memory access, built-in HW prefetching is enough
      RowsWiseBuildHistKernel<false, BuildingManager, T, H>(gpair, row_indices, gmat, hist);
    } else {
      const RowSetCollection::Elem span1(row_indices.begin, row_indices.end - no_prefetch_size);
      const RowSetCollection::Elem span2(row_indices.end - no_prefetch_size, row_indices.end);

      RowsWiseBuildHistKernel<true, BuildingManager, T, H>(gpair, span1, gmat, hist);
      // no prefetching to avoid loading extra memory
      RowsWiseBuildHistKernel<false, BuildingManager, T, H>(gpair, span2, gmat, hist);
    }
  }
}

template <bool any_missing, typename T, typename H>
void GHistBuilder::BuildHist(const std::vector<GradientPairT<T>> &gpair,
                             const RowSetCollection::Elem row_indices, const GHistIndexMatrix &gmat,
                             GHistRow<H> hist, bool force_read_by_column) const {
  /* force_read_by_column is used for testing the columnwise building of histograms.
   * default force_read_by_column = false
   */
  constexpr double kAdhocL2Size = 1024 * 1024 * 0.8;
  const bool hist_fit_to_l2 = kAdhocL2Size > 2 * sizeof(float) * gmat.cut.Ptrs().back();
  bool first_page = gmat.base_rowid == 0;
  bool read_by_column = !hist_fit_to_l2 && !any_missing;
  auto bin_type_size = gmat.index.GetBinTypeSize();

  GHistBuildingManager<any_missing>::DispatchAndExecute(
      {first_page, read_by_column || force_read_by_column, bin_type_size}, [&](auto t) {
        using BuildingManager = decltype(t);
        BuildHistDispatch<BuildingManager, T, H>(gpair, row_indices, gmat, hist);
      });
}

template void GHistBuilder::BuildHist<true, float, double>(const std::vector<GradientPair> &gpair,
                                                           const RowSetCollection::Elem row_indices,
                                                           const GHistIndexMatrix &gmat,
                                                           GHistRow<double> hist,
                                                           bool force_read_by_column) const;

template void GHistBuilder::BuildHist<true, EncryptedType<float>, EncryptedType<double>>(
    const std::vector<EncryptedGradientPair> &gpair, const RowSetCollection::Elem row_indices,
    const GHistIndexMatrix &gmat, GHistRow<EncryptedType<double>> hist,
    bool force_read_by_column) const;

template void GHistBuilder::BuildHist<false, float, double>(
    const std::vector<GradientPair> &gpair, const RowSetCollection::Elem row_indices,
    const GHistIndexMatrix &gmat, GHistRow<double> hist, bool force_read_by_column) const;

template void GHistBuilder::BuildHist<false, EncryptedType<float>, EncryptedType<double>>(
    const std::vector<EncryptedGradientPair> &gpair, const RowSetCollection::Elem row_indices,
    const GHistIndexMatrix &gmat, GHistRow<EncryptedType<double>> hist,
    bool force_read_by_column) const;

template void InitilizeHistByZeroes<double>(GHistRow<double> hist, size_t begin, size_t end);

template void IncrementHist<double>(GHistRow<double> dst, const GHistRow<double> add, size_t begin,
                                    size_t end);

template void CopyHist<double>(GHistRow<double> dst, const GHistRow<double> src, size_t begin,
                               size_t end);

template void SubtractionHist<double>(GHistRow<double> dst, const GHistRow<double> src1,
                                      const GHistRow<double> src2, size_t begin, size_t end);

template void InitilizeHistByZeroes<EncryptedType<double>>(GHistRow<EncryptedType<double>> hist,
                                                           size_t begin, size_t end);

template void IncrementHist<EncryptedType<double>>(GHistRow<EncryptedType<double>> dst,
                                                   const GHistRow<EncryptedType<double>> add,
                                                   size_t begin, size_t end);

template void CopyHist<EncryptedType<double>>(GHistRow<EncryptedType<double>> dst,
                                              const GHistRow<EncryptedType<double>> src,
                                              size_t begin, size_t end);

template void SubtractionHist<EncryptedType<double>>(GHistRow<EncryptedType<double>> dst,
                                                     const GHistRow<EncryptedType<double>> src1,
                                                     const GHistRow<EncryptedType<double>> src2,
                                                     size_t begin, size_t end);
}  // namespace common
}  // namespace xgboost
