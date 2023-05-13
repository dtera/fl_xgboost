//
// Created by HqZhao on 2023/4/18.
//

#pragma once

#include <tbb/concurrent_unordered_map.h>

#include <boost/algorithm/string.hpp>

#include "comm/grpc/common.h"
#include "comm/pulsar/PulsarClient.hpp"
#include "tree/hist/expand_entry.h"

class XgbPulsarService {
 private:
  std::unique_ptr<PulsarClient> client;
  std::int32_t n_threads;
  opt_private_key_t* pri;
  opt_public_key_t* pub;
  std::int32_t pulsar_topic_ttl;
  bool batched = false;

  inline std::string PubKeyTopic() { return "pub_key"; }

  inline std::string GradPairTopic() { return "grad_pairs_iter-" + std::to_string(cur_version); }

  inline std::string SplitsTopic(const std::uint32_t nid) {
    return "splits_iter-" + std::to_string(cur_version) + "_nid-" + std::to_string(nid);
  }

  inline std::string SplitsByLayerTopic(std::uint32_t nids) {
    return "splits_by_layer_iter-" + std::to_string(cur_version) + "_nid-" + std::to_string(nids);
  }

  inline std::string BestSplitTopic(const std::uint32_t nid) {
    return "best_split_iter-" + std::to_string(cur_version) + "_nid-" + std::to_string(nid);
  }

  inline std::string BestSplitByLayerTopic(std::uint32_t nids) {
    return "best_split_by_layer_iter-" + std::to_string(cur_version) + "_nid-" +
           std::to_string(nids);
  }

  inline std::string SplitValidTopic(const std::uint32_t nid, const bool is_push = false) {
    return "split_valid_iter-" + std::to_string(cur_version) + "_nid-" + std::to_string(nid) +
           (is_push ? "_pushed" : "");
  }

  inline std::string SplitsValidTopic(const std::uint32_t nids) {
    return "splits_valid_iter-" + std::to_string(cur_version) + "_nids-" + std::to_string(nids);
  }

  inline std::string LeftRightNodeSizesTopic(const std::string& nids) {
    return "left_right_nodes_sizes_iter-" + std::to_string(cur_version) + "_nids-" + nids;
  }

  inline std::string BlockInfosTopic(const std::string& nids) {
    return "block_infos_iter-" + std::to_string(cur_version) + "_nids-" + nids;
  }

  inline std::string NextNodesTopic(const int idx) {
    return "next_nodes_iter-" + std::to_string(cur_version) + "_idx-" + std::to_string(idx);
  }

  inline std::string MetricsTopic(const int iter) { return "metrics_iter-" + std::to_string(iter); }

 public:
  std::uint32_t cur_version{0};
  std::uint32_t max_iter{std::numeric_limits<uint32_t>().max()};
  std::size_t eval_data_idx = 0;

  XgbPulsarService(bool start = false, const std::string& pulsar_url = "pulsar://localhost:6650",
                   const std::string& topic_prefix = "federated_xgb_",
                   const std::string& pulsar_token = "notoken",
                   const std::string& pulsar_tenant = "fl-tenant",
                   const std::string& pulsar_namespace = "fl-algorithm",
                   const std::int32_t pulsar_topic_ttl = 60,
                   const std::uint32_t pulsar_batch_max_size = 1000000, const bool batched = false,
                   const std::int32_t n_threads = omp_get_num_procs());

  void Start(const std::string& pulsar_url = "pulsar://localhost:6650",
             const std::string& topic_prefix = "federated_xgb_",
             const std::string& pulsar_token = "notoken",
             const std::string& pulsar_tenant = "fl-tenant",
             const std::string& pulsar_namespace = "fl-algorithm",
             const std::int32_t pulsar_topic_ttl = 60,
             const std::uint32_t pulsar_batch_max_size = 1000000, const bool batched = false,
             const std::int32_t n_threads = omp_get_num_procs());

  void SetPriKey(opt_private_key_t* pri_);

  void SendPubKey(opt_public_key_t* pub_);

  void GetPubKey(opt_public_key_t** pub_);

  void SendEncryptedGradPairs(const std::vector<xgboost::EncryptedGradientPair>& grad_pairs);

  void GetEncryptedGradPairs(std::vector<xgboost::EncryptedGradientPair>& grad_pairs);

  void HandleSplitUpdate(
      const xgbcomm::SplitsRequest& sr,
      const std::function<void(std::uint32_t, xgboost::tree::GradStats<double>&,
                               xgboost::tree::GradStats<double>&, const xgbcomm::SplitsRequest&)>&
          update_grad_stats) const;

  void SendEncryptedSplits(const xgbcomm::SplitsRequest& sr);

  void GetEncryptedSplits(
      const std::uint32_t nid, xgbcomm::SplitsRequest& sr,
      std::function<void(std::uint32_t, xgboost::tree::GradStats<double>&,
                         xgboost::tree::GradStats<double>&, const xgbcomm::SplitsRequest&)>
          update_grad_stats);

  void SendEncryptedSplitsByLayer(std::uint32_t nids, const xgbcomm::SplitsRequests& srs);

  void GetEncryptedSplitsByLayer(std::uint32_t nids, xgbcomm::SplitsRequests& srs);

  template <typename ExpandEntry>
  void HandleBestSplit(int bestIdx, ExpandEntry& entry, const xgbcomm::SplitsRequest& sr,
                       xgbcomm::SplitsResponse& response);

  template <typename ExpandEntry>
  void SendBestSplit(int bestIdx, ExpandEntry& entry, const xgbcomm::SplitsRequest& sr);

  void GetBestSplit(const std::uint32_t nid,
                    std::function<void(xgbcomm::SplitsResponse&)> process_best_split);

  void SendBestSplitsByLayer(std::uint32_t nids, const xgbcomm::SplitsResponses& splitsResponses);

  void GetBestSplitsByLayer(std::uint32_t nids, xgbcomm::SplitsResponses& splitsResponses);

  void SendSplitValid(const std::uint32_t nid, const bool split_valid, const bool is_push = false);

  bool GetSplitValid(const std::uint32_t nid, const bool is_push = false);

  void SendSplitsValid(const std::map<std::uint32_t, std::pair<bool, bool>>& splits_valid);

  void GetSplitsValid(std::map<std::uint32_t, std::pair<bool, bool>>& splits_valid);

  void SendLeftRightNodeSizes(
      const std::map<std::size_t, const std::pair<std::size_t, std::size_t>>&
          left_right_nodes_sizes);

  void GetLeftRightNodeSizes(
      std::string nids,
      std::map<std::size_t, const std::pair<std::size_t, std::size_t>>& left_right_nodes_sizes);

  void SendBlockInfos(
      const tbb::concurrent_unordered_map<std::size_t, xgbcomm::BlockInfo>& block_infos);

  void GetBlockInfos(std::string nids,
                     tbb::concurrent_unordered_map<std::size_t, xgbcomm::BlockInfo>& block_infos);

  void SendNextNodes(int idx, const xgbcomm::NextNodesV2& next_nids);

  void GetNextNodes(int idx,
                    std::function<void(const google::protobuf::Map<std::uint32_t, std::uint32_t>&)>
                        process_part_idxs);

  void SendMetrics(int iter, const std::vector<double>& metrics);

  void GetMetrics(int iter, std::vector<std::string>& metrics);
};