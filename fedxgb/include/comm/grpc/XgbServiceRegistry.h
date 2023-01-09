//
// Created by HqZhao on 2022/12/22.
//

#pragma once

#include "comm/grpc/XgbClient.h"
#include "comm/grpc/XgbServer.h"
#include "dmlc/registry.h"

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

struct XgbServiceServerFactory;

struct XgbServiceClientFactory;

namespace dmlc {
#define DMLC_REGISTRY_DECLARE(EntryType) \
  template <>                            \
  Registry<EntryType>* Registry<EntryType>::Get();

DMLC_REGISTRY_DECLARE(XgbServiceServerFactory);
DMLC_REGISTRY_DECLARE(XgbServiceClientFactory);
}  // namespace dmlc

extern std::unique_ptr<XgbServiceServer> xgb_server_;
extern std::unique_ptr<XgbServiceClient> xgb_client_;

REGISTER_XGB_SERVEICE(XgbServiceServer, xgb_server_);
REGISTER_XGB_SERVEICE(XgbServiceClient, xgb_client_);

#define FIND_XGB_SERVICE(ServiceName) \
  dmlc::Registry<ServiceName##Factory>::Find(#ServiceName)->body()
