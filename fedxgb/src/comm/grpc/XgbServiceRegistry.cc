//
// Created by HqZhao on 2022/12/22.
//

#include "comm/grpc/XgbServiceRegistry.h"

namespace dmlc {
DMLC_REGISTRY_ENABLE(XgbServiceServerFactory);

DMLC_REGISTRY_ENABLE(XgbServiceClientFactory);
}  // namespace dmlc
