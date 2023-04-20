//
// Created by HqZhao on 2023/4/18.
//
#include "comm/pulsar/XgbPulsarService.h"

#include "comm/pulsar/PulsarClient.hpp"

XgbPulsarService::XgbPulsarService(bool start, const std::string& pulsar_url,
                                   const std::string& topic_prefix, const std::string& pulsar_token,
                                   const std::string& pulsar_tenant,
                                   const std::string& pulsar_namespace, std::int32_t n_threads)
    : n_threads(n_threads) {
  if (start) {
    Start(pulsar_url, topic_prefix, pulsar_token, pulsar_tenant, pulsar_namespace, n_threads);
  }
}

void XgbPulsarService::Start(const std::string& pulsar_url, const std::string& topic_prefix,
                             const std::string& pulsar_token, const std::string& pulsar_tenant,
                             const std::string& pulsar_namespace, std::int32_t n_threads) {
  char yyyymmddhh[13];
  time_t now = time(NULL);
  strftime(yyyymmddhh, 13, "%Y%m%d%H%M", localtime(&now));
  client = std::make_unique<PulsarClient>(
      pulsar_url, topic_prefix + std::to_string(std::atoll(yyyymmddhh) / 2) + "_", pulsar_token,
      pulsar_tenant, pulsar_namespace, n_threads);
}

void XgbPulsarService::SendEncryptedGradPairs(
    const std::vector<xgboost::EncryptedGradientPair>& grad_pairs) {
  /*client->BatchSend<xgboost::EncryptedGradientPair, xgbcomm::GradPair>(
      GradPairTopic(), grad_pairs,
      [&](xgbcomm::GradPair* m, const xgboost::EncryptedGradientPair& data) {
        mpz_t2_mpz_type(m, data);
      });*/
  client->Send<std::vector<xgboost::EncryptedGradientPair>, xgbcomm::GradPairsResponse>(
      GradPairTopic(), grad_pairs,
      [&](xgbcomm::GradPairsResponse* m, const std::vector<xgboost::EncryptedGradientPair>& data) {
        for (auto grad_pair : data) {
          auto gp = m->mutable_encrypted_grad_pairs()->Add();
          mpz_t2_mpz_type(gp, grad_pair);
        }
      });
}

void XgbPulsarService::GetEncryptedGradPairs(
    std::vector<xgboost::EncryptedGradientPair>& grad_pairs) {
  /*client->BatchReceive<xgboost::EncryptedGradientPair, xgbcomm::GradPair>(
      GradPairTopic(), grad_pairs,
      [&](xgboost::EncryptedGradientPair& data, const xgbcomm::GradPair& m) {
        mpz_type2_mpz_t(data, m);
      },
      true);*/
  client->Receive<std::vector<xgboost::EncryptedGradientPair>, xgbcomm::GradPairsResponse>(
      GradPairTopic(), grad_pairs,
      [&](std::vector<xgboost::EncryptedGradientPair>& data, const xgbcomm::GradPairsResponse& m) {
        auto encrypted_grad_pairs = m.encrypted_grad_pairs();
        ParallelFor(m.encrypted_grad_pairs_size(), n_threads, [&](const size_t i) {
          xgboost::EncryptedGradientPair gp;
          mpz_type2_mpz_t(gp, encrypted_grad_pairs[i]);
          data[i] = std::move(gp);
        });
      });
}