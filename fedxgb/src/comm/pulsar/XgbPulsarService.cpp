//
// Created by HqZhao on 2023/4/18.
//
#include "comm/pulsar/XgbPulsarService.h"

#include "comm/pulsar/PulsarClient.hpp"

XgbPulsarService::XgbPulsarService(bool start, const std::string& pulsar_url,
                                   const std::string& topic_prefix, const std::string& pulsar_token,
                                   const std::string& pulsar_tenant,
                                   const std::string& pulsar_namespace) {
  if (start) {
    Start(pulsar_url, topic_prefix, pulsar_token, pulsar_tenant, pulsar_namespace);
  }
}

void XgbPulsarService::Start(const std::string& pulsar_url, const std::string& topic_prefix,
                             const std::string& pulsar_token, const std::string& pulsar_tenant,
                             const std::string& pulsar_namespace) {
  char yyyymmddhh[13];
  time_t now = time(NULL);
  strftime(yyyymmddhh, 13, "%Y%m%d%H%M", localtime(&now));
  client = std::make_unique<PulsarClient>(pulsar_url,
                                          topic_prefix + std::string(yyyymmddhh)  //.substr(0, 11)
                                              + "_",
                                          pulsar_token, pulsar_tenant, pulsar_namespace);
}

void XgbPulsarService::SendEncryptedGradPairs(
    const std::vector<xgboost::EncryptedGradientPair>& grad_pairs) {
  client->BatchSend<xgboost::EncryptedGradientPair, xgbcomm::GradPair>(
      GradPairTopic(), grad_pairs,
      [&](xgbcomm::GradPair* m, const xgboost::EncryptedGradientPair& data) {
        mpz_t2_mpz_type(m, data);
      });
}

void XgbPulsarService::GetEncryptedGradPairs(
    std::vector<xgboost::EncryptedGradientPair>& grad_pairs) {
  client->BatchReceive<xgboost::EncryptedGradientPair, xgbcomm::GradPair>(
      GradPairTopic(), grad_pairs,
      [&](xgboost::EncryptedGradientPair& data, const xgbcomm::GradPair& m) {
        mpz_type2_mpz_t(data, m);
      },
      true);
}