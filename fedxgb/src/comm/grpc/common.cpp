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

void pub_key2pb(xgbcomm::PubKeyResponse *response, opt_public_key_t *pub_) {
  response->set_nbits(pub_->nbits);
  response->set_lbits(pub_->lbits);
  mpz_t2_mpz_type(response->mutable_n(), pub_->n);
  mpz_t2_mpz_type(response->mutable_half_n(), pub_->half_n);
  mpz_t2_mpz_type(response->mutable_n_squared(), pub_->n_squared);
  mpz_t2_mpz_type(response->mutable_h_s(), pub_->h_s);
  mpz_t2_mpz_type(response->mutable_p_squared_mul_p_squared_inverse(),
                  pub_->P_squared_mul_P_squared_inverse);
  auto fb_mod_p_sqaured = response->mutable_fb_mod_p_sqaured();
  mpz_t2_mpz_type(fb_mod_p_sqaured->mutable_m_mod(), pub_->fb_mod_P_sqaured.m_mod);
  for (int i = 0; i <= pub_->fb_mod_P_sqaured.m_t; ++i) {
    auto m_table_g = fb_mod_p_sqaured->mutable_m_table_g()->Add();
    mpz_t2_mpz_type(m_table_g, pub_->fb_mod_P_sqaured.m_table_G[i]);
  }
  fb_mod_p_sqaured->set_m_h(pub_->fb_mod_P_sqaured.m_h);
  fb_mod_p_sqaured->set_m_t(pub_->fb_mod_P_sqaured.m_t);
  fb_mod_p_sqaured->set_m_w(pub_->fb_mod_P_sqaured.m_w);
  auto fb_mod_q_sqaured = response->mutable_fb_mod_q_sqaured();
  mpz_t2_mpz_type(fb_mod_q_sqaured->mutable_m_mod(), pub_->fb_mod_Q_sqaured.m_mod);
  for (int i = 0; i <= pub_->fb_mod_Q_sqaured.m_t; ++i) {
    auto m_table_g = fb_mod_q_sqaured->mutable_m_table_g()->Add();
    mpz_t2_mpz_type(m_table_g, pub_->fb_mod_Q_sqaured.m_table_G[i]);
  }
  fb_mod_q_sqaured->set_m_h(pub_->fb_mod_Q_sqaured.m_h);
  fb_mod_q_sqaured->set_m_t(pub_->fb_mod_Q_sqaured.m_t);
  fb_mod_q_sqaured->set_m_w(pub_->fb_mod_Q_sqaured.m_w);
}

void pb2pub_key(opt_public_key_t **pub, xgbcomm::PubKeyResponse &response) {
  *pub = (opt_public_key_t *)malloc(sizeof(opt_public_key_t));
  (*pub)->nbits = response.nbits();
  (*pub)->lbits = response.lbits();
  mpz_type2_mpz_t((*pub)->n, response.n());
  mpz_type2_mpz_t((*pub)->half_n, response.half_n());
  mpz_type2_mpz_t((*pub)->n_squared, response.n_squared());
  mpz_type2_mpz_t((*pub)->h_s, response.h_s());
  mpz_type2_mpz_t((*pub)->P_squared_mul_P_squared_inverse,
                  response.p_squared_mul_p_squared_inverse());
  auto mod_p_sqaured = response.fb_mod_p_sqaured();
  mpz_type2_mpz_t((*pub)->fb_mod_P_sqaured.m_mod, mod_p_sqaured.m_mod());
  (*pub)->fb_mod_P_sqaured.m_table_G = new mpz_t[mod_p_sqaured.m_t() + 1];
  for (int i = 0; i <= mod_p_sqaured.m_t(); ++i) {
    mpz_type2_mpz_t((*pub)->fb_mod_P_sqaured.m_table_G[i], mod_p_sqaured.m_table_g(i));
  }
  (*pub)->fb_mod_P_sqaured.m_h = mod_p_sqaured.m_h();
  (*pub)->fb_mod_P_sqaured.m_t = mod_p_sqaured.m_t();
  (*pub)->fb_mod_P_sqaured.m_w = mod_p_sqaured.m_w();
  auto mod_q_sqaured = response.fb_mod_q_sqaured();
  mpz_type2_mpz_t((*pub)->fb_mod_Q_sqaured.m_mod, mod_q_sqaured.m_mod());
  (*pub)->fb_mod_Q_sqaured.m_table_G = new mpz_t[mod_q_sqaured.m_t() + 1];
  for (int i = 0; i <= mod_q_sqaured.m_t(); ++i) {
    mpz_type2_mpz_t((*pub)->fb_mod_Q_sqaured.m_table_G[i], mod_q_sqaured.m_table_g(i));
  }
  (*pub)->fb_mod_Q_sqaured.m_h = mod_q_sqaured.m_h();
  (*pub)->fb_mod_Q_sqaured.m_t = mod_q_sqaured.m_t();
  (*pub)->fb_mod_Q_sqaured.m_w = mod_q_sqaured.m_w();
}
