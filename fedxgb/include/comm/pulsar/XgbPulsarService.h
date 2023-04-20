//
// Created by HqZhao on 2023/4/18.
//

#pragma once

#include "comm/grpc/common.h"
#include "comm/pulsar/PulsarClient.hpp"

class XgbPulsarService {
 private:
  std::unique_ptr<PulsarClient> client;
  std::int32_t n_threads;

  inline std::string GradPairTopic() { return "grad_pairs_" + std::to_string(cur_version); }

 public:
  uint32_t cur_version{0};
  uint32_t max_iter{std::numeric_limits<uint32_t>().max()};

  XgbPulsarService(bool start = false, const std::string& pulsar_url = "pulsar://localhost:6650",
                   const std::string& topic_prefix = "federated_xgb_",
                   const std::string& pulsar_token = "notoken",
                   const std::string& pulsar_tenant = "fl-tenant",
                   const std::string& pulsar_namespace = "fl-algorithm",
                   const std::int32_t n_threads = omp_get_num_procs());

  void Start(const std::string& pulsar_url = "pulsar://localhost:6650",
             const std::string& topic_prefix = "federated_xgb_",
             const std::string& pulsar_token = "notoken",
             const std::string& pulsar_tenant = "fl-tenant",
             const std::string& pulsar_namespace = "fl-algorithm",
             const std::int32_t n_threads = omp_get_num_procs());

  void SendEncryptedGradPairs(const std::vector<xgboost::EncryptedGradientPair>& grad_pairs);

  void GetEncryptedGradPairs(std::vector<xgboost::EncryptedGradientPair>& grad_pairs);
};