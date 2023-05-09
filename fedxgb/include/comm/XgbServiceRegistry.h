//
// Created by HqZhao on 2022/12/22.
//

#pragma once

#include "comm/grpc/XgbClient.h"
#include "comm/grpc/XgbServer.h"
#include "comm/pulsar/XgbPulsarService.h"
#include "dmlc/registry.h"
#include "xgboost/federated_param.h"

using namespace xgboost;

namespace {
#define REGISTER_XGB_SERVEICE(ServiceName, serv)                                                  \
  struct ServiceName##Factory                                                                     \
      : public dmlc::FunctionRegEntryBase<ServiceName##Factory,                                   \
                                          std::function<std::unique_ptr<ServiceName>()>> {};      \
                                                                                                  \
  DMLC_REGISTRY_REGISTER(ServiceName##Factory, ServiceName##Factory, ServiceName).set_body([]() { \
    if (serv.get() == nullptr) {                                                                  \
      serv.reset(new ServiceName());                                                              \
    }                                                                                             \
    return std::move(serv);                                                                       \
  })
}  // namespace

struct XgbServiceServerFactory;

struct XgbServiceClientFactory;

struct XgbPulsarServiceFactory;

struct FederatedParamFactory;

namespace dmlc {
#define DMLC_REGISTRY_DECLARE(EntryType) \
  template <>                            \
  Registry<EntryType> *Registry<EntryType>::Get();

DMLC_REGISTRY_DECLARE(XgbServiceServerFactory)
DMLC_REGISTRY_DECLARE(XgbServiceClientFactory)
DMLC_REGISTRY_DECLARE(XgbPulsarServiceFactory)
DMLC_REGISTRY_DECLARE(FederatedParamFactory)
}  // namespace dmlc

#define FIND_XGB_SERVICE(ServiceName) \
  dmlc::Registry<ServiceName##Factory>::Find(#ServiceName)->body()

extern std::unique_ptr<XgbServiceServer> xgb_server;
extern std::unique_ptr<XgbServiceClient> xgb_client;
extern std::unique_ptr<XgbPulsarService> xgb_pulsar;
extern std::unique_ptr<FederatedParam> fparam;

REGISTER_XGB_SERVEICE(XgbServiceServer, xgb_server);
REGISTER_XGB_SERVEICE(XgbServiceClient, xgb_client);
REGISTER_XGB_SERVEICE(XgbPulsarService, xgb_pulsar);
REGISTER_XGB_SERVEICE(FederatedParam, fparam);

/*
inline bool IsPulsar() { return fparam_->fl_comm_type == FedratedCommType::Pulsar; }

inline bool IsGuest() { return fparam_->fl_role == FedratedRole::Guest; }

inline bool IsFederated() {
  // return fparam_->dsplit == DataSplitMode::kCol;
  return fparam_->fl_on == 1;
}

inline bool NotSelfPart(int32_t part_id) { return part_id != fparam_->fl_part_id; }

inline bool IsFederatedAndSelfPartNotBest(int32_t part_id) {
  return IsFederated() && NotSelfPart(part_id);
}
*/

//==========================================test to debug==========================================
inline void test_hist(bst_bin_t nidx, bst_bin_t bin_id, const EncryptedType<double> &grad,
                      const EncryptedType<double> &hess, bst_feature_t fidx = -1,
                      const GradStats<EncryptedType<double>> &left_sum =
                          GradStats<EncryptedType<double>>(EncryptedType<double>(0),
                                                           EncryptedType<double>(0)),
                      const GradStats<EncryptedType<double>> &right_sum =
                          GradStats<EncryptedType<double>>(EncryptedType<double>(0),
                                                           EncryptedType<double>(0))) {
  SplitsRequest req;
  req.set_nidx(nidx);
  req.set_part_id(-2);
  auto es = req.add_encrypted_splits();
  es->set_mask_id(to_string(fidx == -1 ? bin_id : fidx));
  GradStats<EncryptedType<double>> stat(EncryptedType<double>(0), EncryptedType<double>(0));
  stat.Add(grad, hess);
  mpz_t2_mpz_type(es->mutable_left_sum(), stat);
  // xgb_client_->SendEncryptedSplits(req, [](SplitsResponse &res) {});
}

inline void test_hist(bst_bin_t nidx, bst_bin_t bin_id, const double &grad, const double &hess,
                      bst_feature_t fidx = -1,
                      const GradStats<double> &left_sum = GradStats<double>(0, 0),
                      const GradStats<double> &right_sum = GradStats<double>(0, 0)) {
  if (left_sum.sum_grad == 0 && right_sum.sum_grad == 0) {
    cout << "**nid: " << nidx << ", bin_id: " << bin_id << ", hist: " << grad << "/" << hess
         << endl;
  } else {
    cout << "nid: " << nidx << ", fidx: " << fidx << ", left_sum: " << left_sum
         << ", right_sum: " << right_sum << "\n\t  bin_id: " << bin_id << ", hist: " << grad << "/"
         << hess << endl;
  }
}
//==========================================test to debug==========================================