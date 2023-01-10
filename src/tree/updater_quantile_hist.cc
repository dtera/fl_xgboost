/*!
 * Copyright 2017-2022 by XGBoost Contributors
 * \file updater_quantile_hist.cc
 * \brief use quantized feature values to construct a tree
 * \author Philip Cho, Tianqi Checn, Egor Smirnov
 */
#include "./updater_quantile_hist.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common_row_partitioner.h"
#include "constraints.h"
#include "hist/evaluate_splits.h"
#include "hist/histogram.h"
#include "param.h"
#include "xgboost/logging.h"
#include "xgboost/tree_updater.h"

namespace xgboost {
namespace tree {

DMLC_REGISTRY_FILE_TAG(updater_quantile_hist);

void QuantileHistMaker::Configure(const Args &args) {
  param_.UpdateAllowUnknown(args);
  fparam_.UpdateAllowUnknown(args);
}

template <typename T>
inline void QuantileHistMaker::UpdateT(HostDeviceVector<GradientPairT<T>> *gpair, DMatrix *dmat,
                                       common::Span<HostDeviceVector<bst_node_t>> out_position,
                                       const std::vector<RegTree *> &trees) {
  // rescale learning rate according to size of trees
  float lr = param_.learning_rate;
  param_.learning_rate = lr / trees.size();

  // build tree
  const size_t n_trees = trees.size();
  if (!pimpl_) {
    pimpl_.reset(new Builder(n_trees, param_, fparam_, dmat, task_, ctx_));
  }

  size_t t_idx{0};
  for (auto p_tree : trees) {
    auto &t_row_position = out_position[t_idx];
    this->pimpl_->UpdateTree<T>(gpair, dmat, p_tree, &t_row_position);
    ++t_idx;
  }

  param_.learning_rate = lr;
}

void QuantileHistMaker::Update(HostDeviceVector<GradientPair> *gpair, DMatrix *dmat,
                               common::Span<HostDeviceVector<bst_node_t>> out_position,
                               const std::vector<RegTree *> &trees) {
  UpdateT<float>(gpair, dmat, out_position, trees);
}

void QuantileHistMaker::Update(HostDeviceVector<EncryptedGradientPair> *gpair, DMatrix *dmat,
                               common::Span<HostDeviceVector<bst_node_t>> out_position,
                               const std::vector<RegTree *> &trees) {
  UpdateT<EncryptedType<float>>(gpair, dmat, out_position, trees);
}

bool QuantileHistMaker::UpdatePredictionCache(const DMatrix *data,
                                              linalg::VectorView<float> out_preds) {
  if (pimpl_) {
    return pimpl_->UpdatePredictionCache(data, out_preds);
  } else {
    return false;
  }
}

template <typename T>
CPUExpandEntry QuantileHistMaker::Builder::InitRoot(DMatrix *p_fmat, RegTree *p_tree,
                                                    const std::vector<GradientPairT<T>> &gpair_h) {
  CPUExpandEntry node(RegTree::kRoot, p_tree->GetDepth(0), 0.0f);

  size_t page_id = 0;
  auto space = ConstructHistSpace(partitioner_, {node});
  for (auto const &gidx : p_fmat->GetBatches<GHistIndexMatrix>(HistBatch(param_))) {
    std::vector<CPUExpandEntry> nodes_to_build{node};
    std::vector<CPUExpandEntry> nodes_to_sub;
    BuildHist(page_id, space, gidx, p_tree, partitioner_.at(page_id).Partitions(), nodes_to_build,
              nodes_to_sub, gpair_h);

    ++page_id;
  }

  grad_stat.Clear();
  encrypted_grad_stat.Clear();
  {
    if (p_fmat->IsDense()) {
      /**
       * Specialized code for dense data: For dense data (with no missing value), the sum
       * of gradient histogram is equal to snode[nid]
       */
      auto const &gmat = *(p_fmat->GetBatches<GHistIndexMatrix>(HistBatch(param_)).begin());
      std::vector<uint32_t> const &row_ptr = gmat.cut.Ptrs();
      CHECK_GE(row_ptr.size(), 2);
      uint32_t const ibegin = row_ptr[0];
      uint32_t const iend = row_ptr[1];
      if (is_same<float, T>()) {
        auto hist = this->histogram_builder_->Histogram()[RegTree::kRoot];
        auto begin = hist.data();
        for (uint32_t i = ibegin; i < iend; ++i) {
          GradientPairT<double> const &et = begin[i];
          grad_stat.Add(et.GetGrad(), et.GetHess());
        }
      } else {
        auto hist = this->encrypted_histogram_builder_->Histogram()[RegTree::kRoot];
        auto begin = hist.data();
        for (uint32_t i = ibegin; i < iend; ++i) {
          GradientPairT<EncryptedType<double>> const &et = begin[i];
          encrypted_grad_stat.Add(et.GetGrad(), et.GetHess());
        }
      }
    } else {
      for (auto const &grad : gpair_h) {
        AddGradPair(grad);
      }
      collective::Allreduce<collective::Operation::kSum>(reinterpret_cast<double *>(&grad_stat), 2);
    }

    if (is_same<float, T>()) {
      auto weight = evaluator_->InitRoot(GradStats<>{grad_stat});
      p_tree->Stat(RegTree::kRoot).sum_hess = grad_stat.GetHess();
      p_tree->Stat(RegTree::kRoot).base_weight = weight;
      (*p_tree)[RegTree::kRoot].SetLeaf(param_.learning_rate * weight);
    } else {
      encrypted_evaluator_->InitRoot(GradStats<EncryptedType<double>>{encrypted_grad_stat});
    }

    std::vector<CPUExpandEntry> entries{node};
    monitor_->Start("EvaluateSplits");
    auto ft = p_fmat->Info().feature_types.ConstHostSpan();
    for (auto const &gmat : p_fmat->GetBatches<GHistIndexMatrix>(HistBatch(param_))) {
      if (is_same<float, T>()) {
        evaluator_->EvaluateSplits(histogram_builder_->Histogram(), gmat.cut, ft, *p_tree,
                                   &entries);
      } else {
        // evaluate splits for the data holder part
        encrypted_evaluator_->EvaluateSplits(encrypted_histogram_builder_->Histogram(), gmat.cut,
                                             ft, *p_tree, &entries);
      }
      break;
    }
    monitor_->Stop("EvaluateSplits");
    node = entries.front();
  }

  return node;
}

template <typename T>
void QuantileHistMaker::Builder::BuildHistogram(DMatrix *p_fmat, RegTree *p_tree,
                                                std::vector<CPUExpandEntry> const &valid_candidates,
                                                std::vector<GradientPairT<T>> const &gpair) {
  std::vector<CPUExpandEntry> nodes_to_build(valid_candidates.size());
  std::vector<CPUExpandEntry> nodes_to_sub(valid_candidates.size());

  size_t n_idx = 0;
  for (auto const &c : valid_candidates) {
    auto left_nidx = (*p_tree)[c.nid].LeftChild();
    auto right_nidx = (*p_tree)[c.nid].RightChild();
    auto fewer_right = c.split.right_sum.GetHess() < c.split.left_sum.GetHess();

    auto build_nidx = left_nidx;
    auto subtract_nidx = right_nidx;
    if (fewer_right) {
      std::swap(build_nidx, subtract_nidx);
    }
    nodes_to_build[n_idx] = CPUExpandEntry{build_nidx, p_tree->GetDepth(build_nidx), {}};
    nodes_to_sub[n_idx] = CPUExpandEntry{subtract_nidx, p_tree->GetDepth(subtract_nidx), {}};
    n_idx++;
  }

  size_t page_id{0};
  auto space = ConstructHistSpace(partitioner_, nodes_to_build);
  for (auto const &gidx : p_fmat->GetBatches<GHistIndexMatrix>(HistBatch(param_))) {
    BuildHist(page_id, space, gidx, p_tree, partitioner_.at(page_id).Partitions(), nodes_to_build,
              nodes_to_sub, gpair);
    ++page_id;
  }
}

void QuantileHistMaker::Builder::LeafPartition(RegTree const &tree,
                                               common::Span<GradientPair const> gpair,
                                               std::vector<bst_node_t> *p_out_position) {
  monitor_->Start(__func__);
  if (!task_.UpdateTreeLeaf()) {
    return;
  }
  for (auto const &part : partitioner_) {
    part.LeafPartition(ctx_, tree, gpair, p_out_position);
  }
  monitor_->Stop(__func__);
}

void QuantileHistMaker::Builder::LeafPartition(RegTree const &tree,
                                               common::Span<EncryptedGradientPair const> gpair,
                                               std::vector<bst_node_t> *p_out_position) {
  monitor_->Start(__func__);
  if (!task_.UpdateTreeLeaf()) {
    return;
  }
  for (auto const &part : partitioner_) {
    // TODO
    // part.LeafPartition(ctx_, tree, gpair, p_out_position);
  }
  monitor_->Stop(__func__);
}

template <typename T>
void QuantileHistMaker::Builder::ExpandTree(DMatrix *p_fmat, RegTree *p_tree,
                                            const std::vector<GradientPairT<T>> &gpair_h,
                                            HostDeviceVector<bst_node_t> *p_out_position) {
  monitor_->Start(__func__);

  Driver<CPUExpandEntry> driver(param_);
  driver.Push(this->InitRoot<T>(p_fmat, p_tree, gpair_h));
  auto const &tree = *p_tree;
  auto expand_set = driver.Pop();

  while (!expand_set.empty()) {
    // candidates that can be further splited.
    std::vector<CPUExpandEntry> valid_candidates;
    // candidaates that can be applied.
    std::vector<CPUExpandEntry> applied;
    int32_t depth = expand_set.front().depth + 1;
    for (auto const &candidate : expand_set) {
      evaluator_->ApplyTreeSplit(candidate, p_tree);
      applied.push_back(candidate);
      if (driver.IsChildValid(candidate)) {
        valid_candidates.emplace_back(candidate);
      }
    }

    monitor_->Start("UpdatePosition");
    size_t page_id{0};
    for (auto const &page : p_fmat->GetBatches<GHistIndexMatrix>(HistBatch(param_))) {
      partitioner_.at(page_id).UpdatePosition(ctx_, page, applied, p_tree);
      ++page_id;
    }
    monitor_->Stop("UpdatePosition");

    std::vector<CPUExpandEntry> best_splits;
    if (!valid_candidates.empty()) {
      this->BuildHistogram(p_fmat, p_tree, valid_candidates, gpair_h);
      for (auto const &candidate : valid_candidates) {
        int left_child_nidx = tree[candidate.nid].LeftChild();
        int right_child_nidx = tree[candidate.nid].RightChild();
        CPUExpandEntry l_best{left_child_nidx, depth, 0.0};
        CPUExpandEntry r_best{right_child_nidx, depth, 0.0};
        best_splits.push_back(l_best);
        best_splits.push_back(r_best);
      }
      auto const &histograms = histogram_builder_->Histogram();
      auto ft = p_fmat->Info().feature_types.ConstHostSpan();
      for (auto const &gmat : p_fmat->GetBatches<GHistIndexMatrix>(HistBatch(param_))) {
        evaluator_->EvaluateSplits(histograms, gmat.cut, ft, *p_tree, &best_splits);
        break;
      }
    }
    driver.Push(best_splits.begin(), best_splits.end());
    expand_set = driver.Pop();
  }

  auto &h_out_position = p_out_position->HostVector();
  this->LeafPartition(tree, gpair_h, &h_out_position);
  monitor_->Stop(__func__);
}

template <typename T>
void QuantileHistMaker::Builder::UpdateTree(HostDeviceVector<GradientPairT<T>> *gpair,
                                            DMatrix *p_fmat, RegTree *p_tree,
                                            HostDeviceVector<bst_node_t> *p_out_position) {
  monitor_->Start(__func__);

  std::vector<GradientPairT<T>> *gpair_ptr = &(gpair->HostVector());
  // in case 'num_parallel_trees != 1' no posibility to change initial gpair
  SetGradPairLocal(gpair_ptr);
  /*if (GetNumberOfTrees() != 1) {
    gpair_local_.resize(gpair_ptr->size());
    gpair_local_ = *gpair_ptr;
    gpair_ptr = &gpair_local_;
  }*/

  this->InitData<T>(p_fmat, *p_tree, gpair_ptr);

  ExpandTree<T>(p_fmat, p_tree, *gpair_ptr, p_out_position);
  monitor_->Stop(__func__);
}

bool QuantileHistMaker::Builder::UpdatePredictionCache(DMatrix const *data,
                                                       linalg::VectorView<float> out_preds) const {
  // p_last_fmat_ is a valid pointer as long as UpdatePredictionCache() is called in
  // conjunction with Update().
  if (!p_last_fmat_ || !p_last_tree_ || data != p_last_fmat_) {
    return false;
  }
  monitor_->Start(__func__);
  CHECK_EQ(out_preds.Size(), data->Info().num_row_);
  UpdatePredictionCacheImpl(ctx_, p_last_tree_, partitioner_, out_preds);
  monitor_->Stop(__func__);
  return true;
}

template <typename T>
void QuantileHistMaker::Builder::InitSampling(const DMatrix &fmat,
                                              std::vector<GradientPairT<T>> *gpair) {
  monitor_->Start(__func__);
  const auto &info = fmat.Info();
  auto &rnd = common::GlobalRandom();
  std::vector<GradientPairT<T>> &gpair_ref = *gpair;

#if XGBOOST_CUSTOMIZE_GLOBAL_PRNG
  std::bernoulli_distribution coin_flip(param_.subsample);
  for (size_t i = 0; i < info.num_row_; ++i) {
    if (!(gpair_ref[i].GetHess() >= 0.0f && coin_flip(rnd)) || gpair_ref[i].GetGrad() == 0.0f) {
      gpair_ref[i] = GradientPairT<T>(0);
    }
  }
#else
  uint64_t initial_seed = rnd();

  auto n_threads = static_cast<size_t>(ctx_->Threads());
  const size_t discard_size = info.num_row_ / n_threads;
  std::bernoulli_distribution coin_flip(param_.subsample);

  dmlc::OMPException exc;
#pragma omp parallel num_threads(n_threads)
  {
    exc.Run([&]() {
      const size_t tid = omp_get_thread_num();
      const size_t ibegin = tid * discard_size;
      const size_t iend = (tid == (n_threads - 1)) ? info.num_row_ : ibegin + discard_size;
      RandomReplace::MakeIf(
          [&](size_t i, RandomReplace::EngineT &eng) {
            // return !(gpair_ref[i].GetHess() >= 0.0f && coin_flip(eng));
            return !(coin_flip(eng));
          },
          GradientPairT<T>(T(0), T(0)), initial_seed, ibegin, iend, &gpair_ref);
    });
  }
  exc.Rethrow();
#endif  // XGBOOST_CUSTOMIZE_GLOBAL_PRNG
  monitor_->Stop(__func__);
}

size_t QuantileHistMaker::Builder::GetNumberOfTrees() { return n_trees_; }

void QuantileHistMaker::Builder::SetGradPairLocal(std::vector<GradientPair> *gpair_ptr) {
  if (GetNumberOfTrees() != 1) {
    gpair_local_.resize(gpair_ptr->size());
    gpair_local_ = *gpair_ptr;
    gpair_ptr = &gpair_local_;
  }
}

void QuantileHistMaker::Builder::SetGradPairLocal(std::vector<EncryptedGradientPair> *gpair_ptr) {
  if (GetNumberOfTrees() != 1) {
    encrypted_gpair_local_.resize(gpair_ptr->size());
    encrypted_gpair_local_ = *gpair_ptr;
    gpair_ptr = &encrypted_gpair_local_;
  }
}

void QuantileHistMaker::Builder::AddGradPair(const GradientPair &gpair) {
  grad_stat.Add(gpair.GetGrad(), gpair.GetHess());
}

void QuantileHistMaker::Builder::AddGradPair(const EncryptedGradientPair &gpair) {
  encrypted_grad_stat.EncryptedAdd(gpair.GetGrad(), gpair.GetHess());
}

template <typename ExpandEntry>
void QuantileHistMaker::Builder::BuildHist(
    size_t page_id, common::BlockedSpace2d space, GHistIndexMatrix const &gidx, RegTree *p_tree,
    common::RowSetCollection const &row_set_collection,
    std::vector<ExpandEntry> const &nodes_for_explicit_hist_build,
    std::vector<ExpandEntry> const &nodes_for_subtraction_trick,
    std::vector<GradientPair> const &gpair) {
  this->histogram_builder_->BuildHist(page_id, space, gidx, p_tree, row_set_collection,
                                      nodes_for_explicit_hist_build, nodes_for_subtraction_trick,
                                      gpair);
}

template <typename ExpandEntry>
void QuantileHistMaker::Builder::BuildHist(
    size_t page_id, common::BlockedSpace2d space, GHistIndexMatrix const &gidx, RegTree *p_tree,
    common::RowSetCollection const &row_set_collection,
    std::vector<ExpandEntry> const &nodes_for_explicit_hist_build,
    std::vector<ExpandEntry> const &nodes_for_subtraction_trick,
    std::vector<EncryptedGradientPair> const &gpair) {
  this->encrypted_histogram_builder_->BuildHist(page_id, space, gidx, p_tree, row_set_collection,
                                                nodes_for_explicit_hist_build,
                                                nodes_for_subtraction_trick, gpair);
}

template <typename T>
void QuantileHistMaker::Builder::InitData(DMatrix *fmat, const RegTree &tree,
                                          std::vector<GradientPairT<T>> *gpair) {
  monitor_->Start(__func__);
  const auto &info = fmat->Info();

  {
    size_t page_id{0};
    int32_t n_total_bins{0};
    partitioner_.clear();
    for (auto const &page : fmat->GetBatches<GHistIndexMatrix>(HistBatch(param_))) {
      if (n_total_bins == 0) {
        n_total_bins = page.cut.TotalBins();
      } else {
        CHECK_EQ(n_total_bins, page.cut.TotalBins());
      }
      partitioner_.emplace_back(this->ctx_, page.Size(), page.base_rowid);
      ++page_id;
    }
    if (is_same<float, T>()) {
      histogram_builder_->Reset(n_total_bins, HistBatch(param_), ctx_->Threads(), page_id,
                                collective::IsDistributed());
      evaluator_.reset(new HistEvaluator<CPUExpandEntry>{param_, fparam_, info,
                                                         this->ctx_->Threads(), column_sampler_});
    } else {
      encrypted_histogram_builder_->Reset(n_total_bins, HistBatch(param_), ctx_->Threads(), page_id,
                                          collective::IsDistributed());
      encrypted_evaluator_.reset(new HistEvaluator<CPUExpandEntry, EncryptedType<double>>{
          param_, fparam_, info, this->ctx_->Threads(), column_sampler_});
    }

    if (param_.subsample < 1.0f) {
      CHECK_EQ(param_.sampling_method, TrainParam::kUniform)
          << "Only uniform sampling is supported, "
          << "gradient-based sampling is only support by GPU Hist.";
      InitSampling<T>(*fmat, gpair);
    }
  }

  // store a pointer to the tree
  p_last_tree_ = &tree;

  monitor_->Stop(__func__);
}

XGBOOST_REGISTER_TREE_UPDATER(QuantileHistMaker, "grow_quantile_histmaker")
    .describe("Grow tree using quantized histogram.")
    .set_body([](GenericParameter const *ctx, ObjInfo task) {
      return new QuantileHistMaker(ctx, task);
    });
}  // namespace tree
}  // namespace xgboost
