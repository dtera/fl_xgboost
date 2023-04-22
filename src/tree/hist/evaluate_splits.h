/*!
 * Copyright 2021-2022 by XGBoost Contributors
 */
#ifndef XGBOOST_TREE_HIST_EVALUATE_SPLITS_H_
#define XGBOOST_TREE_HIST_EVALUATE_SPLITS_H_

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include "../../common/categorical.h"
#include "../../common/hist_util.h"
#include "../../common/random.h"
#include "../../data/gradient_index.h"
#include "../constraints.h"
#include "../param.h"
#include "../split_evaluator.h"
#include "comm/XgbServiceRegistry.h"
#include "ssl_util.h"
#include "xgboost/federated_param.h"

using namespace google::protobuf;

namespace xgboost {
namespace tree {

template <typename ExpandEntry, typename H = double>
class HistEvaluator {
 private:
  struct NodeEntry {
    /*! \brief statics for node entry */
    GradStats<H> stats;
    /*! \brief loss of this node, without split */
    bst_float root_gain{0.0f};
  };

 private:
  TrainParam param_;
  std::shared_ptr<common::ColumnSampler> column_sampler_;
  TreeEvaluator tree_evaluator_;
  int32_t n_threads_{0};
  FeatureInteractionConstraintHost interaction_constraints_;
  std::vector<NodeEntry> snode_;
  std::string key_;

  // if sum of statistics for non-missing values in the node
  // is equal to sum of statistics for all values:
  // then - there are no missing values
  // else - there are missing values
  bool static SplitContainsMissingValues(const GradStats<H> e, const NodeEntry &snode) {
    bool flag = false;
    if (is_same<double, H>()) {
      flag = e.GetGrad() == snode.stats.GetGrad() && e.GetHess() == snode.stats.GetHess();
    } else {
      if (!IsPulsar()) {
        flag = xgb_client_->IsSplitContainsMissingValues(e, snode.stats);
      }
    }
    if (flag) {
      return false;
    } else {
      return true;
    }
  }

  bool IsValid(GradStats<double> const &left, GradStats<double> const &right) const {
    return left.GetHess() >= param_.min_child_weight && right.GetHess() >= param_.min_child_weight;
  }

  bool IsValid(GradStats<EncryptedType<double>> const &left,
               GradStats<EncryptedType<double>> const &right) const {
    // TODO: is valid
    return true;
  }

  /**
   * \brief Use learned direction with one-hot split. Other implementations (LGB) create a
   *        pseudo-category for missing value but here we just do a complete scan to avoid
   *        making specialized histogram bin.
   */
  void EnumerateOneHot(common::HistogramCuts const &cut, const common::GHistRow<H> &hist,
                       bst_feature_t fidx, bst_node_t nidx,
                       TreeEvaluator::SplitEvaluator<TrainParam> const &evaluator,
                       SplitEntry<> *p_best, SplitsRequest *splits_request) const {
    const std::vector<uint32_t> &cut_ptr = cut.Ptrs();
    const std::vector<bst_float> &cut_val = cut.Values();

    bst_bin_t ibegin = static_cast<bst_bin_t>(cut_ptr[fidx]);
    bst_bin_t iend = static_cast<bst_bin_t>(cut_ptr[fidx + 1]);
    bst_bin_t n_bins = iend - ibegin;

    GradStats<H> left_sum;
    GradStats<H> right_sum;
    // best split so far
    SplitEntry<> best;
    best.is_cat = false;  // marker for whether it's updated or not.

    auto f_hist = hist.subspan(cut_ptr[fidx], n_bins);
    auto feature_sum = GradStats<H>{
        std::accumulate(f_hist.data(), f_hist.data() + f_hist.size(), GradientPairT<H>{})};
    GradStats<H> missing;
    auto const &parent = snode_[nidx];
    missing.SetSubstract(parent.stats, feature_sum);

    for (bst_bin_t i = ibegin; i != iend; i += 1) {
      auto split_pt = cut_val[i];

      // missing on left (treat missing as other categories)
      right_sum = GradStats<H>{hist[i]};
      left_sum.SetSubstract(parent.stats, right_sum);
      EnumerateUpdate(i, fidx, nidx, split_pt, evaluator, left_sum, right_sum, best, 1, true, true,
                      splits_request);

      // missing on right (treat missing as chosen category)
      right_sum.Add(missing);
      left_sum.SetSubstract(parent.stats, right_sum);
      EnumerateUpdate(i, fidx, nidx, split_pt, evaluator, left_sum, right_sum, best, 1, false, true,
                      splits_request);
    }

    if (best.is_cat) {
      auto n = common::CatBitField::ComputeStorageSize(n_bins + 1);
      best.cat_bits.resize(n, 0);
      common::CatBitField cat_bits{best.cat_bits};
      cat_bits.Set(best.split_value);
    }

    p_best->Update(best);
  }

  /**
   * \brief Enumerate with partition-based splits.
   *
   * The implementation is different from LightGBM. Firstly we don't have a
   * pseudo-cateogry for missing value, instead of we make 2 complete scans over the
   * histogram. Secondly, both scan directions generate splits in the same
   * order. Following table depicts the scan process, square bracket means the gradient in
   * missing values is resided on that partition:
   *
   *   | Forward  | Backward |
   *   |----------+----------|
   *   | [BCDE] A | E [ABCD] |
   *   | [CDE] AB | DE [ABC] |
   *   | [DE] ABC | CDE [AB] |
   *   | [E] ABCD | BCDE [A] |
   */
  template <int d_step>
  void EnumeratePart(common::HistogramCuts const &cut, common::Span<size_t const> sorted_idx,
                     common::GHistRow<H> const &hist, bst_feature_t fidx, bst_node_t nidx,
                     TreeEvaluator::SplitEvaluator<TrainParam> const &evaluator,
                     SplitEntry<> *p_best, SplitsRequest *splits_request) {
    static_assert(d_step == +1 || d_step == -1, "Invalid step.");

    auto const &cut_ptr = cut.Ptrs();
    auto const &cut_val = cut.Values();
    auto const &parent = snode_[nidx];

    bst_bin_t f_begin = cut_ptr[fidx];
    bst_bin_t f_end = cut_ptr[fidx + 1];
    bst_bin_t n_bins_feature{f_end - f_begin};
    auto n_bins = std::min(param_.max_cat_threshold, n_bins_feature);

    // statistics on both sides of split
    GradStats<H> left_sum;
    GradStats<H> right_sum;
    // best split so far
    SplitEntry<> best;

    auto f_hist = hist.subspan(f_begin, n_bins_feature);
    bst_bin_t it_begin, it_end;
    if (d_step > 0) {
      it_begin = f_begin;
      it_end = it_begin + n_bins - 1;
    } else {
      it_begin = f_end - 1;
      it_end = it_begin - n_bins + 1;
    }

    bst_bin_t best_thresh{-1};
    for (bst_bin_t i = it_begin; i != it_end; i += d_step) {
      auto j = i - f_begin;  // index local to current feature
      if (d_step == 1) {
        right_sum.Add(f_hist[sorted_idx[j]].GetGrad(), f_hist[sorted_idx[j]].GetHess());
        left_sum.SetSubstract(parent.stats, right_sum);  // missing on left
      } else {
        left_sum.Add(f_hist[sorted_idx[j]].GetGrad(), f_hist[sorted_idx[j]].GetHess());
        right_sum.SetSubstract(parent.stats, left_sum);  // missing on right
      }

      // TODO We don't have a numeric split point, nan here is a dummy split.
      if (EnumerateUpdate(i, fidx, nidx, std::numeric_limits<float>::quiet_NaN(), evaluator,
                          left_sum, right_sum, best, d_step, d_step == 1, true, splits_request)) {
        best_thresh = i;
      }
    }

    if (best_thresh != -1) {
      auto n = common::CatBitField::ComputeStorageSize(n_bins_feature);
      best.cat_bits = decltype(best.cat_bits)(n, 0);
      common::CatBitField cat_bits{best.cat_bits};
      bst_bin_t partition = d_step == 1 ? (best_thresh - it_begin + 1) : (best_thresh - f_begin);
      CHECK_GT(partition, 0);
      std::for_each(sorted_idx.begin(), sorted_idx.begin() + partition, [&](size_t c) {
        auto cat = cut_val[c + f_begin];
        cat_bits.Set(cat);
      });
    }

    p_best->Update(best);
  }

  bool EnumerateUpdate(bst_bin_t bin_id, bst_feature_t fidx, bst_node_t nidx, bst_float split_pt,
                       TreeEvaluator::SplitEvaluator<TrainParam> const &evaluator,
                       GradStats<EncryptedType<double>> &left_sum,
                       GradStats<EncryptedType<double>> &right_sum, SplitEntry<> &best, int d_step,
                       bool default_left, bool is_cat = false,
                       SplitsRequest *splits_request = nullptr) const {
    assert(splits_request != nullptr);
    EncryptedSplit *es = splits_request->mutable_encrypted_splits()->Add();
    // encrypt the feature id and bin id to mask id
    es->set_mask_id(xor_encrypt(to_string(fidx) + "_" + to_string(bin_id), key_));
    xgbcomm::GradPair *ls = es->mutable_left_sum();
    xgbcomm::GradPair *rs = es->mutable_right_sum();
    mpz_t2_mpz_type(ls, left_sum);
    mpz_t2_mpz_type(rs, right_sum);
    es->set_d_step(d_step);
    es->set_default_left(default_left);
    es->set_is_cat(is_cat);
    if (!IsPulsar()) {
      xgb_client_->CacheEncryptedSplit(es->mask_id(), es);
    }
    return true;
  }

  bool EnumerateUpdate(bst_bin_t bin_id, bst_feature_t fidx, bst_node_t nidx, bst_float split_pt,
                       TreeEvaluator::SplitEvaluator<TrainParam> const &evaluator,
                       GradStats<double> &left_sum, GradStats<double> &right_sum,
                       SplitEntry<> &best, int d_step, bool default_left, bool is_cat = false,
                       SplitsRequest *splits_request = nullptr) const {
    bool updated = false;
    if (IsValid(left_sum, right_sum)) {
      bst_float loss_chg;
      if (d_step > 0) {
        // forward enumeration: split at right bound of each bin
        loss_chg = evaluator.CalcSplitGain(param_, nidx, fidx, left_sum, right_sum) -
                   snode_[nidx].root_gain;

        updated = best.Update(loss_chg, fidx, split_pt, default_left, is_cat, left_sum, right_sum);
      } else {
        // backward enumeration: split at left bound of each bin
        loss_chg = evaluator.CalcSplitGain(param_, nidx, fidx, right_sum, left_sum) -
                   snode_[nidx].root_gain;
        updated = best.Update(loss_chg, fidx, split_pt, default_left, is_cat, right_sum, left_sum);
      }
    }
    return updated;
  }

  // Enumerate/Scan the split values of specific feature
  // Returns the sum of gradients corresponding to the data points that contains
  // a non-missing value for the particular feature fid.
  template <int d_step>
  GradStats<H> EnumerateSplit(common::HistogramCuts const &cut, const common::GHistRow<H> &hist,
                              bst_feature_t fidx, bst_node_t nidx,
                              TreeEvaluator::SplitEvaluator<TrainParam> const &evaluator,
                              SplitEntry<> *p_best, SplitsRequest *splits_request) const {
    static_assert(d_step == +1 || d_step == -1, "Invalid step.");

    // aliases
    const std::vector<uint32_t> &cut_ptr = cut.Ptrs();
    const std::vector<bst_float> &cut_val = cut.Values();
    auto const &parent = snode_[nidx];

    // statistics on both sides of split
    GradStats<H> left_sum;
    GradStats<H> right_sum;
    // best split so far
    SplitEntry<> best;

    // bin boundaries
    CHECK_LE(cut_ptr[fidx], static_cast<uint32_t>(std::numeric_limits<bst_bin_t>::max()));
    CHECK_LE(cut_ptr[fidx + 1], static_cast<uint32_t>(std::numeric_limits<bst_bin_t>::max()));
    // imin: index (offset) of the minimum value for feature fid need this for backward
    //       enumeration
    const auto imin = static_cast<bst_bin_t>(cut_ptr[fidx]);
    // ibegin, iend: smallest/largest cut points for feature fid use int to allow for
    // value -1
    bst_bin_t ibegin, iend;
    if (d_step > 0) {
      ibegin = static_cast<bst_bin_t>(cut_ptr[fidx]);
      iend = static_cast<bst_bin_t>(cut_ptr.at(fidx + 1));
    } else {
      ibegin = static_cast<bst_bin_t>(cut_ptr[fidx + 1]) - 1;
      iend = static_cast<bst_bin_t>(cut_ptr[fidx]) - 1;
    }

    for (bst_bin_t i = ibegin; i != iend; i += d_step) {
      // start working
      // try to find a split
      left_sum.Add(hist[i].GetGrad(), hist[i].GetHess());
      right_sum.SetSubstract(parent.stats, left_sum);
      // update best split
      bst_float split_pt;
      if (d_step > 0) {
        split_pt = cut.Values()[i];  // not used for partition based
      } else if (i == static_cast<bst_bin_t>(cut.Ptrs()[fidx])) {
        split_pt = cut.MinValues()[fidx];
      } else {
        split_pt = cut.Values()[i - 1];
      }
      EnumerateUpdate(i, fidx, nidx, split_pt, evaluator, left_sum, right_sum, best, d_step,
                      d_step == -1, false, splits_request);
    }

    p_best->Update(best);
    return left_sum;
  }

 public:
  void SortFeatHistogram(std::vector<size_t> &sorted_idx,
                         common::GHistRow<EncryptedType<double>> &feat_hist,
                         TreeEvaluator::SplitEvaluator<TrainParam> &evaluator) {
    // TODO: Sort the histogram to get contiguous partitions for data holder part.
  }

  void SortFeatHistogram(std::vector<size_t> &sorted_idx, common::GHistRow<double> &feat_hist,
                         TreeEvaluator::SplitEvaluator<TrainParam> &evaluator) {
    // Sort the histogram to get contiguous partitions.
    std::stable_sort(sorted_idx.begin(), sorted_idx.end(), [&](size_t l, size_t r) {
      auto ret = evaluator.CalcWeightCat(param_, feat_hist[l]) <
                 evaluator.CalcWeightCat(param_, feat_hist[r]);
      return ret;
    });
  }

  void UpdateForDataHolder(ExpandEntry &e,
                           const TreeEvaluator::SplitEvaluator<TrainParam> &evaluator,
                           const SplitsRequest *sr, std::atomic<int> &bestIdx) const {
    if (IsFederated()) {
      e.split.part_id = fparam_->fl_part_id;
      if (IsPulsar()) {
        /*
        SplitsRequest sr;
        xgb_pulsar_->GetEncryptedSplits(e.nid, sr,
                                        [&](uint32_t i, GradStats<double> &left_sum,
                                            GradStats<double> &right_sum, const SplitsRequest &) {
                                          auto es = sr.encrypted_splits()[i];
                                          auto updated = EnumerateUpdate(
                                              -1, 0, sr.nidx(), 0.0, evaluator, left_sum, right_sum,
                                              e.split, es.d_step(), es.default_left(), es.is_cat());
                                          if (updated) {
                                            e.split.part_id = sr.part_id();
                                            bestIdx = i;
                                          }
                                        });
        xgb_pulsar_->SendBestSplit(bestIdx.load(), e, sr);*/
        xgb_pulsar_->HandleSplitUpdate(
            *sr, [&](uint32_t i, GradStats<double> &left_sum, GradStats<double> &right_sum,
                     const SplitsRequest &) {
              auto es = sr->encrypted_splits()[i];
              auto updated = EnumerateUpdate(-1, 0, sr->nidx(), 0.0, evaluator, left_sum, right_sum,
                                             e.split, es.d_step(), es.default_left(), es.is_cat());
              if (updated) {
                e.split.part_id = sr->part_id();
                bestIdx = i;
              }
            });
      } else {
        xgb_server_->UpdateFinishSplits(e.nid, false);
        // update expand entry for the data holder part
        xgb_server_->UpdateExpandEntry(
            e, [&](uint32_t i, GradStats<double> &left_sum, GradStats<double> &right_sum,
                   const SplitsRequest &sr) {
              auto es = sr.encrypted_splits()[i];
              // update grad statistics for the data holder part
              auto updated = EnumerateUpdate(-1, 0, sr.nidx(), 0.0, evaluator, left_sum, right_sum,
                                             e.split, es.d_step(), es.default_left(), es.is_cat());
              if (updated) {
                e.split.part_id = sr.part_id();
                xgb_server_->UpdateBestEncryptedSplit(sr.nidx(), es);
              }
            });
      }
    }
  }

  void DataHolderUpdate(const common::HistogramCuts &cut,
                        common::Span<const FeatureType> &feature_types, ExpandEntry &e,
                        SplitsRequest &sr,
                        function<void(function<void(SplitsResponse &)>)> update) const {
    auto const &cut_ptrs = cut.Ptrs();
    auto process_best_split = [&](SplitsResponse &response) {
      bst_feature_t fidx = 0;
      bst_float split_pt = 0;

      auto es = response.encrypted_split();
      if (!es.mask_id().empty()) {
        // decrypt the feature id and bin id from mask id
        vector<string> ids;
        boost::split(ids, xor_decrypt(es.mask_id(), key_), boost::is_any_of("_"));
        fidx = atoi(ids[0].c_str());
        bst_bin_t bin_id = atoi(ids[1].c_str());
        bool is_cat = common::IsCat(feature_types, fidx);
        auto n_bins = cut_ptrs.at(fidx + 1) - cut_ptrs[fidx];

        if (is_cat) {
          if (common::UseOneHot(n_bins, param_.max_cat_to_onehot)) {
            split_pt = cut.Values()[bin_id];
          } else {
            split_pt = numeric_limits<float>::quiet_NaN();
          }
        } else {
          if (es.d_step() > 0) {
            split_pt = cut.Values()[bin_id];  // not used for partition based
          } else if (bin_id == static_cast<bst_bin_t>(cut.Ptrs()[fidx])) {
            split_pt = cut.MinValues()[fidx];
          } else {
            split_pt = cut.Values()[bin_id - 1];
          }
        }
      }

      GradStats<EncryptedType<double>> left_sum;
      GradStats<EncryptedType<double>> right_sum;
      mpz_type2_mpz_t(left_sum, es.left_sum());
      mpz_type2_mpz_t(right_sum, es.right_sum());
      if (!es.mask_id().empty() && es.d_step() < 0) {
        e.split.Update(fidx, split_pt, es.default_left(), response.part_id(), right_sum, left_sum);
      } else {
        e.split.Update(fidx, split_pt, es.default_left(), response.part_id(), left_sum, right_sum);
      }
    };
    update(process_best_split);
    /*if (IsPulsar()) {
      xgb_pulsar_->SendEncryptedSplits(sr);
      xgb_pulsar_->GetBestSplit(e.nid, process_best_split);
    } else {
      xgb_client_->SendEncryptedSplits(sr, process_best_split);
    }*/
  }

  void EvaluateSplits(const common::HistCollection<H> &hist, common::HistogramCuts const &cut,
                      common::Span<FeatureType const> feature_types, const RegTree &tree,
                      std::vector<ExpandEntry> *p_entries) {
    auto &entries = *p_entries;
    // All nodes are on the same level, so we can store the shared ptr.
    std::vector<std::shared_ptr<HostDeviceVector<bst_feature_t>>> features(entries.size());
    for (size_t nidx_in_set = 0; nidx_in_set < entries.size(); ++nidx_in_set) {
      auto nidx = entries[nidx_in_set].nid;
      features[nidx_in_set] = column_sampler_->GetFeatureSet(tree.GetDepth(nidx));
    }
    CHECK(!features.empty());
    const size_t grain_size = std::max<size_t>(1, features.front()->Size() / n_threads_);
    common::BlockedSpace2d space(
        entries.size(), [&](size_t nidx_in_set) { return features[nidx_in_set]->Size(); },
        grain_size);

    std::vector<SplitsRequest> splits_requests;
    splits_requests.resize(entries.size());
    std::vector<ExpandEntry> tloc_candidates(n_threads_ * entries.size());
    std::vector<SplitsRequest> tloc_requests(n_threads_ * entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
      for (decltype(n_threads_) j = 0; j < n_threads_; ++j) {
        tloc_candidates[i * n_threads_ + j] = entries[i];
        tloc_requests[i * n_threads_ + j] = splits_requests[i];
      }
    }
    auto evaluator = tree_evaluator_.GetEvaluator();
    auto const &cut_ptrs = cut.Ptrs();

    common::ParallelFor2d(space, n_threads_, [&](size_t nidx_in_set, common::Range1d r) {
      auto tidx = omp_get_thread_num();
      auto entry = &tloc_candidates[n_threads_ * nidx_in_set + tidx];
      auto splits_req = &tloc_requests[n_threads_ * nidx_in_set + tidx];
      auto best = &entry->split;
      auto nidx = entry->nid;
      auto histogram = hist[nidx];
      auto features_set = features[nidx_in_set]->ConstHostSpan();
      for (auto fidx_in_set = r.begin(); fidx_in_set < r.end(); fidx_in_set++) {
        auto fidx = features_set[fidx_in_set];
        bool is_cat = common::IsCat(feature_types, fidx);
        if (!interaction_constraints_.Query(nidx, fidx)) {
          continue;
        }
        if (is_cat) {
          auto n_bins = cut_ptrs.at(fidx + 1) - cut_ptrs[fidx];
          if (common::UseOneHot(n_bins, param_.max_cat_to_onehot)) {
            EnumerateOneHot(cut, histogram, fidx, nidx, evaluator, best, splits_req);
          } else {
            std::vector<size_t> sorted_idx(n_bins);
            std::iota(sorted_idx.begin(), sorted_idx.end(), 0);
            auto feat_hist = histogram.subspan(cut_ptrs[fidx], n_bins);
            SortFeatHistogram(sorted_idx, feat_hist, evaluator);

            EnumeratePart<+1>(cut, sorted_idx, histogram, fidx, nidx, evaluator, best, splits_req);
            EnumeratePart<-1>(cut, sorted_idx, histogram, fidx, nidx, evaluator, best, splits_req);
          }
        } else {
          auto grad_stats =
              EnumerateSplit<+1>(cut, histogram, fidx, nidx, evaluator, best, splits_req);
          if (SplitContainsMissingValues(grad_stats, snode_[nidx])) {
            EnumerateSplit<-1>(cut, histogram, fidx, nidx, evaluator, best, splits_req);
          }
        }
      }
    });

    SplitsRequests splitsRequests;
    SplitsResponses splitsResponses;
    std::uint32_t nids = 0;
    if (is_same<double, H>()) {
      for (unsigned nidx_in_set = 0; nidx_in_set < entries.size(); ++nidx_in_set) {
        for (auto tidx = 0; tidx < n_threads_; ++tidx) {
          entries[nidx_in_set].split.Update(tloc_candidates[n_threads_ * nidx_in_set + tidx].split);
        }
        if (IsPulsar()) {
          nids += entries[nidx_in_set].nid;
        }
      }
      if (IsPulsar()) {
        xgb_pulsar_->GetEncryptedSplitsByLayer(nids, splitsRequests);
      }
      auto srs = splitsResponses.mutable_splits_responses();
      for (unsigned nidx_in_set = 0; nidx_in_set < entries.size(); ++nidx_in_set) {
        std::atomic<int> bestIdx{-1};
        UpdateForDataHolder(entries[nidx_in_set], evaluator,
                            IsPulsar() ? &splitsRequests.splits_requests(nidx_in_set) : nullptr,
                            bestIdx);
        if (IsPulsar()) {
          auto sr = srs->Add();
          xgb_pulsar_->HandleBestSplit(bestIdx.load(), entries[nidx_in_set],
                                       splitsRequests.splits_requests(nidx_in_set), *sr);
        }
      }
      if (IsPulsar()) {
        xgb_pulsar_->SendBestSplitsByLayer(nids, splitsResponses);
      }
    } else {
      auto srs = splitsRequests.mutable_splits_requests();
      for (unsigned nidx_in_set = 0; nidx_in_set < entries.size(); ++nidx_in_set) {
        for (auto tidx = 0; tidx < n_threads_; ++tidx) {
          splits_requests[nidx_in_set].mutable_encrypted_splits()->MergeFrom(
              tloc_requests[n_threads_ * nidx_in_set + tidx].encrypted_splits());
        }
        splits_requests[nidx_in_set].set_nidx(entries[nidx_in_set].nid);
        splits_requests[nidx_in_set].set_part_id(fparam_->fl_part_id);
        if (IsPulsar()) {
          nids += entries[nidx_in_set].nid;
          srs->Add(std::move(splits_requests[nidx_in_set]));
        } else {
          DataHolderUpdate(cut, feature_types, entries[nidx_in_set], splits_requests[nidx_in_set],
                           [&](function<void(SplitsResponse &)> process_best_split) {
                             xgb_client_->SendEncryptedSplits(splits_requests[nidx_in_set],
                                                              process_best_split);
                           });
        }
      }
      if (IsPulsar()) {
        xgb_pulsar_->SendEncryptedSplitsByLayer(nids, splitsRequests);
        xgb_pulsar_->GetBestSplitsByLayer(nids, splitsResponses);
        for (unsigned nidx_in_set = 0; nidx_in_set < entries.size(); ++nidx_in_set) {
          auto splitsResponse = splitsResponses.splits_responses(nidx_in_set);
          DataHolderUpdate(cut, feature_types, entries[nidx_in_set], splits_requests[nidx_in_set],
                           [&](function<void(SplitsResponse &)> process_best_split) {
                             process_best_split(splitsResponse);
                           });
        }
      }
    }
  }

  // Add splits to tree, handles all statistic
  void ApplyTreeSplit(ExpandEntry const &candidate, RegTree *p_tree) {
    auto evaluator = tree_evaluator_.GetEvaluator();
    RegTree &tree = *p_tree;

    float base_weight = 0, left_weight = 0, right_weight = 0;
    GradStats<> parent_sum = candidate.split.left_sum;

    if (is_same<double, H>()) {
      parent_sum.Add(candidate.split.right_sum);
      base_weight = evaluator.CalcWeight(candidate.nid, param_, parent_sum);
      left_weight = evaluator.CalcWeight(candidate.nid, param_, candidate.split.left_sum);
      right_weight = evaluator.CalcWeight(candidate.nid, param_, candidate.split.right_sum);
    }

    if (candidate.split.is_cat) {
      tree.ExpandCategorical(candidate.nid, candidate.split.SplitIndex(), candidate.split.cat_bits,
                             candidate.split.DefaultLeft(), base_weight,
                             left_weight * param_.learning_rate,
                             right_weight * param_.learning_rate, candidate.split.loss_chg,
                             parent_sum.GetHess(), candidate.split.left_sum.GetHess(),
                             candidate.split.right_sum.GetHess(), candidate.split.part_id);
    } else {
      tree.ExpandNode(candidate.nid, candidate.split.SplitIndex(), candidate.split.split_value,
                      candidate.split.DefaultLeft(), base_weight,
                      left_weight * param_.learning_rate, right_weight * param_.learning_rate,
                      candidate.split.loss_chg, parent_sum.GetHess(),
                      candidate.split.left_sum.GetHess(), candidate.split.right_sum.GetHess(),
                      tree.kInvalidNodeId, candidate.split.part_id);
    }

    // Set up child constraints
    auto left_child = tree[candidate.nid].LeftChild();
    auto right_child = tree[candidate.nid].RightChild();
    tree_evaluator_.AddSplit(candidate.nid, left_child, right_child,
                             tree[candidate.nid].SplitIndex(), left_weight, right_weight);

    auto max_node = std::max(left_child, tree[candidate.nid].RightChild());
    max_node = std::max(candidate.nid, max_node);
    snode_.resize(tree.GetNodes().size());

    H empty;
    SetNodeEntry(candidate.nid, candidate.split, evaluator, left_child, right_child, empty);

    interaction_constraints_.Split(candidate.nid, tree[candidate.nid].SplitIndex(), left_child,
                                   right_child);
  }

  void SetNodeEntry(int nid, const SplitEntry<> &split,
                    const TreeEvaluator::SplitEvaluator<TrainParam> &evaluator, int left_child,
                    int right_child, double &empty) {
    snode_.at(left_child).stats = split.left_sum;
    snode_.at(left_child).root_gain = evaluator.CalcGain(nid, param_, split.left_sum);
    snode_.at(right_child).stats = split.right_sum;
    snode_.at(right_child).root_gain = evaluator.CalcGain(nid, param_, split.right_sum);
  }

  void SetNodeEntry(int nid, const SplitEntry<> &split,
                    const TreeEvaluator::SplitEvaluator<TrainParam> &evaluator, int left_child,
                    int right_child, EncryptedType<double> &empty) {
    auto *ls = split.encrypted_left_sum.get();
    auto *rs = split.encrypted_right_sum.get();
    if (ls != nullptr) {
      snode_.at(left_child).stats = *ls;
    }
    if (rs != nullptr) {
      snode_.at(right_child).stats = *rs;
    }
  }

  auto Evaluator() const { return tree_evaluator_.GetEvaluator(); }

  auto const &Stats() const { return snode_; }

  void InitNodeStat(GradStats<H> const &stat_sum, bst_node_t i = 0) {
    if (i == 0) {
      snode_.resize(1);
    }

    snode_[i].stats = GradStats<H>{stat_sum.GetGrad(), stat_sum.GetHess()};
  }

  float InitSplitNode(GradStats<double> const &stat_sum, bst_node_t i = 0) {
    InitNodeStat(stat_sum, i);

    auto root_evaluator = tree_evaluator_.GetEvaluator();
    snode_[i].root_gain =
        root_evaluator.CalcGain(RegTree::kRoot, param_, GradStats<H>{snode_[0].stats});
    auto weight = root_evaluator.CalcWeight(RegTree::kRoot, param_, GradStats<H>{snode_[0].stats});

    return weight;
  }

 public:
  // The column sampler must be constructed by caller since we need to preserve the rng
  // for the entire training session.
  explicit HistEvaluator(TrainParam const &param, MetaInfo const &info, int32_t n_threads,
                         std::shared_ptr<common::ColumnSampler> sampler)
      : param_{param},
        column_sampler_{std::move(sampler)},
        tree_evaluator_{param, static_cast<bst_feature_t>(info.num_col_), GenericParameter::kCpuId},
        n_threads_{n_threads} {
    interaction_constraints_.Configure(param, info.num_col_);
    column_sampler_->Init(info.num_col_, info.feature_weights.HostVector(), param_.colsample_bynode,
                          param_.colsample_bylevel, param_.colsample_bytree);
    if (!is_same<double, H>()) {
      key_ = generate_key();
    }
  }
};

/**
 * \brief CPU implementation of update prediction cache, which calculates the leaf value
 *        for the last tree and accumulates it to prediction vector.
 *
 * \param p_last_tree The last tree being updated by tree updater
 */
template <typename Partitioner>
void UpdatePredictionCacheImpl(GenericParameter const *ctx, RegTree const *p_last_tree,
                               std::vector<Partitioner> const &partitioner,
                               linalg::VectorView<float> out_preds) {
  CHECK_GT(out_preds.Size(), 0U);

  CHECK(p_last_tree);
  auto const &tree = *p_last_tree;
  CHECK_EQ(out_preds.DeviceIdx(), GenericParameter::kCpuId);
  size_t n_nodes = p_last_tree->GetNodes().size();
  for (auto &part : partitioner) {
    CHECK_EQ(part.Size(), n_nodes);
    common::BlockedSpace2d space(
        part.Size(), [&](size_t node) { return part[node].Size(); }, 1024);
    common::ParallelFor2d(space, ctx->Threads(), [&](size_t nidx, common::Range1d r) {
      if (!tree[nidx].IsDeleted() && tree[nidx].IsLeaf()) {
        auto const &rowset = part[nidx];
        auto leaf_value = tree[nidx].LeafValue();
        for (const size_t *it = rowset.begin + r.begin(); it < rowset.begin + r.end(); ++it) {
          out_preds(*it) += leaf_value;
        }
      }
    });
  }
}
}  // namespace tree
}  // namespace xgboost
#endif  // XGBOOST_TREE_HIST_EVALUATE_SPLITS_H_
