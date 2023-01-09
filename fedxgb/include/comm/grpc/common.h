//
// Created by HqZhao on 2022/11/23.
//

#pragma once

#include <gmp.h>

#include "tree/param.h"
#include "xgbcomm.grpc.pb.h"
#include "xgboost/base.h"
#include "xgboost/logging.h"

#define DEBUG LOG(DEBUG)  // std::cout
#define INFO LOG(INFO)
#define ERROR LOG(FATAL)

enum class XgbCommType {
  GRAD_CONNECT = 1,
  GRAD_READ = 2,
  GRAD_WRITE = 3,
  SPLITS_CONNECT = 4,
  SPLITS_READ = 5,
  SPLITS_WRITE = 6,
  DONE = 7,
  FINISH = 8
};

struct GradPair {
  mpz_t grad;
  mpz_t hess;
};

struct XgbEncryptedSplit {
  std::string mask_id;
  struct GradPair left_sum;
  struct GradPair right_sum;
};

void mpz_t2_mpz_type(xgbcomm::MpzType *mt, const mpz_t &m_t);

void mpz_t2_mpz_type(xgbcomm::GradPair *gp, const struct GradPair &g_p);

void mpz_t2_mpz_type(xgbcomm::GradPair *gp, const xgboost::EncryptedGradientPair &g_p);

void mpz_t2_mpz_type(xgbcomm::GradPair *gp,
                     const xgboost::tree::GradStats<EncryptedType<double>> &g_p);

void mpz_type2_mpz_t(mpz_t &m_t, const xgbcomm::MpzType &mt);

void mpz_type2_mpz_t(struct GradPair &g_p, const xgbcomm::GradPair &gp);

void mpz_type2_mpz_t(xgboost::EncryptedGradientPair &g_p, const xgbcomm::GradPair &gp);

void mpz_type2_mpz_t(xgboost::tree::GradStats<EncryptedType<double>> &g_p,
                     const xgbcomm::GradPair &gp);

template <typename T>
void mpz_t2_mpz_type(xgbcomm::MpzType *mt, const EncryptedType<T> &m_t) {
  mt->set__mp_alloc(m_t.data_->_mp_alloc);
  mt->set__mp_size(m_t.data_->_mp_size);
  auto mp = m_t.data_->_mp_d;
  for (int j = 0; j < m_t.data_->_mp_size; ++j) {
    auto t = mt->mutable__mp_d()->Add();
    *t = mp[j];
  }
}

template <typename T>
void mpz_type2_mpz_t(EncryptedType<T> &m_t, const xgbcomm::MpzType &mt) {
  m_t.data_->_mp_alloc = mt._mp_alloc();
  m_t.data_->_mp_size = mt._mp_size();
  m_t.data_->_mp_d = new mp_limb_t[mt._mp_d().size()];
  for (int j = 0; j < mt._mp_d().size(); ++j) {
    m_t.data_->_mp_d[j] = mt._mp_d()[j];
  }
}

#define MAX_MESSAGE_LENGTH 10 * 1024 * 1024 * 1024l

#define RpcRequest(Request, RequestFunc, Response, SetRequest, SetResponse)               \
  Request request;                                                                        \
  Response response;                                                                      \
  SetRequest;                                                                             \
                                                                                          \
  ClientContext context;                                                                  \
  context.set_wait_for_ready(true);                                                       \
  auto s = stub_->RequestFunc(&context, request, &response);                              \
  if (s.ok()) {                                                                           \
    DEBUG << "** RPC request success! " << std::endl;                                     \
    SetResponse;                                                                          \
  } else {                                                                                \
    ERROR << "code: " << s.error_code() << ", error: " << s.error_message() << std::endl; \
  }

#define RpcRequest_(request, RequestFunc, Response, SetResponse)                          \
  Response response;                                                                      \
                                                                                          \
  ClientContext context;                                                                  \
  context.set_wait_for_ready(true);                                                       \
  auto s = stub_->RequestFunc(&context, request, &response);                              \
  if (s.ok()) {                                                                           \
    DEBUG << "** RPC request success! " << std::endl;                                     \
    SetResponse;                                                                          \
  } else {                                                                                \
    ERROR << "code: " << s.error_code() << ", error: " << s.error_message() << std::endl; \
  }
