//
// Created by HqZhao on 2022/12/22.
//

#include "comm/XgbServiceRegistry.h"

namespace dmlc {
DMLC_REGISTRY_ENABLE(XgbServiceServerFactory);

DMLC_REGISTRY_ENABLE(XgbServiceClientFactory);

DMLC_REGISTRY_ENABLE(XgbPulsarServiceFactory);

DMLC_REGISTRY_ENABLE(FederatedParamFactory);
}  // namespace dmlc

std::unique_ptr<XgbServiceServer> xgb_server_ = nullptr;
std::unique_ptr<XgbServiceClient> xgb_client_ = nullptr;
std::unique_ptr<XgbPulsarService> xgb_pulsar_ = nullptr;
std::unique_ptr<FederatedParam> fparam_ = nullptr;