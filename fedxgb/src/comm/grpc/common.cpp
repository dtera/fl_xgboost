//
// Created by HqZhao on 2022/12/1.
//
#include "comm/grpc/common.h"

void mpz_t2_mpz_type(MpzType *mt, const mpz_t &m_t) {
  mt->set__mp_alloc(m_t->_mp_alloc);
  mt->set__mp_size(m_t->_mp_size);
  auto mp = m_t->_mp_d;
  for (int j = 0; j < m_t->_mp_size; ++j) {
    auto t = mt->mutable__mp_d()->Add();
    *t = mp[j];
  }
}

void mpz_type2_mpz_t(mpz_t &m_t, const MpzType &mt) {
  m_t->_mp_alloc = mt._mp_alloc();
  m_t->_mp_size = mt._mp_size();
  m_t->_mp_d = new mp_limb_t[mt._mp_d().size()];
  for (int j = 0; j < mt._mp_d().size(); ++j) {
    m_t->_mp_d[j] = mt._mp_d()[j];
  }
}
