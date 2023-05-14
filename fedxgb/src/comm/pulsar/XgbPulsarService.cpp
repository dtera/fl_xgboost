//
// Created by HqZhao on 2023/4/18.
//
#include "comm/pulsar/XgbPulsarService.h"

XgbPulsarService::XgbPulsarService(bool start, const std::string& pulsar_url,
                                   const std::string& topic_prefix, const std::string& pulsar_token,
                                   const std::string& pulsar_tenant,
                                   const std::string& pulsar_namespace,
                                   const std::int32_t pulsar_topic_ttl,
                                   const std::uint32_t pulsar_batch_size,
                                   const std::uint32_t pulsar_batch_max_size, const bool batched,
                                   const std::int32_t n_threads)
    : pulsar_topic_ttl(pulsar_topic_ttl), n_threads(n_threads), batched(batched) {
  if (start) {
    Start(pulsar_url, topic_prefix, pulsar_token, pulsar_tenant, pulsar_namespace, pulsar_topic_ttl,
          pulsar_batch_size, pulsar_batch_max_size, batched, n_threads);
  }
}

void XgbPulsarService::Start(const std::string& pulsar_url, const std::string& topic_prefix,
                             const std::string& pulsar_token, const std::string& pulsar_tenant,
                             const std::string& pulsar_namespace,
                             const std::int32_t pulsar_topic_ttl_,
                             const std::uint32_t pulsar_batch_size,
                             const std::uint32_t pulsar_batch_max_size, const bool batched,
                             const std::int32_t n_threads) {
  this->n_threads = n_threads;
  this->pulsar_topic_ttl = pulsar_topic_ttl_;
  this->batched = batched;
  char yyyymmddhhMM[13];
  time_t now = time(NULL);
  strftime(yyyymmddhhMM, 13, "%Y%m%d%H%M", localtime(&now));
  client = std::make_unique<PulsarClient>(
      pulsar_url, topic_prefix + std::to_string(std::atoll(yyyymmddhhMM) / pulsar_topic_ttl) + "_",
      pulsar_token, pulsar_tenant, pulsar_namespace, pulsar_topic_ttl, pulsar_batch_size,
      pulsar_batch_max_size, n_threads);
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
  if (batched) {
    client
        ->BatchSend<xgboost::EncryptedGradientPair, xgbcomm::GradPair, xgbcomm::GradPairsResponse>(
            GradPairTopic(), grad_pairs,
            [&](xgbcomm::GradPair* m, const xgboost::EncryptedGradientPair& data) {
              mpz_t2_mpz_type(m, data);
            },
            [&](xgbcomm::GradPairsResponse& r) { return r.mutable_encrypted_grad_pairs()->Add(); });
  } else {
    client->Send<std::vector<xgboost::EncryptedGradientPair>, xgbcomm::GradPairsResponse>(
        GradPairTopic(), grad_pairs,
        [&](xgbcomm::GradPairsResponse* m,
            const std::vector<xgboost::EncryptedGradientPair>& data) {
          for (auto grad_pair : data) {
            auto gp = m->mutable_encrypted_grad_pairs()->Add();
            mpz_t2_mpz_type(gp, grad_pair);
          }
        });
  }
}

void XgbPulsarService::GetEncryptedGradPairs(
    std::vector<xgboost::EncryptedGradientPair>& grad_pairs) {
  if (batched) {
    client->BatchReceive<xgboost::EncryptedGradientPair, xgbcomm::GradPair,
                         xgbcomm::GradPairsResponse>(
        GradPairTopic(), grad_pairs,
        [&](xgboost::EncryptedGradientPair& data, const xgbcomm::GradPair& m) {
          mpz_type2_mpz_t(data, m);
        },
        [&](const xgbcomm::GradPairsResponse& r) { return r.encrypted_grad_pairs(); });
  } else {
    client->Receive<std::vector<xgboost::EncryptedGradientPair>, xgbcomm::GradPairsResponse>(
        GradPairTopic(), grad_pairs,
        [&](std::vector<xgboost::EncryptedGradientPair>& data,
            const xgbcomm::GradPairsResponse& m) {
          auto encrypted_grad_pairs = m.encrypted_grad_pairs();
          ParallelFor(m.encrypted_grad_pairs_size(), n_threads, [&](const size_t i) {
            xgboost::EncryptedGradientPair gp;
            mpz_type2_mpz_t(gp, encrypted_grad_pairs[i]);
            data[i] = std::move(gp);
          });
        });
  }
}

void XgbPulsarService::HandleSplitUpdate(
    const xgbcomm::SplitsRequest& sr,
    const std::function<void(std::uint32_t, xgboost::tree::GradStats<double>&,
                             xgboost::tree::GradStats<double>&, const xgbcomm::SplitsRequest&)>&
        update_grad_stats) const {
  auto encrypted_splits = sr.encrypted_splits();
  ParallelFor(encrypted_splits.size(), this->n_threads, [&](uint32_t i) {
    xgboost::tree::GradStats<double> left_sum;
    xgboost::tree::GradStats<double> right_sum;
    xgboost::tree::GradStats<EncryptedType<double>> encrypted_left_sum;
    xgboost::tree::GradStats<EncryptedType<double>> encrypted_right_sum;
    mpz_type2_mpz_t(encrypted_left_sum, encrypted_splits[i].left_sum());
    mpz_type2_mpz_t(encrypted_right_sum, encrypted_splits[i].right_sum());
    opt_paillier_decrypt(left_sum, encrypted_left_sum, this->pub, this->pri);
    opt_paillier_decrypt(right_sum, encrypted_right_sum, this->pub, this->pri);
    // update grad statistics
    update_grad_stats(i, left_sum, right_sum, sr);
  });
}

void XgbPulsarService::SendEncryptedSplits(const xgbcomm::SplitsRequest& sr) {
  client->Send(SplitsTopic(sr.nidx()), sr);
  DEBUG << "[S]nid: " << sr.nidx() << ", encrypted splits size: " << sr.encrypted_splits_size()
        << std::endl;
}

void XgbPulsarService::GetEncryptedSplits(
    const std::uint32_t nid, xgbcomm::SplitsRequest& sr,
    std::function<void(std::uint32_t, xgboost::tree::GradStats<double>&,
                       xgboost::tree::GradStats<double>&, const xgbcomm::SplitsRequest&)>
        update_grad_stats) {
  client->Receive(SplitsTopic(nid), sr);
  DEBUG << "[R]nid: " << nid << ", encrypted splits size: " << sr.encrypted_splits_size()
        << std::endl;
  HandleSplitUpdate(sr, update_grad_stats);
}

void XgbPulsarService::SendEncryptedSplitsByLayer(std::uint32_t nids,
                                                  const xgbcomm::SplitsRequests& srs) {
  client->Send(SplitsByLayerTopic(nids), srs);
  std::size_t size = 0;
  std::for_each(srs.splits_requests().begin(), srs.splits_requests().end(),
                [&](auto& sr) { size += sr.encrypted_splits_size(); });
  DEBUG << "[S]iter: " << cur_version << ", encrypted splits size: " << size
        << ", part_id: " << srs.splits_requests(0).part_id() << std::endl;
}

void XgbPulsarService::GetEncryptedSplitsByLayer(std::uint32_t nids, xgbcomm::SplitsRequests& srs) {
  client->Receive(SplitsByLayerTopic(nids), srs);
  std::size_t size = 0;
  std::for_each(srs.splits_requests().begin(), srs.splits_requests().end(),
                [&](auto& sr) { size += sr.encrypted_splits_size(); });
  DEBUG << "[R]iter: " << cur_version << ", encrypted splits size: " << size
        << ", part_id: " << srs.splits_requests(0).part_id() << std::endl;
}

template <typename ExpandEntry>
void XgbPulsarService::HandleBestSplit(int bestIdx, ExpandEntry& entry,
                                       const xgbcomm::SplitsRequest& sr,
                                       xgbcomm::SplitsResponse& response) {
  auto es = response.mutable_encrypted_split();
  auto left_sum = es->mutable_left_sum();
  auto right_sum = es->mutable_right_sum();
  if (bestIdx != -1) {
    // notify the data holder part: it's split is the best
    auto best_split = sr.encrypted_splits(bestIdx);
    // DEBUG << "best split for entry " << entry.nid << " is " << bestIdx << std::endl;
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
}

template <typename ExpandEntry>
void XgbPulsarService::SendBestSplit(int bestIdx, ExpandEntry& entry,
                                     const xgbcomm::SplitsRequest& sr) {
  xgbcomm::SplitsResponse response;
  HandleBestSplit(bestIdx, entry, sr, response);

  client->Send(BestSplitTopic(entry.nid), response);
}

void XgbPulsarService::GetBestSplit(
    const std::uint32_t nid, std::function<void(xgbcomm::SplitsResponse&)> process_best_split) {
  xgbcomm::SplitsResponse response;
  client->Receive(BestSplitTopic(nid), response);
  process_best_split(response);
}

void XgbPulsarService::SendBestSplitsByLayer(std::uint32_t nids,
                                             const xgbcomm::SplitsResponses& splitsResponses) {
  client->Send(BestSplitByLayerTopic(nids), splitsResponses);
  DEBUG << "[S]iter: " << cur_version
        << ", best splits size: " << splitsResponses.splits_responses_size()
        << ", part_id: " << splitsResponses.splits_responses(0).part_id() << std::endl;
}

void XgbPulsarService::GetBestSplitsByLayer(std::uint32_t nids,
                                            xgbcomm::SplitsResponses& splitsResponses) {
  client->Receive(BestSplitByLayerTopic(nids), splitsResponses);
  DEBUG << "[R]iter: " << cur_version
        << ", best splits size: " << splitsResponses.splits_responses_size()
        << ", part_id: " << splitsResponses.splits_responses(0).part_id() << std::endl;
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
  if (!splits_valid.empty()) {
    std::uint32_t nids;
    std::string content;
    nids = splits_valid.begin()->first;
    content = std::to_string(splits_valid.begin()->second.first) + ":" +
              std::to_string(splits_valid.begin()->second.second);
    std::for_each(++splits_valid.begin(), splits_valid.end(), [&](auto t) {
      nids += t.first;
      content += "_" + std::to_string(t.second.first) + ":" + std::to_string(t.second.second);
    });

    client->Send(SplitsValidTopic(nids), content);
    DEBUG << "[S]nids: " << nids << ", splits_valid size: " << splits_valid.size() << std::endl;
  }
}

void XgbPulsarService::GetSplitsValid(
    std::map<std::uint32_t, std::pair<bool, bool>>& splits_valid) {
  if (!splits_valid.empty()) {
    std::uint32_t nids = 0;
    std::string content;
    std::for_each(splits_valid.begin(), splits_valid.end(), [&](auto t) { nids += t.first; });

    client->Receive(SplitsValidTopic(nids), content);
    std::vector<std::string> v1;
    boost::split(v1, content, boost::is_any_of("_"));
    DEBUG << "[R]nids: " << nids << ", splits_valid size: " << v1.size() << std::endl;
    int i = 0;
    for (auto& sv : splits_valid) {
      std::vector<std::string> v2;
      boost::split(v2, v1[i], boost::is_any_of(":"));
      sv.second.first = std::atoi(v2[0].c_str());
      sv.second.second = std::atoi(v2[1].c_str());
      i++;
    }
  }
}

void XgbPulsarService::SendLeftRightNodeSizes(
    const std::map<std::size_t, const std::pair<std::size_t, std::size_t>>&
        left_right_nodes_sizes) {
  if (!left_right_nodes_sizes.empty()) {
    std::string nids, content;
    nids = std::to_string(left_right_nodes_sizes.begin()->first);
    content = std::to_string(left_right_nodes_sizes.begin()->second.first) + ":" +
              std::to_string(left_right_nodes_sizes.begin()->second.second);
    std::for_each(++left_right_nodes_sizes.begin(), left_right_nodes_sizes.end(), [&](auto t) {
      nids += "_" + std::to_string(t.first);
      content += "_" + std::to_string(t.second.first) + ":" + std::to_string(t.second.second);
    });

    client->Send(LeftRightNodeSizesTopic(nids), content);
    DEBUG << "[S]nids: " << nids << ", left_right size: " << left_right_nodes_sizes.size()
          << std::endl;
  }
}

void XgbPulsarService::GetLeftRightNodeSizes(
    std::string nids,
    std::map<std::size_t, const std::pair<std::size_t, std::size_t>>& left_right_nodes_sizes) {
  std::string content;
  client->Receive(LeftRightNodeSizesTopic(nids), content);
  std::vector<std::string> v1, ids;
  boost::split(v1, content, boost::is_any_of("_"));
  boost::split(ids, nids, boost::is_any_of("_"));
  DEBUG << "[R]nids: " << nids << ", left_right size: " << v1.size() << std::endl;
  int i = 0;
  for (auto& id : ids) {
    std::vector<std::string> v2;
    boost::split(v2, v1[i], boost::is_any_of(":"));
    left_right_nodes_sizes.insert(
        {std::atoi(id.c_str()), {std::atoi(v2[0].c_str()), std::atoi(v2[1].c_str())}});
    i++;
  }
}

void XgbPulsarService::SendBlockInfos(
    const tbb::concurrent_unordered_map<std::size_t, xgbcomm::BlockInfo>& block_infos) {
  if (!block_infos.empty()) {
    std::string nids = std::to_string(block_infos.begin()->first);
    xgbcomm::BlockInfos bis;
    auto bs = bis.mutable_block_infos();
    bs->Add()->CopyFrom(block_infos.begin()->second);
    std::for_each(++block_infos.begin(), block_infos.end(), [&](auto& b) {
      nids += "_" + std::to_string(b.first);
      bs->Add()->CopyFrom(b.second);
    });
    client->Send(BlockInfosTopic(nids), bis);
    DEBUG << "[S]nids: " << nids << ", block_infos size: " << block_infos.size() << std::endl;
  }
}

void XgbPulsarService::GetBlockInfos(
    std::string nids, tbb::concurrent_unordered_map<std::size_t, xgbcomm::BlockInfo>& block_infos) {
  xgbcomm::BlockInfos bis;
  client->Receive(BlockInfosTopic(nids), bis);
  ParallelFor(bis.block_infos_size(), n_threads, [&](auto& i) {
    auto block_info = bis.block_infos(i);
    block_infos.insert({block_info.idx(), block_info});
  });
  DEBUG << "[R]nids: " << nids << ", block_infos size: " << block_infos.size() << std::endl;
}

void XgbPulsarService::SendNextNodes(int idx, const xgbcomm::NextNodesV2& next_nids) {
  client->Send(NextNodesTopic(idx), next_nids);
  DEBUG << "[S]idx:" << idx << ", next_nids size:" << next_nids.next_ids_size() << std::endl;
}

void XgbPulsarService::GetNextNodes(
    int idx, std::function<void(const google::protobuf::Map<std::uint32_t, std::uint32_t>&)>
                 process_part_idxs) {
  xgbcomm::NextNodesV2 next_nids;
  client->Receive(NextNodesTopic(idx), next_nids);
  process_part_idxs(next_nids.next_ids());
  DEBUG << "[R]idx:" << idx << ", next_nids size:" << next_nids.next_ids_size() << std::endl;
}

void XgbPulsarService::SendMetrics(int iter, const std::vector<double>& metrics) {
  if (!metrics.empty()) {
    std::string content = std::to_string(*metrics.begin());
    std::for_each(++metrics.begin(), metrics.end(),
                  [&](auto t) { content += "_" + std::to_string(t); });

    client->Send(MetricsTopic(iter), content);
    DEBUG << "[S]iter: " << iter << ", metrics size: " << metrics.size() << ", content: " << content
          << std::endl;
  }
}

void XgbPulsarService::GetMetrics(int iter, std::vector<std::string>& metrics) {
  std::string content;
  client->Receive(MetricsTopic(iter), content);
  std::vector<std::string> ms;
  boost::split(ms, content, boost::is_any_of("_"));
  std::for_each(ms.begin(), ms.end(), [&](auto& t) { metrics.emplace_back(std::move(t)); });
  DEBUG << "[R]iter: " << iter << ", metrics size: " << metrics.size() << ", content: " << content
        << std::endl;
}

template void XgbPulsarService::SendBestSplit(int bestIdx, xgboost::tree::CPUExpandEntry& entry,
                                              const xgbcomm::SplitsRequest& sr);

template void XgbPulsarService::HandleBestSplit(int bestIdx, xgboost::tree::CPUExpandEntry& entry,
                                                const xgbcomm::SplitsRequest& sr,
                                                xgbcomm::SplitsResponse& response);