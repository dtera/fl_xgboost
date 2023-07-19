// Copyright 2023 Tencent Inc.

#pragma once

#define JVM_CHECK_CALL(__expr)                                                 \
  {                                                                            \
    int __errcode = (__expr);                                                  \
    if (__errcode != 0) {                                                      \
      return __errcode;                                                        \
    }                                                                          \
  }

// helper functions
// set handle
void setHandle(JNIEnv *jenv, jlongArray jhandle, void *handle) {
#ifdef __APPLE__
  jlong out = (long)handle;
#else
  int64_t out = (int64_t)handle;
#endif
  jenv->SetLongArrayRegion(jhandle, 0, 1, &out);
}

#define JVM_SET_HANDLE                                                         \
  JVM_CHECK_CALL(ret);                                                         \
  setHandle(jenv, jout, handle);                                               \
  return ret;

inline jbyteArray buffer_to_jbytes(JNIEnv *jenv, yacl::Buffer &buf) {
  jbyteArray jba = jenv->NewByteArray(buf.size());
  jenv->SetByteArrayRegion(jba, 0, buf.size(), (jbyte *)buf.data());
  buf.reset();
  return jba;
}

inline void buffer_to_jbytes(JNIEnv *jenv, yacl::Buffer &buf, jbyteArray jba) {
  jenv->SetByteArrayRegion(jba, 0, buf.size(), (jbyte *)buf.data());
  buf.reset();
}

jbyteArray cipher_to_jbytes(JNIEnv *jenv,
                            const heu::lib::phe::Ciphertext &cipher) {
  auto buf = cipher.Serialize();
  return buffer_to_jbytes(jenv, buf);
}

void cipher_to_jbytes(JNIEnv *jenv, const heu::lib::phe::Ciphertext &cipher,
                      jbyteArray jba) {
  auto buf = cipher.Serialize();
  buffer_to_jbytes(jenv, buf, jba);
}

yacl::Buffer *jbytes_to_buffer(JNIEnv *jenv, const jbyteArray jba) {
  auto size = jenv->GetArrayLength(jba);
  jboolean is_copy;
  auto ba = jenv->GetPrimitiveArrayCritical(jba, &is_copy);
  yacl::Buffer *buf = new yacl::Buffer(ba, size);
  jenv->ReleasePrimitiveArrayCritical(jba, ba, 0);
  return buf;
}

void jbytes_to_cipher(JNIEnv *jenv, const jbyteArray jba,
                      heu::lib::phe::Ciphertext &cipher) {
  auto buf = jbytes_to_buffer(jenv, jba);
  cipher.Deserialize(*buf);
  buf->reset();
  delete buf;
}

void ciphers_to_jobjectarray(JNIEnv *jenv,
                             const heu::lib::phe::Ciphertext *ciphers,
                             jobjectArray jbas,
                             heu::lib::phe::Ciphertext **ciphers2 = nullptr) {
  auto len = jenv->GetArrayLength(jbas);
  for (int i = 0; i < len; ++i) {
    jenv->SetObjectArrayElement(jbas, i, cipher_to_jbytes(jenv, ciphers[i]));
    if (ciphers2 != nullptr && ciphers2[i] != nullptr) {
      delete[] (ciphers2[i]);
    }
  }
  if (ciphers2 != nullptr) {
    delete[] (ciphers2);
  }
}

void jobjectarray_to_ciphers(JNIEnv *jenv, const jobjectArray jbas,
                             heu::lib::phe::Ciphertext *ciphers) {
  auto len = jenv->GetArrayLength(jbas);
  for (int i = 0; i < len; ++i) {
    auto jba = (jbyteArray)jenv->GetObjectArrayElement(jbas, i);
    jbytes_to_cipher(jenv, jba, ciphers[i]);
    // jenv->ReleaseByteArrayElements(jba, jenv->GetByteArrayElements(jba, 0),
    // 0); jenv->DeleteLocalRef(jba);
  }
  // jenv->DeleteLocalRef(jbas);
}

void jobjectarray_to_cipher_pairs(JNIEnv *jenv, const jobjectArray jciphers1,
                                  const jobjectArray jciphers2,
                                  heu::lib::phe::Ciphertext *cs1,
                                  heu::lib::phe::Ciphertext *cs2) {
  auto len = jenv->GetArrayLength(jciphers1);
  for (int i = 0; i < len; ++i) {
    auto jba1 = (jbyteArray)jenv->GetObjectArrayElement(jciphers1, i);
    auto jba2 = (jbyteArray)jenv->GetObjectArrayElement(jciphers2, i);
    jbytes_to_cipher(jenv, jba1, cs1[i]);
    jbytes_to_cipher(jenv, jba2, cs2[i]);
    /*jenv->ReleaseByteArrayElements(jba1, jenv->GetByteArrayElements(jba1, 0),
                                   0);
    jenv->DeleteLocalRef(jba1);
    jenv->ReleaseByteArrayElements(jba2, jenv->GetByteArrayElements(jba2, 0),
                                   0);
    jenv->DeleteLocalRef(jba2);*/
  }
  // jenv->DeleteLocalRef(jciphers2);
}
