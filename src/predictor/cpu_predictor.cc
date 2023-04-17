/*!
 * Copyright by Contributors 2017-2021
 */
#include <dmlc/any.h>
#include <dmlc/omp.h>

#include <cstddef>
#include <limits>
#include <mutex>

#include "../common/categorical.h"
#include "../common/math.h"
#include "../common/threading_utils.h"
#include "../data/adapter.h"
#include "../data/gradient_index.h"
#include "../data/proxy_dmatrix.h"
#include "../gbm/gbtree_model.h"
#include "predict_fn.h"
#include "xgboost/base.h"
#include "xgboost/data.h"
#include "xgboost/host_device_vector.h"
#include "xgboost/logging.h"
#include "xgboost/predictor.h"
#include "xgboost/tree_model.h"
#include "xgboost/tree_updater.h"

namespace xgboost {
namespace predictor {

DMLC_REGISTRY_FILE_TAG(cpu_predictor);

template <bool has_missing, bool has_categorical>
bst_node_t GetNextNode(const RegTree &tree, bst_node_t nid, const RegTree::FVec &feat,
                       const RegTree::CategoricalSplitMatrix &cats) {
  unsigned split_index = tree[nid].SplitIndex();
  auto fvalue = feat.GetFvalue(split_index);
  auto next_nid = GetNextNode<has_missing, has_categorical>(
      tree[nid], nid, fvalue, has_missing && feat.IsMissing(split_index), cats);
  return next_nid;
}

template <bool has_missing, bool has_categorical>
bool FlowLeft(const RegTree &tree, bst_node_t nid, const RegTree::FVec &feat,
              const RegTree::CategoricalSplitMatrix &cats) {
  unsigned split_index = tree[nid].SplitIndex();
  auto fvalue = feat.GetFvalue(split_index);
  return FlowLeft<has_missing, has_categorical>(tree[nid], nid, fvalue,
                                                has_missing && feat.IsMissing(split_index), cats);
}

template <bool has_missing, bool has_categorical>
bst_node_t GetLeafIndex(RegTree const &tree, const RegTree::FVec &feat,
                        RegTree::CategoricalSplitMatrix const &cats, size_t k) {
  bst_node_t nid = 0;  // , prev_nid = nid;
  /*if (IsFederated()) {
    while (!tree[nid].IsLeaf()) {
      int64_t pid = nid;  // (k << 32) | nid;
      if (NotSelfPart(tree[nid].PartId())) {
        if (IsGuest()) {
          xgb_server_->GetNextNode(
              k, pid, [&](bool flow_left) { nid = tree[nid].LeftChild() + !flow_left; });
        } else {
          xgb_client_->GetNextNode(
              k, pid, [&](bool flow_left) { nid = tree[nid].LeftChild() + !flow_left; });
        }
      } else {
        auto flow_left = FlowLeft<has_missing, has_categorical>(tree, nid, feat, cats);
        nid = GetNextNode<has_missing, has_categorical>(tree, nid, feat, cats);
        if (IsGuest()) {
          xgb_server_->SendNextNode(k, pid, flow_left);
        } else {
          xgb_client_->SendNextNode(k, pid, flow_left);
        }
      }
    }
  } else {
    while (!tree[nid].IsLeaf()) {
      nid = GetNextNode<has_missing, has_categorical>(tree, nid, feat, cats);
    }
  }*/
  while (!tree[nid].IsLeaf()) {
    nid = GetNextNode<has_missing, has_categorical>(tree, nid, feat, cats);
  }
  return nid;
}

bst_float PredValue(const SparsePage::Inst &inst,
                    const std::vector<std::unique_ptr<RegTree>> &trees,
                    const std::vector<int> &tree_info, int bst_group, RegTree::FVec *p_feats,
                    unsigned tree_begin, unsigned tree_end) {
  bst_float psum = 0.0f;
  p_feats->Fill(inst);
  for (size_t i = tree_begin; i < tree_end; ++i) {
    if (tree_info[i] == bst_group) {
      auto const &tree = *trees[i];
      bool has_categorical = tree.HasCategoricalSplit();
      auto cats = tree.GetCategoriesMatrix();
      bst_node_t nidx = -1;
      if (has_categorical) {
        nidx = GetLeafIndex<true, true>(tree, *p_feats, cats, bst_group);
      } else {
        nidx = GetLeafIndex<true, false>(tree, *p_feats, cats, bst_group);
      }
      psum += (*trees[i])[nidx].LeafValue();
    }
  }
  p_feats->Drop(inst);
  return psum;
}

template <bool has_categorical>
bst_float PredValueByOneTree(const RegTree::FVec &p_feats, RegTree const &tree,
                             RegTree::CategoricalSplitMatrix const &cats, size_t k) {
  const bst_node_t leaf = p_feats.HasMissing()
                              ? GetLeafIndex<true, has_categorical>(tree, p_feats, cats, k)
                              : GetLeafIndex<false, has_categorical>(tree, p_feats, cats, k);
  return tree[leaf].LeafValue();
}

void PredictByAllTrees(gbm::GBTreeModel const &model, const size_t tree_begin,
                       const size_t tree_end, std::vector<bst_float> *out_preds,
                       const size_t predict_offset, const size_t num_group,
                       const std::vector<RegTree::FVec> &thread_temp, const size_t offset,
                       const size_t block_size) {
  std::vector<bst_float> &preds = *out_preds;
  for (size_t tree_id = tree_begin; tree_id < tree_end; ++tree_id) {
    const size_t gid = model.tree_info[tree_id];
    auto const &tree = *model.trees[tree_id];
    auto const &cats = tree.GetCategoriesMatrix();
    auto has_categorical = tree.HasCategoricalSplit();

    if (has_categorical) {
      for (size_t i = 0; i < block_size; ++i) {
        size_t k = (predict_offset + i) * num_group + gid;
        preds[k] += PredValueByOneTree<true>(thread_temp[offset + i], tree, cats, k);
      }
    } else {
      for (size_t i = 0; i < block_size; ++i) {
        size_t k = (predict_offset + i) * num_group + gid;
        preds[k] += PredValueByOneTree<false>(thread_temp[offset + i], tree, cats, k);
      }
    }
  }
}

template <bool has_categorical>
void FlowToByOneTree(const RegTree::FVec &p_feats, RegTree const &tree,
                     RegTree::CategoricalSplitMatrix const &cats, size_t k, bst_node_t leaf_nids[],
                     oneapi::tbb::concurrent_unordered_map<uint32_t, uint32_t> &self_part_idxs,
                     oneapi::tbb::concurrent_unordered_set<bst_node_t> &other_part_idxs) {
  if (!tree[leaf_nids[k]].IsLeaf()) {
    if (NotSelfPart(tree[leaf_nids[k]].PartId())) {
      other_part_idxs.insert(tree[leaf_nids[k]].PartId());
    } else {
      if (p_feats.HasMissing()) {
        leaf_nids[k] = GetNextNode<true, has_categorical>(tree, leaf_nids[k], p_feats, cats);
      } else {
        leaf_nids[k] = GetNextNode<false, has_categorical>(tree, leaf_nids[k], p_feats, cats);
      }
      self_part_idxs.insert({k, leaf_nids[k]});
    }
  }
}

template <typename DataView>
void FVecFill(const size_t block_size, const size_t batch_offset, const int num_feature,
              DataView *batch, const size_t fvec_offset, std::vector<RegTree::FVec> *p_feats) {
  for (size_t i = 0; i < block_size; ++i) {
    RegTree::FVec &feats = (*p_feats)[fvec_offset + i];
    if (feats.Size() == 0) {
      feats.Init(num_feature);
    }
    const SparsePage::Inst inst = (*batch)[batch_offset + i];
    feats.Fill(inst);
  }
}

template <typename DataView>
void FVecDrop(const size_t block_size, const size_t batch_offset, DataView *batch,
              const size_t fvec_offset, std::vector<RegTree::FVec> *p_feats) {
  for (size_t i = 0; i < block_size; ++i) {
    RegTree::FVec &feats = (*p_feats)[fvec_offset + i];
    const SparsePage::Inst inst = (*batch)[batch_offset + i];
    feats.Drop(inst);
  }
}

namespace {
static size_t constexpr kUnroll = 8;
}  // anonymous namespace

struct SparsePageView {
  bst_row_t base_rowid;
  HostSparsePageView view;

  explicit SparsePageView(SparsePage const *p) : base_rowid{p->base_rowid} { view = p->GetView(); }
  SparsePage::Inst operator[](size_t i) { return view[i]; }
  size_t Size() const { return view.Size(); }
};

struct GHistIndexMatrixView {
 private:
  GHistIndexMatrix const &page_;
  uint64_t n_features_;
  common::Span<FeatureType const> ft_;
  common::Span<Entry> workspace_;
  std::vector<size_t> current_unroll_;

 public:
  size_t base_rowid;

 public:
  GHistIndexMatrixView(GHistIndexMatrix const &_page, uint64_t n_feat,
                       common::Span<FeatureType const> ft, common::Span<Entry> workplace,
                       int32_t n_threads)
      : page_{_page},
        n_features_{n_feat},
        ft_{ft},
        workspace_{workplace},
        current_unroll_(n_threads > 0 ? n_threads : 1, 0),
        base_rowid{_page.base_rowid} {}

  SparsePage::Inst operator[](size_t r) {
    auto t = omp_get_thread_num();
    auto const beg = (n_features_ * kUnroll * t) + (current_unroll_[t] * n_features_);
    size_t non_missing{beg};

    for (bst_feature_t c = 0; c < n_features_; ++c) {
      float f = page_.GetFvalue(r, c, common::IsCat(ft_, c));
      if (!common::CheckNAN(f)) {
        workspace_[non_missing] = Entry{c, f};
        ++non_missing;
      }
    }

    auto ret = workspace_.subspan(beg, non_missing - beg);
    current_unroll_[t]++;
    if (current_unroll_[t] == kUnroll) {
      current_unroll_[t] = 0;
    }
    return ret;
  }
  size_t Size() const { return page_.Size(); }
};

template <typename Adapter>
class AdapterView {
  Adapter *adapter_;
  float missing_;
  common::Span<Entry> workspace_;
  std::vector<size_t> current_unroll_;

 public:
  explicit AdapterView(Adapter *adapter, float missing, common::Span<Entry> workplace,
                       int32_t n_threads)
      : adapter_{adapter},
        missing_{missing},
        workspace_{workplace},
        current_unroll_(n_threads > 0 ? n_threads : 1, 0) {}
  SparsePage::Inst operator[](size_t i) {
    bst_feature_t columns = adapter_->NumColumns();
    auto const &batch = adapter_->Value();
    auto row = batch.GetLine(i);
    auto t = omp_get_thread_num();
    auto const beg = (columns * kUnroll * t) + (current_unroll_[t] * columns);
    size_t non_missing{beg};
    for (size_t c = 0; c < row.Size(); ++c) {
      auto e = row.GetElement(c);
      if (missing_ != e.value && !common::CheckNAN(e.value)) {
        workspace_[non_missing] = Entry{static_cast<bst_feature_t>(e.column_idx), e.value};
        ++non_missing;
      }
    }
    auto ret = workspace_.subspan(beg, non_missing - beg);
    current_unroll_[t]++;
    if (current_unroll_[t] == kUnroll) {
      current_unroll_[t] = 0;
    }
    return ret;
  }

  size_t Size() const { return adapter_->NumRows(); }

  bst_row_t const static base_rowid = 0;  // NOLINT
};

inline int GenIdxForNextIds(std::int32_t tree_id, std::int32_t part_id, std::int32_t depth) {
  return tree_id << 20 | part_id << 10 | depth;
}

template <typename DataView, size_t block_of_rows_size>
void PredictBatchByBlockOfRowsKernel(DataView batch, std::vector<bst_float> *out_preds,
                                     gbm::GBTreeModel const &model, int32_t tree_begin,
                                     int32_t tree_end, std::vector<RegTree::FVec> *p_thread_temp,
                                     int32_t n_threads) {
  auto &thread_temp = *p_thread_temp;
  int32_t const num_group = model.learner_model_param->num_output_group;

  CHECK_EQ(model.param.size_leaf_vector, 0) << "size_leaf_vector is enforced to 0 so far";
  // parallel over local batch
  const auto nsize = static_cast<bst_omp_uint>(batch.Size());
  const int num_feature = model.learner_model_param->num_feature;
  omp_ulong n_blocks = common::DivRoundUp(nsize, block_of_rows_size);

  auto parallel_for = [&](std::function<void(const size_t, const size_t, const size_t)> process) {
    common::ParallelFor(n_blocks, n_threads, [&](bst_omp_uint block_id) {
      const size_t batch_offset = block_id * block_of_rows_size;
      const size_t block_size = std::min(nsize - batch_offset, block_of_rows_size);
      const size_t fvec_offset = omp_get_thread_num() * block_of_rows_size;

      process(batch_offset, block_size, fvec_offset);
    });
  };

  if (IsFederated()) {
    if (IsGuest()) {
      // xgb_server_->ResizeNextNode(out_preds->size());
      xgb_server_->ClearNextNodeV2();
    } else {
      xgb_client_->Clear(1);
    }
    bst_node_t leaf_nids[tree_end - tree_begin][out_preds->size()];
    std::memset(leaf_nids, 0, sizeof(leaf_nids));
    oneapi::tbb::concurrent_unordered_map<uint32_t, uint32_t> self_part_idxs[tree_end - tree_begin];
    oneapi::tbb::concurrent_unordered_set<bst_node_t> other_part_idxs[tree_end - tree_begin];

    std::vector<bst_float> &preds = *out_preds;

    for (size_t tree_id = tree_begin; tree_id < tree_end; ++tree_id) {
      const size_t gid = model.tree_info[tree_id];
      auto const &tree = *model.trees[tree_id];
      auto const &cats = tree.GetCategoriesMatrix();
      auto has_categorical = tree.HasCategoricalSplit();

      std::size_t depth = 0;
      std::size_t j = tree_id - tree_begin;
      // LOG(CONSOLE) << "j: " << j << ", tree_id: " << tree_id << endl;
      do {
        other_part_idxs[j].clear();
        self_part_idxs[j].clear();
        parallel_for([&](const size_t batch_offset, const size_t block_size, const size_t offset) {
          FVecFill(block_size, batch_offset, num_feature, &batch, offset, p_thread_temp);
          auto predict_offset = batch_offset + batch.base_rowid;
          if (has_categorical) {
            for (size_t i = 0; i < block_size; ++i) {
              size_t k = (predict_offset + i) * num_group + gid;
              FlowToByOneTree<true>(thread_temp[offset + i], tree, cats, k, leaf_nids[i],
                                    self_part_idxs[j], other_part_idxs[j]);
            }
          } else {
            for (size_t i = 0; i < block_size; ++i) {
              size_t k = (predict_offset + i) * num_group + gid;
              FlowToByOneTree<false>(thread_temp[offset + i], tree, cats, k, leaf_nids[i],
                                     self_part_idxs[j], other_part_idxs[j]);
            }
          }
          FVecDrop(block_size, batch_offset, &batch, offset, p_thread_temp);
        });

        if (!self_part_idxs[j].empty()) {
          auto idx = GenIdxForNextIds(tree_id, fparam_->fl_part_id, depth);
          // send next node id to other part
          if (IsGuest()) {
            google::protobuf::Map<uint32_t, uint32_t> self_part_idx;
            for (auto part_idx : self_part_idxs[j]) {
              self_part_idx.insert({part_idx.first, part_idx.second});
            };
            xgb_server_->SendNextNodesV2(idx, self_part_idx);
          } else {
            xgb_client_->SendNextNodesV2(idx, self_part_idxs[j]);
          }
        }
        // LOG(CONSOLE) << "SendNextNodesV2 End, depth: " << depth << endl;

        if (!other_part_idxs[j].empty()) {
          std::for_each(
              other_part_idxs[j].begin(), other_part_idxs[j].end(), [&](bst_node_t other_part_id) {
                auto idx = GenIdxForNextIds(tree_id, other_part_id, depth);
                // receive next node id from other part
                auto process_part_idxs =
                    [&](const google::protobuf::Map<uint32_t, uint32_t> &next_ids) {
                      std::for_each(
                          next_ids.begin(), next_ids.end(),
                          [&](google::protobuf::MapPair<unsigned int, unsigned int> id_pair) {
                            leaf_nids[j][id_pair.first] = id_pair.second;
                          });
                    };
                // LOG(CONSOLE) << "idx: " << idx << ", other_part_id: " << other_part_id << endl;
                if (IsGuest()) {
                  xgb_server_->GetNextNodesV2(idx, process_part_idxs);
                } else {
                  xgb_client_->GetNextNodesV2(idx, process_part_idxs);
                }
              });
        }
        // LOG(CONSOLE) << "GetNextNodesV2 End, depth: " << depth << endl;

        /*cout << FederatedParam::getRole(fparam_->fl_role) << ": [" << leaf_nids[tree_id][0];
        for (int i = 1; i < out_preds->size() * 0.01; ++i) {
          cout << ", " << leaf_nids[tree_id][i];
        }
        cout << "]" << endl;*/
        depth++;
      } while (!self_part_idxs[j].empty() || !other_part_idxs[j].empty());

      if (IsGuest()) {
        parallel_for([&](const size_t batch_offset, const size_t block_size, const size_t) {
          auto predict_offset = batch_offset + batch.base_rowid;
          if (has_categorical) {
            for (size_t i = 0; i < block_size; ++i) {
              size_t k = (predict_offset + i) * num_group + gid;
              preds[k] += tree[leaf_nids[i][k]].LeafValue();
            }
          } else {
            for (size_t i = 0; i < block_size; ++i) {
              size_t k = (predict_offset + i) * num_group + gid;
              preds[k] += tree[leaf_nids[i][k]].LeafValue();
            }
          }
        });
      }
      // LOG(CONSOLE) << "depth: " << depth << endl;
    }
  } else {
    parallel_for([&](const size_t batch_offset, const size_t block_size, const size_t fvec_offset) {
      FVecFill(block_size, batch_offset, num_feature, &batch, fvec_offset, p_thread_temp);
      // process block of rows through all trees to keep cache locality
      PredictByAllTrees(model, tree_begin, tree_end, out_preds, batch_offset + batch.base_rowid,
                        num_group, thread_temp, fvec_offset, block_size);
      FVecDrop(block_size, batch_offset, &batch, fvec_offset, p_thread_temp);
    });
  }
}

float FillNodeMeanValues(RegTree const *tree, bst_node_t nidx, std::vector<float> *mean_values) {
  bst_float result;
  auto &node = (*tree)[nidx];
  auto &node_mean_values = *mean_values;
  if (node.IsLeaf()) {
    result = node.LeafValue();
  } else {
    result = FillNodeMeanValues(tree, node.LeftChild(), mean_values) *
             tree->Stat(node.LeftChild()).sum_hess;
    result += FillNodeMeanValues(tree, node.RightChild(), mean_values) *
              tree->Stat(node.RightChild()).sum_hess;
    result /= tree->Stat(nidx).sum_hess;
  }
  node_mean_values[nidx] = result;
  return result;
}

void FillNodeMeanValues(RegTree const *tree, std::vector<float> *mean_values) {
  size_t num_nodes = tree->param.num_nodes;
  if (mean_values->size() == num_nodes) {
    return;
  }
  mean_values->resize(num_nodes);
  FillNodeMeanValues(tree, 0, mean_values);
}

class CPUPredictor : public Predictor {
 protected:
  // init thread buffers
  static void InitThreadTemp(int nthread, std::vector<RegTree::FVec> *out) {
    int prev_thread_temp_size = out->size();
    if (prev_thread_temp_size < nthread) {
      out->resize(nthread, RegTree::FVec());
    }
  }

  void PredictGHistIndex(DMatrix *p_fmat, gbm::GBTreeModel const &model, int32_t tree_begin,
                         int32_t tree_end, std::vector<bst_float> *out_preds) const {
    auto const n_threads = this->ctx_->Threads();

    constexpr double kDensityThresh = .5;
    size_t total =
        std::max(p_fmat->Info().num_row_ * p_fmat->Info().num_col_, static_cast<uint64_t>(1));
    double density = static_cast<double>(p_fmat->Info().num_nonzero_) / static_cast<double>(total);
    bool blocked = density > kDensityThresh;

    std::vector<RegTree::FVec> feat_vecs;
    InitThreadTemp(n_threads * (blocked ? kBlockOfRowsSize : 1), &feat_vecs);
    std::vector<Entry> workspace(p_fmat->Info().num_col_ * kUnroll * n_threads);
    auto ft = p_fmat->Info().feature_types.ConstHostVector();
    for (auto const &batch : p_fmat->GetBatches<GHistIndexMatrix>({})) {
      if (blocked) {
        PredictBatchByBlockOfRowsKernel<GHistIndexMatrixView, kBlockOfRowsSize>(
            GHistIndexMatrixView{batch, p_fmat->Info().num_col_, ft, workspace, n_threads},
            out_preds, model, tree_begin, tree_end, &feat_vecs, n_threads);
      } else {
        PredictBatchByBlockOfRowsKernel<GHistIndexMatrixView, 1>(
            GHistIndexMatrixView{batch, p_fmat->Info().num_col_, ft, workspace, n_threads},
            out_preds, model, tree_begin, tree_end, &feat_vecs, n_threads);
      }
    }
  }

  void PredictDMatrix(DMatrix *p_fmat, std::vector<bst_float> *out_preds,
                      gbm::GBTreeModel const &model, int32_t tree_begin, int32_t tree_end) const {
    if (!p_fmat->PageExists<SparsePage>()) {
      this->PredictGHistIndex(p_fmat, model, tree_begin, tree_end, out_preds);
      return;
    }

    auto const n_threads = this->ctx_->Threads();
    constexpr double kDensityThresh = .5;
    size_t total =
        std::max(p_fmat->Info().num_row_ * p_fmat->Info().num_col_, static_cast<uint64_t>(1));
    double density = static_cast<double>(p_fmat->Info().num_nonzero_) / static_cast<double>(total);
    bool blocked = density > kDensityThresh;

    std::vector<RegTree::FVec> feat_vecs;
    InitThreadTemp(n_threads * (blocked ? kBlockOfRowsSize : 1), &feat_vecs);

    for (auto const &batch : p_fmat->GetBatches<SparsePage>()) {
      CHECK_EQ(out_preds->size(),
               p_fmat->Info().num_row_ * model.learner_model_param->num_output_group);
      if (blocked) {
        PredictBatchByBlockOfRowsKernel<SparsePageView, kBlockOfRowsSize>(
            SparsePageView{&batch}, out_preds, model, tree_begin, tree_end, &feat_vecs, n_threads);

      } else {
        PredictBatchByBlockOfRowsKernel<SparsePageView, 1>(
            SparsePageView{&batch}, out_preds, model, tree_begin, tree_end, &feat_vecs, n_threads);
      }
    }
  }

 public:
  explicit CPUPredictor(GenericParameter const *generic_param)
      : Predictor::Predictor{generic_param} {}

  void PredictBatch(DMatrix *dmat, PredictionCacheEntry *predts, const gbm::GBTreeModel &model,
                    uint32_t tree_begin, uint32_t tree_end = 0) const override {
    auto *out_preds = &predts->predictions;
    // This is actually already handled in gbm, but large amount of tests rely on the
    // behaviour.
    if (tree_end == 0) {
      tree_end = model.trees.size();
    }
    this->PredictDMatrix(dmat, &out_preds->HostVector(), model, tree_begin, tree_end);
  }

  template <typename Adapter, size_t kBlockSize>
  void DispatchedInplacePredict(dmlc::any const &x, std::shared_ptr<DMatrix> p_m,
                                const gbm::GBTreeModel &model, float missing,
                                PredictionCacheEntry *out_preds, uint32_t tree_begin,
                                uint32_t tree_end) const {
    auto const n_threads = this->ctx_->Threads();
    auto m = dmlc::get<std::shared_ptr<Adapter>>(x);
    CHECK_EQ(m->NumColumns(), model.learner_model_param->num_feature)
        << "Number of columns in data must equal to trained model.";
    if (p_m) {
      p_m->Info().num_row_ = m->NumRows();
      this->InitOutPredictions(p_m->Info(), &(out_preds->predictions), model);
    } else {
      MetaInfo info;
      info.num_row_ = m->NumRows();
      this->InitOutPredictions(info, &(out_preds->predictions), model);
    }
    std::vector<Entry> workspace(m->NumColumns() * kUnroll * n_threads);
    auto &predictions = out_preds->predictions.HostVector();
    std::vector<RegTree::FVec> thread_temp;
    InitThreadTemp(n_threads * kBlockSize, &thread_temp);
    PredictBatchByBlockOfRowsKernel<AdapterView<Adapter>, kBlockSize>(
        AdapterView<Adapter>(m.get(), missing, common::Span<Entry>{workspace}, n_threads),
        &predictions, model, tree_begin, tree_end, &thread_temp, n_threads);
  }

  bool InplacePredict(std::shared_ptr<DMatrix> p_m, const gbm::GBTreeModel &model, float missing,
                      PredictionCacheEntry *out_preds, uint32_t tree_begin,
                      unsigned tree_end) const override {
    auto proxy = dynamic_cast<data::DMatrixProxy *>(p_m.get());
    CHECK(proxy) << "Inplace predict accepts only DMatrixProxy as input.";
    auto x = proxy->Adapter();
    if (x.type() == typeid(std::shared_ptr<data::DenseAdapter>)) {
      this->DispatchedInplacePredict<data::DenseAdapter, kBlockOfRowsSize>(
          x, p_m, model, missing, out_preds, tree_begin, tree_end);
    } else if (x.type() == typeid(std::shared_ptr<data::CSRAdapter>)) {
      this->DispatchedInplacePredict<data::CSRAdapter, 1>(x, p_m, model, missing, out_preds,
                                                          tree_begin, tree_end);
    } else if (x.type() == typeid(std::shared_ptr<data::ArrayAdapter>)) {
      this->DispatchedInplacePredict<data::ArrayAdapter, kBlockOfRowsSize>(
          x, p_m, model, missing, out_preds, tree_begin, tree_end);
    } else if (x.type() == typeid(std::shared_ptr<data::CSRArrayAdapter>)) {
      this->DispatchedInplacePredict<data::CSRArrayAdapter, 1>(x, p_m, model, missing, out_preds,
                                                               tree_begin, tree_end);
    } else {
      return false;
    }
    return true;
  }

  void PredictInstance(const SparsePage::Inst &inst, std::vector<bst_float> *out_preds,
                       const gbm::GBTreeModel &model, unsigned ntree_limit) const override {
    std::vector<RegTree::FVec> feat_vecs;
    feat_vecs.resize(1, RegTree::FVec());
    feat_vecs[0].Init(model.learner_model_param->num_feature);
    ntree_limit *= model.learner_model_param->num_output_group;
    if (ntree_limit == 0 || ntree_limit > model.trees.size()) {
      ntree_limit = static_cast<unsigned>(model.trees.size());
    }
    out_preds->resize(model.learner_model_param->num_output_group *
                      (model.param.size_leaf_vector + 1));
    auto base_score = model.learner_model_param->BaseScore(ctx_)(0);
    // loop over output groups
    for (uint32_t gid = 0; gid < model.learner_model_param->num_output_group; ++gid) {
      (*out_preds)[gid] =
          PredValue(inst, model.trees, model.tree_info, gid, &feat_vecs[0], 0, ntree_limit) +
          base_score;
    }
  }

  void PredictLeaf(DMatrix *p_fmat, HostDeviceVector<bst_float> *out_preds,
                   const gbm::GBTreeModel &model, unsigned ntree_limit) const override {
    auto const n_threads = this->ctx_->Threads();
    std::vector<RegTree::FVec> feat_vecs;
    const int num_feature = model.learner_model_param->num_feature;
    InitThreadTemp(n_threads, &feat_vecs);
    const MetaInfo &info = p_fmat->Info();
    // number of valid trees
    if (ntree_limit == 0 || ntree_limit > model.trees.size()) {
      ntree_limit = static_cast<unsigned>(model.trees.size());
    }
    std::vector<bst_float> &preds = out_preds->HostVector();
    preds.resize(info.num_row_ * ntree_limit);
    // start collecting the prediction
    for (const auto &batch : p_fmat->GetBatches<SparsePage>()) {
      // parallel over local batch
      auto page = batch.GetView();
      const auto nsize = static_cast<bst_omp_uint>(batch.Size());
      common::ParallelFor(nsize, n_threads, [&](bst_omp_uint i) {
        const int tid = omp_get_thread_num();
        auto ridx = static_cast<size_t>(batch.base_rowid + i);
        RegTree::FVec &feats = feat_vecs[tid];
        if (feats.Size() == 0) {
          feats.Init(num_feature);
        }
        feats.Fill(page[i]);
        for (unsigned j = 0; j < ntree_limit; ++j) {
          auto const &tree = *model.trees[j];
          auto const &cats = tree.GetCategoriesMatrix();
          std::size_t k = ridx * ntree_limit + j;
          bst_node_t tid = GetLeafIndex<true, true>(tree, feats, cats, k);
          preds[k] = static_cast<bst_float>(tid);
        }
        feats.Drop(page[i]);
      });
    }
  }

  void PredictContribution(DMatrix *p_fmat, HostDeviceVector<float> *out_contribs,
                           const gbm::GBTreeModel &model, uint32_t ntree_limit,
                           std::vector<bst_float> const *tree_weights, bool approximate,
                           int condition, unsigned condition_feature) const override {
    auto const n_threads = this->ctx_->Threads();
    const int num_feature = model.learner_model_param->num_feature;
    std::vector<RegTree::FVec> feat_vecs;
    InitThreadTemp(n_threads, &feat_vecs);
    const MetaInfo &info = p_fmat->Info();
    // number of valid trees
    if (ntree_limit == 0 || ntree_limit > model.trees.size()) {
      ntree_limit = static_cast<unsigned>(model.trees.size());
    }
    const int ngroup = model.learner_model_param->num_output_group;
    CHECK_NE(ngroup, 0);
    size_t const ncolumns = num_feature + 1;
    CHECK_NE(ncolumns, 0);
    // allocate space for (number of features + bias) times the number of rows
    std::vector<bst_float> &contribs = out_contribs->HostVector();
    contribs.resize(info.num_row_ * ncolumns * model.learner_model_param->num_output_group);
    // make sure contributions is zeroed, we could be reusing a previously
    // allocated one
    std::fill(contribs.begin(), contribs.end(), 0);
    // initialize tree node mean values
    std::vector<std::vector<float>> mean_values(ntree_limit);
    common::ParallelFor(ntree_limit, n_threads, [&](bst_omp_uint i) {
      FillNodeMeanValues(model.trees[i].get(), &(mean_values[i]));
    });
    auto base_margin = info.base_margin_.View(Context::kCpuId);
    auto base_score = model.learner_model_param->BaseScore(Context::kCpuId)(0);
    // start collecting the contributions
    for (const auto &batch : p_fmat->GetBatches<SparsePage>()) {
      auto page = batch.GetView();
      // parallel over local batch
      const auto nsize = static_cast<bst_omp_uint>(batch.Size());
      common::ParallelFor(nsize, n_threads, [&](bst_omp_uint i) {
        auto row_idx = static_cast<size_t>(batch.base_rowid + i);
        RegTree::FVec &feats = feat_vecs[omp_get_thread_num()];
        if (feats.Size() == 0) {
          feats.Init(num_feature);
        }
        std::vector<bst_float> this_tree_contribs(ncolumns);
        // loop over all classes
        for (int gid = 0; gid < ngroup; ++gid) {
          bst_float *p_contribs = &contribs[(row_idx * ngroup + gid) * ncolumns];
          feats.Fill(page[i]);
          // calculate contributions
          for (unsigned j = 0; j < ntree_limit; ++j) {
            auto *tree_mean_values = &mean_values.at(j);
            std::fill(this_tree_contribs.begin(), this_tree_contribs.end(), 0);
            if (model.tree_info[j] != gid) {
              continue;
            }
            if (!approximate) {
              model.trees[j]->CalculateContributions(
                  feats, tree_mean_values, &this_tree_contribs[0], condition, condition_feature);
            } else {
              model.trees[j]->CalculateContributionsApprox(feats, tree_mean_values,
                                                           &this_tree_contribs[0]);
            }
            for (size_t ci = 0; ci < ncolumns; ++ci) {
              p_contribs[ci] +=
                  this_tree_contribs[ci] * (tree_weights == nullptr ? 1 : (*tree_weights)[j]);
            }
          }
          feats.Drop(page[i]);
          // add base margin to BIAS
          if (base_margin.Size() != 0) {
            CHECK_EQ(base_margin.Shape(1), ngroup);
            p_contribs[ncolumns - 1] += base_margin(row_idx, gid);
          } else {
            p_contribs[ncolumns - 1] += base_score;
          }
        }
      });
    }
  }

  void PredictInteractionContributions(DMatrix *p_fmat, HostDeviceVector<bst_float> *out_contribs,
                                       const gbm::GBTreeModel &model, unsigned ntree_limit,
                                       std::vector<bst_float> const *tree_weights,
                                       bool approximate) const override {
    const MetaInfo &info = p_fmat->Info();
    const int ngroup = model.learner_model_param->num_output_group;
    size_t const ncolumns = model.learner_model_param->num_feature;
    const unsigned row_chunk = ngroup * (ncolumns + 1) * (ncolumns + 1);
    const unsigned mrow_chunk = (ncolumns + 1) * (ncolumns + 1);
    const unsigned crow_chunk = ngroup * (ncolumns + 1);

    // allocate space for (number of features^2) times the number of rows and tmp off/on contribs
    std::vector<bst_float> &contribs = out_contribs->HostVector();
    contribs.resize(info.num_row_ * ngroup * (ncolumns + 1) * (ncolumns + 1));
    HostDeviceVector<bst_float> contribs_off_hdv(info.num_row_ * ngroup * (ncolumns + 1));
    auto &contribs_off = contribs_off_hdv.HostVector();
    HostDeviceVector<bst_float> contribs_on_hdv(info.num_row_ * ngroup * (ncolumns + 1));
    auto &contribs_on = contribs_on_hdv.HostVector();
    HostDeviceVector<bst_float> contribs_diag_hdv(info.num_row_ * ngroup * (ncolumns + 1));
    auto &contribs_diag = contribs_diag_hdv.HostVector();

    // Compute the difference in effects when conditioning on each of the features on and off
    // see: Axiomatic characterizations of probabilistic and
    //      cardinal-probabilistic interaction indices
    PredictContribution(p_fmat, &contribs_diag_hdv, model, ntree_limit, tree_weights, approximate,
                        0, 0);
    for (size_t i = 0; i < ncolumns + 1; ++i) {
      PredictContribution(p_fmat, &contribs_off_hdv, model, ntree_limit, tree_weights, approximate,
                          -1, i);
      PredictContribution(p_fmat, &contribs_on_hdv, model, ntree_limit, tree_weights, approximate,
                          1, i);

      for (size_t j = 0; j < info.num_row_; ++j) {
        for (int l = 0; l < ngroup; ++l) {
          const unsigned o_offset = j * row_chunk + l * mrow_chunk + i * (ncolumns + 1);
          const unsigned c_offset = j * crow_chunk + l * (ncolumns + 1);
          contribs[o_offset + i] = 0;
          for (size_t k = 0; k < ncolumns + 1; ++k) {
            // fill in the diagonal with additive effects, and off-diagonal with the interactions
            if (k == i) {
              contribs[o_offset + i] += contribs_diag[c_offset + k];
            } else {
              contribs[o_offset + k] =
                  (contribs_on[c_offset + k] - contribs_off[c_offset + k]) / 2.0;
              contribs[o_offset + i] -= contribs[o_offset + k];
            }
          }
        }
      }
    }
  }

 private:
  static size_t constexpr kBlockOfRowsSize = 64;
};

XGBOOST_REGISTER_PREDICTOR(CPUPredictor, "cpu_predictor")
    .describe("Make predictions using CPU.")
    .set_body([](GenericParameter const *generic_param) {
      return new CPUPredictor(generic_param);
    });
}  // namespace predictor
}  // namespace xgboost
