//
// Created by HqZhao on 2022/12/1.
//
#include "comm/grpc/common.h"

void mpz_t2_mpz_type(xgbcomm::MpzType *mt, const mpz_t &m_t) {
  mt->set__mp_alloc(m_t->_mp_alloc);
  mt->set__mp_size(m_t->_mp_size);
  auto mp = m_t->_mp_d;
  for (int j = 0; j < m_t->_mp_size; ++j) {
    auto t = mt->mutable__mp_d()->Add();
    *t = mp[j];
  }
}

void mpz_t2_mpz_type(xgbcomm::GradPair *gp, const GradPair &g_p) {
  mpz_t2_mpz_type(gp->mutable_grad(), g_p.grad);
  mpz_t2_mpz_type(gp->mutable_hess(), g_p.hess);
}

void mpz_t2_mpz_type(xgbcomm::GradPair *gp, const xgboost::EncryptedGradientPair &g_p) {
  mpz_t2_mpz_type(gp->mutable_grad(), g_p.grad_);
  mpz_t2_mpz_type(gp->mutable_hess(), g_p.hess_);
}

void mpz_t2_mpz_type(xgbcomm::GradPair *gp,
                     const xgboost::tree::GradStats<EncryptedType<double>> &g_p) {
  mpz_t2_mpz_type(gp->mutable_grad(), g_p.sum_grad);
  mpz_t2_mpz_type(gp->mutable_hess(), g_p.sum_hess);
}

void mpz_type2_mpz_t(mpz_t &m_t, const xgbcomm::MpzType &mt) {
  m_t->_mp_alloc = mt._mp_alloc();
  m_t->_mp_size = mt._mp_size();
  m_t->_mp_d = new mp_limb_t[mt._mp_d().size()];
  for (int j = 0; j < mt._mp_d().size(); ++j) {
    m_t->_mp_d[j] = mt._mp_d()[j];
  }
}

void mpz_type2_mpz_t(GradPair &g_p, const xgbcomm::GradPair &gp) {
  mpz_type2_mpz_t(g_p.grad, gp.grad());
  mpz_type2_mpz_t(g_p.hess, gp.hess());
}

void mpz_type2_mpz_t(xgboost::EncryptedGradientPair &g_p, const xgbcomm::GradPair &gp) {
  mpz_type2_mpz_t(g_p.grad_, gp.grad());
  mpz_type2_mpz_t(g_p.hess_, gp.hess());
}

void mpz_type2_mpz_t(xgboost::tree::GradStats<EncryptedType<double>> &g_p,
                     const xgbcomm::GradPair &gp) {
  mpz_type2_mpz_t(g_p.sum_grad, gp.grad());
  mpz_type2_mpz_t(g_p.sum_hess, gp.hess());
}
