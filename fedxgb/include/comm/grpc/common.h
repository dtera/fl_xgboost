//
// Created by HqZhao on 2022/11/23.
//

#pragma once

#include <gmp.h>

#include "xgbcomm.grpc.pb.h"
#include "xgboost/logging.h"

#define DEBUG LOG(DEBUG)  // std::cout
#define INFO LOG(INFO)

using xgbcomm::MpzType;

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

struct XgbEncriptedSplit {
  std::string mask_id;
  mpz_t encripted_grad_pair_sum;
};

void mpz_t2_mpz_type(MpzType *mt, const mpz_t &m_t);

void mpz_type2_mpz_t(mpz_t &m_t, const MpzType &mt);

#define MAX_MESSAGE_LENGTH 10 * 1024 * 1024 * 1024l

#define RpcRequest(Request, RequestFunc, Response, SetRequest, SetResponse)                   \
  Request request;                                                                            \
  Response response;                                                                          \
  SetRequest;                                                                                 \
                                                                                              \
  ClientContext context;                                                                      \
  auto status = stub_->RequestFunc(&context, request, &response);                             \
  if (status.ok()) {                                                                          \
    cout << "** RPC request success! " << endl;                                               \
    SetResponse;                                                                              \
  } else {                                                                                    \
    cerr << "code: " << status.error_code() << ", error: " << status.error_message() << endl; \
  }
