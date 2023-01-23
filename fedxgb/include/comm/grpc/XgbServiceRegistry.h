//
// Created by HqZhao on 2022/12/22.
//

#pragma once

#include "comm/grpc/XgbClient.h"
#include "comm/grpc/XgbServer.h"
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

struct FederatedParamFactory;

namespace dmlc {
#define DMLC_REGISTRY_DECLARE(EntryType) \
  template <>                            \
  Registry<EntryType>* Registry<EntryType>::Get();

DMLC_REGISTRY_DECLARE(XgbServiceServerFactory);
DMLC_REGISTRY_DECLARE(XgbServiceClientFactory);
DMLC_REGISTRY_DECLARE(FederatedParamFactory);
}  // namespace dmlc

#define FIND_XGB_SERVICE(ServiceName) \
  dmlc::Registry<ServiceName##Factory>::Find(#ServiceName)->body()

extern std::unique_ptr<XgbServiceServer> xgb_server_;
extern std::unique_ptr<XgbServiceClient> xgb_client_;
extern std::unique_ptr<FederatedParam> fparam_;

REGISTER_XGB_SERVEICE(XgbServiceServer, xgb_server_);
REGISTER_XGB_SERVEICE(XgbServiceClient, xgb_client_);
REGISTER_XGB_SERVEICE(FederatedParam, fparam_);

inline bool IsFederated() { return fparam_->dsplit == DataSplitMode::kCol; }

inline bool SelfPartNotBest(int32_t part_id) { return part_id != fparam_->fl_part_id; }

inline bool IsFederatedAndSelfPartNotBest(int32_t part_id) {
  return IsFederated() && SelfPartNotBest(part_id);
}
