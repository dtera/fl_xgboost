//
// Created by HqZhao on 2022/12/22.
//

#pragma once

#include "comm/grpc/XgbClient.h"
#include "comm/grpc/XgbServer.h"
#include "dmlc/registry.h"

using namespace dmlc;

#define REGISTER_XGB_SERVEICE(ServiceName)                                                         \
  struct ServiceName##Factory                                                                      \
      : public dmlc::FunctionRegEntryBase<ServiceName##Factory, std::function<ServiceName *()> > { \
  };                                                                                               \
                                                                                                   \
  DMLC_REGISTRY_REGISTER(ServiceName##Factory, ServiceName##Factory, ServiceName).set_body([]() {  \
    return new ServiceName();                                                                      \
  })

struct XgbServiceServerFactory;

struct XgbServiceClientFactory;

DMLC_REGISTRY_ENABLE(XgbServiceServerFactory);

DMLC_REGISTRY_ENABLE(XgbServiceClientFactory);

REGISTER_XGB_SERVEICE(XgbServiceServer);

REGISTER_XGB_SERVEICE(XgbServiceClient);

#define FIND_XGB_SERVICE(ServiceName) \
  dmlc::Registry<ServiceName##Factory>::Find(#ServiceName)->body()
