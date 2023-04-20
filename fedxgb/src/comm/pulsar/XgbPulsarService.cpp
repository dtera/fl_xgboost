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
      pulsar_url, topic_prefix + std::to_string(std::atoll(yyyymmddhh) / 5) + "_", pulsar_token,
      pulsar_tenant, pulsar_namespace, n_threads);
}

void XgbPulsarService::SetPriKey(opt_private_key_t* pri_) { pri = pri_; }

void XgbPulsarService::SendPubKey(opt_public_key_t* pub_) {
  pub = pub_;
  xgbcomm::PubKeyResponse response;
  pub_key2pb(&response, pub_);
  client->Send(PubKeyTopic(), response);
}

void XgbPulsarService::GetPubKey(opt_public_key_t** pub_) {
  xgbcomm::PubKeyResponse response;
  client->Receive(PubKeyTopic(), response);
  pb2pub_key(pub_, response);
  pub = *pub_;
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

void XgbPulsarService::SendEncryptedSplits(const xgbcomm::SplitsRequest& sr) {
  client->Send(SplitsTopic(sr.nidx()), sr);
  LOG(CONSOLE) << "nid: " << sr.nidx()
               << ", send encrypted splits size: " << sr.encrypted_splits_size() << std::endl;
}

void XgbPulsarService::GetEncryptedSplits(
    const std::uint32_t nid, xgbcomm::SplitsRequest& sr,
    std::function<void(std::uint32_t, xgboost::tree::GradStats<double>&,
                       xgboost::tree::GradStats<double>&, const xgbcomm::SplitsRequest&)>
        update_grad_stats) {
  client->Receive(SplitsTopic(nid), sr);
  LOG(CONSOLE) << "nid: " << nid
               << ", receive encrypted splits size: " << sr.encrypted_splits_size() << std::endl;
  auto encrypted_splits = sr.encrypted_splits();
  ParallelFor(encrypted_splits.size(), n_threads, [&](uint32_t i) {
    xgboost::tree::GradStats<double> left_sum;
    xgboost::tree::GradStats<double> right_sum;
    xgboost::tree::GradStats<EncryptedType<double>> encrypted_left_sum;
    xgboost::tree::GradStats<EncryptedType<double>> encrypted_right_sum;
    mpz_type2_mpz_t(encrypted_left_sum, encrypted_splits[i].left_sum());
    mpz_type2_mpz_t(encrypted_right_sum, encrypted_splits[i].right_sum());
    opt_paillier_decrypt(left_sum, encrypted_left_sum, pub, pri);
    opt_paillier_decrypt(right_sum, encrypted_right_sum, pub, pri);
    // update grad statistics
    update_grad_stats(i, left_sum, right_sum, sr);
  });
}

template <typename ExpandEntry>
void XgbPulsarService::SendBestSplit(int bestIdx, ExpandEntry& entry,
                                     const xgbcomm::SplitsRequest& sr) {
  xgbcomm::SplitsResponse response;
  auto es = response.mutable_encrypted_split();
  auto left_sum = es->mutable_left_sum();
  auto right_sum = es->mutable_right_sum();
  if (bestIdx != -1) {
    // notify the data holder part: it's split is the best
    auto best_split = sr.encrypted_splits(bestIdx);
    LOG(CONSOLE) << "best split for expand entry " << entry.nid << " is " << bestIdx << std::endl;
    es->set_mask_id(best_split.mask_id());
    es->set_d_step(best_split.d_step());
    es->set_default_left(best_split.default_left());
    left_sum->CopyFrom(best_split.left_sum());
    right_sum->CopyFrom(best_split.right_sum());
    response.set_part_id(sr.part_id());
  } else {
    // notify the data holder part: the label holder is the best
    es->set_default_left(entry.split.DefaultLeft());
    response.set_part_id(entry.split.part_id);
    xgboost::tree::GradStats<EncryptedType<double>> encrypted_left_sum;
    xgboost::tree::GradStats<EncryptedType<double>> encrypted_right_sum;
    opt_paillier_encrypt(encrypted_left_sum, entry.split.left_sum, pub);
    opt_paillier_encrypt(encrypted_right_sum, entry.split.right_sum, pub);
    mpz_t2_mpz_type(left_sum, encrypted_left_sum);
    mpz_t2_mpz_type(right_sum, encrypted_right_sum);
  }

  client->Send(BestSplitTopic(entry.nid), response);
}

void XgbPulsarService::GetBestSplit(
    const std::uint32_t nid, std::function<void(xgbcomm::SplitsResponse&)> process_best_split) {
  xgbcomm::SplitsResponse response;
  client->Receive(BestSplitTopic(nid), response);
  process_best_split(response);
}

void XgbPulsarService::SendSplitValid(const std::uint32_t nid, const bool split_valid,
                                      const bool is_push) {
  client->Send(SplitValidTopic(nid, is_push), std::to_string(split_valid));
}

bool XgbPulsarService::GetSplitValid(const std::uint32_t nid, const bool is_push) {
  std::string split_valid;
  client->Receive(SplitValidTopic(nid, is_push), split_valid);
  return std::atoi(split_valid.c_str());
}

void XgbPulsarService::SendSplitsValid(
    const std::map<std::uint32_t, std::pair<bool, bool>>& splits_valid) {
  std::uint32_t nids;
  std::string vs;
  if (!splits_valid.empty()) {
    nids = splits_valid.begin()->first;
    vs = std::to_string(splits_valid.begin()->second.first) + ":" +
         std::to_string(splits_valid.begin()->second.second);
    std::for_each(++splits_valid.begin(), splits_valid.end(), [&](auto t) {
      nids += t.first;
      vs += "_" + std::to_string(t.second.first) + ":" + std::to_string(t.second.second);
    });

    client->Send(SplitsValidTopic(nids), vs);
    LOG(CONSOLE) << "nids: " << nids << ", send splits valid size: " << splits_valid.size()
                 << std::endl;
  };
}

void XgbPulsarService::GetSplitsValid(
    std::map<std::uint32_t, std::pair<bool, bool>>& splits_valid) {
  std::uint32_t nids = 0;
  std::string vs;
  if (!splits_valid.empty()) {
    std::for_each(splits_valid.begin(), splits_valid.end(), [&](auto t) { nids += t.first; });

    client->Receive(SplitsValidTopic(nids), vs);
    std::vector<std::string> v1;
    boost::split(v1, vs, boost::is_any_of("_"));
    LOG(CONSOLE) << "nids: " << nids << ", receive splits valid size: " << v1.size() << std::endl;
    int i = 0;
    for (auto& sv : splits_valid) {
      std::vector<std::string> v2;
      boost::split(v2, v1[i], boost::is_any_of(":"));
      sv.second.first = std::atoi(v2[0].c_str());
      sv.second.second = std::atoi(v2[1].c_str());
      i++;
    }
  };
}

template void XgbPulsarService::SendBestSplit(int bestIdx, xgboost::tree::CPUExpandEntry& entry,
                                              const xgbcomm::SplitsRequest& sr);