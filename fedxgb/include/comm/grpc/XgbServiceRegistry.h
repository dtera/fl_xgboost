//
// Created by HqZhao on 2022/12/22.
//

#ifndef COMM_GRPC_XGBSERVICEREGISTRY_H_
#define COMM_GRPC_XGBSERVICEREGISTRY_H_

#include "comm/grpc/XgbClient.h"
#include "comm/grpc/XgbServer.h"
#include "dmlc/registry.h"

#define REGISTER_XGB_SERVEICE(ServiceName)                                                        \
  DMLC_REGISTRY_REGISTER(ServiceName##Factory, ServiceName##Factory, ServiceName).set_body([]() { \
    return new ServiceName();                                                                     \
  })

struct XgbServiceServerFactory
    : public dmlc::FunctionRegEntryBase<XgbServiceServerFactory,
                                        std::function<XgbServiceServer*()> > {};

struct XgbServiceClientFactory
    : public dmlc::FunctionRegEntryBase<XgbServiceClientFactory,
                                        std::function<XgbServiceClient*()> > {};

template <>
dmlc::Registry<XgbServiceServerFactory>* dmlc::Registry<XgbServiceServerFactory>::Get() {
  static Registry<XgbServiceServerFactory> inst;
  return &inst;
}

template <>
dmlc::Registry<XgbServiceClientFactory>* dmlc::Registry<XgbServiceClientFactory>::Get() {
  static Registry<XgbServiceClientFactory> inst;
  return &inst;
}

static __attribute__((unused))
XgbServiceServerFactory& __make_XgbServiceServerFactory_XgbServiceServer__ =
    ::dmlc::Registry<XgbServiceServerFactory>::Get()
        ->__REGISTER__("XgbServiceServer")
        .set_body([]() { return new XgbServiceServer(); });

static __attribute__((unused))
XgbServiceClientFactory& __make_XgbServiceClientFactory_XgbServiceClient__ =
    ::dmlc::Registry<XgbServiceClientFactory>::Get()
        ->__REGISTER__("XgbServiceClient")
        .set_body([]() { return new XgbServiceClient(); });

#define FIND_XGB_SERVICE(ServiceName) \
  dmlc::Registry<ServiceName##Factory>::Find(#ServiceName)->body()

#endif  // COMM_GRPC_XGBSERVICEREGISTRY_H_