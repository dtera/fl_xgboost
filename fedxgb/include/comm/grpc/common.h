//
// Created by HqZhao on 2022/11/23.
//

#pragma once

#include <gmp.h>

#include "tree/param.h"
#include "xgbcomm.grpc.pb.h"
#include "xgboost/base.h"
#include "xgboost/logging.h"

// #define DEBUG LOG(CONSOLE)
#define DEBUG LOG(DEBUG)
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

  friend std::ostream &operator<<(std::ostream &os, const GradPair &p) {
    os << "grad: " << mpz_get_d(p.grad) << ", hess: " << mpz_get_d(p.hess);
    return os;
  }
};

struct XgbEncryptedSplit {
  std::string mask_id;
  struct GradPair left_sum;
  struct GradPair right_sum;
};

struct PositionBlockInfo {
  size_t n_left;
  size_t n_right;
  size_t n_offset_left;
  size_t n_offset_right;
  size_t *left_data_;
  size_t *right_data_;
};

void mpz_t2_mpz_type(xgbcomm::MpzType *mt, const mpz_t &m_t);

void mpz_t2_mpz_type(xgbcomm::GradPair *gp, const struct GradPair &g_p);

void mpz_t2_mpz_type(xgbcomm::GradPair *gp, const xgboost::EncryptedGradientPair &g_p);

void mpz_t2_mpz_type(xgbcomm::GradPair *gp, const xgboost::EncryptedGradientPairPrecise &g_p);

void mpz_t2_mpz_type(xgbcomm::GradPair *gp,
                     const xgboost::tree::GradStats<EncryptedType<double>> &g_p);

void mpz_type2_mpz_t(mpz_t &m_t, const xgbcomm::MpzType &mt);

void mpz_type2_mpz_t(struct GradPair &g_p, const xgbcomm::GradPair &gp);

void mpz_type2_mpz_t(xgboost::EncryptedGradientPair &g_p, const xgbcomm::GradPair &gp);

void mpz_type2_mpz_t(xgboost::EncryptedGradientPairPrecise &g_p, const xgbcomm::GradPair &gp);

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

template <typename T>
void println_vector(const std::vector<T> &vs, const std::string &name,
                    const std::string &sep = ", ") {
  std::cout << name << "(len:" << vs.size() << ")--> [";
  if (vs.size() != 0) {
    std::cout << vs[0];
  }
  for (int i = 1; i < vs.size(); i++) {
    std::cout << sep << vs[i];
  }
  std::cout << "]" << std::endl;
}

template <typename K, typename V>
void println_map(const std::map<K, V> &m, const std::string &name) {
  std::cout << name << "(len:" << m.size() << ")--> \n{";
  for ([[maybe_unused]] auto &entry : m) {
    std::cout << "\n\t" << entry.first << ": " << entry.second << ",";
  }
  std::cout << "\n}" << std::endl;
}

template <typename T>
std::string join_vector(const std::vector<T> &vs, const std::string &sep = ",",
                        const std::string &start = "", const std::string &end = "") {
  std::string res = start;
  if (vs.size() != 0) {
    res += vs[0];
  }
  for (int i = 1; i < vs.size(); i++) {
    res += sep + vs[i];
  }
  res += end;
  return res;
}

void pub_key2pb(xgbcomm::PubKeyResponse *response, opt_public_key_t *pub_);

void pb2pub_key(opt_public_key_t **pub, xgbcomm::PubKeyResponse &response);

namespace {
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
}  // namespace