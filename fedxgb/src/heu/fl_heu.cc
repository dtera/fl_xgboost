// Copyright 2023 Tencent Inc.

#include "heu/fl_heu.h"
#include "heu/heu_api.h"
#include "heu/heu_utils.h"

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    heKitCreate
 * Signature: (II[J)I
 */
JNIEXPORT jint JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_heKitCreate(
    JNIEnv *jenv, jclass, jint jschema, jint jkey_size, jlongArray jout) {
  HeKitHandle handle;
  int ret = HeKitCreate(jschema, jkey_size, &handle);
  JVM_SET_HANDLE
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    heKitCreateFromKeys
 * Signature: ([B[B[J)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_heKitCreateFromKeys(
    JNIEnv *jenv, jclass, jbyteArray jpubhandle, jbyteArray jsechandle,
    jlongArray jout) {
  size_t pub_key_len = jenv->GetArrayLength(jpubhandle);
  size_t sec_key_len = jenv->GetArrayLength(jsechandle);
  auto pub_key_handle = (jbyte *)jenv->GetPrimitiveArrayCritical(jpubhandle, 0);
  auto sec_key_handle = (jbyte *)jenv->GetPrimitiveArrayCritical(jsechandle, 0);
  HeKitHandle handle;
  int ret = HeKitCreateFromKeys(pub_key_handle, pub_key_len, sec_key_handle,
                                sec_key_len, &handle);
  jenv->ReleasePrimitiveArrayCritical(jpubhandle, pub_key_handle, 0);
  jenv->ReleasePrimitiveArrayCritical(jsechandle, sec_key_handle, 0);
  JVM_SET_HANDLE
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    heKitFree
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_heKitFree(
    JNIEnv *, jclass, jlong jhandle) {
  return HeKitFree((HeKitHandle)jhandle);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    destinationHeKitCreate
 * Signature: ([B[J)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_destinationHeKitCreate(
    JNIEnv *jenv, jclass, jbyteArray jhandle, jlongArray jout) {
  size_t len = jenv->GetArrayLength(jhandle);
  DestinationHeKitHandle handle;
  auto pubKey = (jbyte *)jenv->GetPrimitiveArrayCritical(jhandle, 0);
  int ret = DestinationHeKitCreate((PubKeyHandle)pubKey, len, &handle);
  jenv->ReleasePrimitiveArrayCritical(jhandle, pubKey, 0);
  JVM_SET_HANDLE
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    destinationHeKitFree
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_destinationHeKitFree(
    JNIEnv *, jclass, jlong jhandle) {
  return DestinationHeKitFree((DestinationHeKitHandle)jhandle);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    getPubKey
 * Signature: (J[B)I
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_getPubKey(JNIEnv *jenv, jclass,
                                                      jlong jhandle) {
  jbyteArray jout;
  GetPubKey((HeKitHandle)jhandle, nullptr,
            [&](yacl::Buffer &buf) { jout = buffer_to_jbytes(jenv, buf); });
  return jout;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    getSecKey
 * Signature: (J)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_getSecKey(JNIEnv *jenv, jclass,
                                                      jlong jhandle) {
  jbyteArray jout;
  GetSecKey((HeKitHandle)jhandle, nullptr,
            [&](yacl::Buffer &buf) { jout = buffer_to_jbytes(jenv, buf); });
  return jout;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    encrypt
 * Signature: (J[D[[B)I
 */
JNIEXPORT jint JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_encrypt(
    JNIEnv *jenv, jclass, jlong jhandle, jdoubleArray jplaintexts,
    jobjectArray jbas) {
  auto *plaintexts = jenv->GetDoubleArrayElements(jplaintexts, 0);
  auto len = jenv->GetArrayLength(jplaintexts);
  heu::lib::phe::Ciphertext ciphers[len];
  auto ret = Encrypt<double>((HeKitHandle)jhandle, plaintexts, len, ciphers);
  jenv->ReleaseDoubleArrayElements(jplaintexts, plaintexts, 0);
  /*ParallelFor(len, omp_get_num_procs(), [&](int i) {
    jbyteArray jba = cipher_to_jbytes(jenv, ciphers[i]);
    jenv->SetObjectArrayElement(jbas, i, jba);
  });*/
  ciphers_to_jobjectarray(jenv, ciphers, jbas);

  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    encryptPair
 * Signature: (J[D[D[[B)I
 */
JNIEXPORT jint JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_encryptPair(
    JNIEnv *jenv, jclass, jlong jhandle, jdoubleArray jplaintexts1,
    jdoubleArray jplaintexts2, jobjectArray jbas) {
  auto *plaintexts1 = jenv->GetDoubleArrayElements(jplaintexts1, 0);
  auto *plaintexts2 = jenv->GetDoubleArrayElements(jplaintexts2, 0);
  auto len = jenv->GetArrayLength(jplaintexts1);
  heu::lib::phe::Ciphertext ciphers[len];
  auto ret = Encrypt<double>((HeKitHandle)jhandle, plaintexts1, plaintexts2,
                             len, ciphers);
  jenv->ReleaseDoubleArrayElements(jplaintexts1, plaintexts1, 0);
  jenv->ReleaseDoubleArrayElements(jplaintexts2, plaintexts2, 0);
  ciphers_to_jobjectarray(jenv, ciphers, jbas);

  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    decrypt
 * Signature: (J[[B[D)I
 */
JNIEXPORT jint JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_decrypt(
    JNIEnv *jenv, jclass, jlong jhandle, jobjectArray jbas,
    jdoubleArray jplaintexts) {
  auto len = jenv->GetArrayLength(jbas);
  heu::lib::phe::Ciphertext ciphers[len];
  /*ParallelFor(len, omp_get_num_procs(), [&](int i) {
    auto jba = (jbyteArray)jenv->GetObjectArrayElement(jbas, i);
    jbytes_to_cipher(jenv, jba, ciphers[i]);
  });*/
  jobjectarray_to_ciphers(jenv, jbas, ciphers);
  double plaintexts[len];
  auto ret = Decrypt((HeKitHandle)jhandle, ciphers, len, plaintexts);
  jenv->SetDoubleArrayRegion(jplaintexts, 0, len, plaintexts);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    decryptPair
 * Signature: (J[[B[D[D)I
 */
JNIEXPORT jint JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_decryptPair(
    JNIEnv *jenv, jclass, jlong jhandle, jobjectArray jbas,
    jdoubleArray jplaintexts1, jdoubleArray jplaintexts2) {
  auto len = jenv->GetArrayLength(jbas);
  heu::lib::phe::Ciphertext ciphers[len];
  jobjectarray_to_ciphers(jenv, jbas, ciphers);
  double plaintexts1[len], plaintexts2[len];
  auto ret =
      Decrypt((HeKitHandle)jhandle, ciphers, len, plaintexts1, plaintexts2);
  jenv->SetDoubleArrayRegion(jplaintexts1, 0, len, plaintexts1);
  jenv->SetDoubleArrayRegion(jplaintexts2, 0, len, plaintexts2);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    encrypt1
 * Signature: (JD)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_encrypt1(JNIEnv *jenv, jclass,
                                                     jlong jhandle,
                                                     jdouble jplaintext) {
  heu::lib::phe::Ciphertext cipher;
  CheckCall(Encrypt<double>((HeKitHandle)jhandle, jplaintext, &cipher),
            "encrypt1");
  return cipher_to_jbytes(jenv, cipher);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    encryptPair1
 * Signature: (JDD)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_encryptPair1(JNIEnv *jenv, jclass,
                                                         jlong jhandle,
                                                         jdouble jplaintext1,
                                                         jdouble jplaintext2) {
  heu::lib::phe::Ciphertext cipher;
  CheckCall(
      Encrypt<double>((HeKitHandle)jhandle, jplaintext1, jplaintext2, &cipher),
      "encryptPair1");
  return cipher_to_jbytes(jenv, cipher);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    decrypt1
 * Signature: (J[B)D
 */
JNIEXPORT jdouble JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_decrypt1(
    JNIEnv *jenv, jclass, jlong jhandle, jbyteArray jba) {
  heu::lib::phe::Ciphertext cipher;
  jbytes_to_cipher(jenv, jba, cipher);
  jdouble res;
  CheckCall(Decrypt((HeKitHandle)jhandle, cipher, &res), "decrypt1");
  return res;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    decryptPair1
 * Signature: (J[B[D)I
 */
JNIEXPORT jint JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_decryptPair1(
    JNIEnv *jenv, jclass, jlong jhandle, jbyteArray jba, jdoubleArray jout) {
  heu::lib::phe::Ciphertext cipher;
  jbytes_to_cipher(jenv, jba, cipher);
  double res[2];
  auto ret = Decrypt((HeKitHandle)jhandle, cipher, res, res + 1);
  jenv->SetDoubleArrayRegion(jout, 0, 2, res);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    dhekitEncrypt
 * Signature: (J[D[[B)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_dhekitEncrypt(
    JNIEnv *jenv, jclass, jlong jhandle, jdoubleArray jplaintexts,
    jobjectArray jbas) {
  auto *plaintexts = jenv->GetDoubleArrayElements(jplaintexts, 0);
  auto len = jenv->GetArrayLength(jplaintexts);
  heu::lib::phe::Ciphertext ciphers[len];
  auto ret = DestinationHeKitEncrypt<double>((DestinationHeKitHandle)jhandle,
                                             plaintexts, len, ciphers);
  jenv->ReleaseDoubleArrayElements(jplaintexts, plaintexts, 0);
  ciphers_to_jobjectarray(jenv, ciphers, jbas);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    dhekitEncryptPair
 * Signature: (J[D[D[[B)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_dhekitEncryptPair(
    JNIEnv *jenv, jclass, jlong jhandle, jdoubleArray jplaintexts1,
    jdoubleArray jplaintexts2, jobjectArray jbas) {
  auto *plaintexts1 = jenv->GetDoubleArrayElements(jplaintexts1, 0);
  auto *plaintexts2 = jenv->GetDoubleArrayElements(jplaintexts2, 0);
  auto len = jenv->GetArrayLength(jplaintexts1);
  heu::lib::phe::Ciphertext ciphers[len];
  auto ret = DestinationHeKitEncrypt<double>(
      (DestinationHeKitHandle)jhandle, plaintexts1, plaintexts2, len, ciphers);
  jenv->ReleaseDoubleArrayElements(jplaintexts1, plaintexts1, 0);
  jenv->ReleaseDoubleArrayElements(jplaintexts2, plaintexts2, 0);
  ciphers_to_jobjectarray(jenv, ciphers, jbas);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    dhekitEncrypt1
 * Signature: (JD)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_dhekitEncrypt1(JNIEnv *jenv, jclass,
                                                           jlong jhandle,
                                                           jdouble jplaintext) {
  heu::lib::phe::Ciphertext cipher;
  CheckCall(DestinationHeKitEncrypt<double>((DestinationHeKitHandle)jhandle,
                                            jplaintext, &cipher),
            "dhekitEncrypt1");
  return cipher_to_jbytes(jenv, cipher);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    dhekitEncryptPair1
 * Signature: (JDD)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_dhekitEncryptPair1(
    JNIEnv *jenv, jclass, jlong jhandle, jdouble jplaintext1,
    jdouble jplaintext2) {
  heu::lib::phe::Ciphertext cipher;
  CheckCall(DestinationHeKitEncrypt<double>((DestinationHeKitHandle)jhandle,
                                            jplaintext1, jplaintext2, &cipher),
            "dhekitEncryptPair1");
  return cipher_to_jbytes(jenv, cipher);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    addCipher
 * Signature: (J[B[B)[B
 */
JNIEXPORT
jbyteArray JNICALL Java_com_tencent_angel_fl_crypto_heu_HeuJNI_addCipher(
    JNIEnv *jenv, jclass, jlong jdhandle, jbyteArray jba1, jbyteArray jba2) {
  heu::lib::phe::Ciphertext c1, c2;
  jbytes_to_cipher(jenv, jba1, c1);
  jbytes_to_cipher(jenv, jba2, c2);
  auto c = AddCipher((DestinationHeKitHandle)jdhandle, c1, c2);
  return cipher_to_jbytes(jenv, c);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    addCipherInplace
 * Signature: (J[B[B)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_addCipherInplace(
    JNIEnv *jenv, jclass, jlong jdhandle, jbyteArray jba1, jbyteArray jba2) {
  heu::lib::phe::Ciphertext c1, c2;
  jbytes_to_cipher(jenv, jba1, c1);
  jbytes_to_cipher(jenv, jba2, c2);
  CheckCall(AddCipherInplace((DestinationHeKitHandle)jdhandle, c1, c2),
            "addCipherInplace");
  return cipher_to_jbytes(jenv, c1);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    addCiphersInplace
 * Signature: (J[[B[[B)[[B
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_addCiphersInplace(
    JNIEnv *jenv, jclass, jlong jdhandle, jobjectArray jciphers1,
    jobjectArray jciphers2) {
  auto len = jenv->GetArrayLength(jciphers1);
  heu::lib::phe::Ciphertext cs1[len], cs2[len];
  jobjectarray_to_cipher_pairs(jenv, jciphers1, jciphers2, cs1, cs2);
  auto ret = AddCiphersInplace((DestinationHeKitHandle)jdhandle, cs1, cs2, len);
  ciphers_to_jobjectarray(jenv, cs1, jciphers1);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    scatterAddCiphersInplace
 * Signature: (J[[B[[B[J)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_scatterAddCiphersInplace(
    JNIEnv *jenv, jclass, jlong jdhandle, jobjectArray jciphers1,
    jobjectArray jciphers2, jlongArray jindexes, jboolean parallel) {
  auto len1 = jenv->GetArrayLength(jciphers1);
  auto len2 = jenv->GetArrayLength(jciphers2);
  auto indexes = jenv->GetLongArrayElements(jindexes, 0);
  heu::lib::phe::Ciphertext cs1[len1], cs2[len2];
  jobjectarray_to_ciphers(jenv, jciphers1, cs1);
  jobjectarray_to_ciphers(jenv, jciphers2, cs2);
  auto ret = ScatterAddCiphersInplace((DestinationHeKitHandle)jdhandle, cs1,
                                      cs2, indexes, len2, parallel);
  jenv->ReleaseLongArrayElements(jindexes, indexes, 0);
  ciphers_to_jobjectarray(jenv, cs1, jciphers1);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    addCiphersInplaceAxis0
 * Signature: (J[[B[[[B)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_addCiphersInplaceAxis0(
    JNIEnv *jenv, jclass, jlong jdhandle, jobjectArray jciphers1,
    jobjectArray jciphers2) {
  auto len = jenv->GetArrayLength(jciphers2);
  heu::lib::phe::Ciphertext cs1[len];
  heu::lib::phe::Ciphertext **cs2 = new heu::lib::phe::Ciphertext *[len];
  int row_size[len];
  for (int i = 0; i < len; ++i) {
    auto jba1 = (jbyteArray)jenv->GetObjectArrayElement(jciphers1, i);
    auto joa2 = (jobjectArray)jenv->GetObjectArrayElement(jciphers2, i);
    jbytes_to_cipher(jenv, jba1, cs1[i]);
    row_size[i] = jenv->GetArrayLength(joa2);
    cs2[i] = new heu::lib::phe::Ciphertext[row_size[i]];
    for (int j = 0; j < row_size[i]; ++j) {
      auto jba2 = (jbyteArray)jenv->GetObjectArrayElement(joa2, j);
      jbytes_to_cipher(jenv, jba2, cs2[i][j]);
    }
    jenv->DeleteLocalRef(joa2);
  }
  auto ret = AddCiphersInplaceAxis0_((DestinationHeKitHandle)jdhandle, cs1, cs2,
                                     row_size, len);
  ciphers_to_jobjectarray(jenv, cs1, jciphers1, cs2);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    subCipher
 * Signature: (J[B[B)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_subCipher(JNIEnv *jenv, jclass,
                                                      jlong jdhandle,
                                                      jbyteArray jba1,
                                                      jbyteArray jba2) {
  heu::lib::phe::Ciphertext c1, c2;
  jbytes_to_cipher(jenv, jba1, c1);
  jbytes_to_cipher(jenv, jba2, c2);
  auto c = SubCipher((DestinationHeKitHandle)jdhandle, c1, c2);
  return cipher_to_jbytes(jenv, c);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    subCipherInplace
 * Signature: (J[B[B)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_subCipherInplace(
    JNIEnv *jenv, jclass, jlong jdhandle, jbyteArray jba1, jbyteArray jba2) {
  heu::lib::phe::Ciphertext c1, c2;
  jbytes_to_cipher(jenv, jba1, c1);
  jbytes_to_cipher(jenv, jba2, c2);
  CheckCall(SubCipherInplace((DestinationHeKitHandle)jdhandle, c1, c2),
            "subCipherInplace");
  return cipher_to_jbytes(jenv, c1);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    subCiphersInplace
 * Signature: (J[[B[[B)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_subCiphersInplace(
    JNIEnv *jenv, jclass, jlong jdhandle, jobjectArray jciphers1,
    jobjectArray jciphers2) {
  auto len = jenv->GetArrayLength(jciphers1);
  heu::lib::phe::Ciphertext cs1[len], cs2[len];
  jobjectarray_to_cipher_pairs(jenv, jciphers1, jciphers2, cs1, cs2);
  auto ret = SubCiphersInplace((DestinationHeKitHandle)jdhandle, cs1, cs2, len);
  ciphers_to_jobjectarray(jenv, cs1, jciphers1);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    scatterSubCiphersInplace
 * Signature: (J[[B[[B[J)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_scatterSubCiphersInplace(
    JNIEnv *jenv, jclass, jlong jdhandle, jobjectArray jciphers1,
    jobjectArray jciphers2, jlongArray jindexes, jboolean parallel) {
  auto len1 = jenv->GetArrayLength(jciphers1);
  auto len2 = jenv->GetArrayLength(jciphers2);
  auto indexes = jenv->GetLongArrayElements(jindexes, 0);
  heu::lib::phe::Ciphertext cs1[len1], cs2[len2];
  jobjectarray_to_ciphers(jenv, jciphers1, cs1);
  jobjectarray_to_ciphers(jenv, jciphers2, cs2);
  auto ret = ScatterSubCiphersInplace((DestinationHeKitHandle)jdhandle, cs1,
                                      cs2, indexes, len2, parallel);
  jenv->ReleaseLongArrayElements(jindexes, indexes, 0);
  ciphers_to_jobjectarray(jenv, cs1, jciphers1);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    subCiphersInplaceAxis0
 * Signature: (J[[B[[[B)I
 */
JNIEXPORT jint JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_subCiphersInplaceAxis0(
    JNIEnv *jenv, jclass, jlong jdhandle, jobjectArray jciphers1,
    jobjectArray jciphers2) {
  auto len = jenv->GetArrayLength(jciphers2);
  heu::lib::phe::Ciphertext cs1[len];
  heu::lib::phe::Ciphertext **cs2 = new heu::lib::phe::Ciphertext *[len];
  int row_size[len];
  for (int i = 0; i < len; ++i) {
    auto jba1 = (jbyteArray)jenv->GetObjectArrayElement(jciphers1, i);
    auto joa2 = (jobjectArray)jenv->GetObjectArrayElement(jciphers2, i);
    jbytes_to_cipher(jenv, jba1, cs1[i]);
    row_size[i] = jenv->GetArrayLength(joa2);
    cs2[i] = new heu::lib::phe::Ciphertext[row_size[i]];
    for (int j = 0; j < row_size[i]; ++j) {
      auto jba2 = (jbyteArray)jenv->GetObjectArrayElement(joa2, j);
      jbytes_to_cipher(jenv, jba2, cs2[i][j]);
    }
    jenv->DeleteLocalRef(joa2);
  }
  auto ret = SubCiphersInplaceAxis0_((DestinationHeKitHandle)jdhandle, cs1, cs2,
                                     row_size, len);
  ciphers_to_jobjectarray(jenv, cs1, jciphers1, cs2);
  return ret;
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    multiPlain
 * Signature: (J[BD)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_multiPlain(JNIEnv *jenv, jclass,
                                                       jlong jdhandle,
                                                       jbyteArray jba,
                                                       jdouble jfactor) {
  heu::lib::phe::Ciphertext c;
  jbytes_to_cipher(jenv, jba, c);
  auto res = MultiPlain((DestinationHeKitHandle)jdhandle, c, jfactor);
  return cipher_to_jbytes(jenv, res);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    multiPlainInplace
 * Signature: (J[BD)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_multiPlainInplace(
    JNIEnv *jenv, jclass, jlong jdhandle, jbyteArray jba, jdouble jfactor) {
  heu::lib::phe::Ciphertext c;
  jbytes_to_cipher(jenv, jba, c);
  CheckCall(MultiPlainInplace((DestinationHeKitHandle)jdhandle, c, jfactor),
            "multiPlainInplace");
  return cipher_to_jbytes(jenv, c);
}

/*
 * Class:     com_tencent_angel_fl_crypto_heu_HeuJNI
 * Method:    sumCiphers
 * Signature: (J[[BI)[B
 */
JNIEXPORT jbyteArray JNICALL
Java_com_tencent_angel_fl_crypto_heu_HeuJNI_sumCiphers(JNIEnv *jenv, jclass,
                                                       jlong jdhandle,
                                                       jobjectArray jciphers,
                                                       jint jminWorkSize) {
  auto len = jenv->GetArrayLength(jciphers);
  heu::lib::phe::Ciphertext ciphers[len];
  jobjectarray_to_ciphers(jenv, jciphers, ciphers);
  heu::lib::phe::Ciphertext out;
  SumCiphers((DestinationHeKitHandle)jdhandle, ciphers, len, &out,
             jminWorkSize);
  return cipher_to_jbytes(jenv, out);
}
