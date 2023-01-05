//
// Created by HqZhao on 2023/01/05.
//
#include <gtest/gtest.h>
#include <openssl/aes.h>
#include <openssl/modes.h>
#include <openssl/rand.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;

static void hex_print(const void* pv, size_t len) {
  const unsigned char* p = (const unsigned char*)pv;
  if (NULL == pv)
    printf("NULL");
  else {
    size_t i = 0;
    for (; i < len; ++i) printf("%02X ", *p++);
  }
  printf("\n");
}

TEST(openssl, aes) {
  int keylength = 256;
  // printf("Give a key length [only 128 or 192 or 256!]:\n");
  // scanf("%d", &keylength);

  /* generate a key with a given length */
  unsigned char aes_key[keylength / 8];
  memset(aes_key, 0, keylength / 8);
  if (!RAND_bytes(aes_key, keylength / 8)) exit(-1);

  size_t inputslength = 128;
  printf("Give an input's length:\n");
  //scanf("%lu", &inputslength);

  /* generate input with a given length */
  unsigned char aes_input[inputslength];
  memset(aes_input, 'X', inputslength);

  const size_t ivsize = AES_BLOCK_SIZE * 2;
  /* init vector */
  unsigned char iv_enc[ivsize], iv_dec[ivsize];
  RAND_bytes(iv_enc, ivsize);
  memcpy(iv_dec, iv_enc, ivsize);

  // buffers for encryption and decryption
  const size_t encslength = ((inputslength + AES_BLOCK_SIZE) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
  unsigned char enc_out[encslength];
  unsigned char dec_out[inputslength];
  memset(enc_out, 0, sizeof(enc_out));
  memset(dec_out, 0, sizeof(dec_out));

  // so i can do with this aes-cbc-128 aes-cbc-192 aes-cbc-256
  AES_KEY enc_key, dec_key;
  AES_set_encrypt_key(aes_key, keylength, &enc_key);
  AES_ige_encrypt(aes_input, enc_out, encslength, &enc_key, iv_enc, AES_ENCRYPT);

  AES_set_decrypt_key(aes_key, keylength, &dec_key);
  AES_ige_encrypt(enc_out, dec_out, encslength, &dec_key, iv_dec, AES_DECRYPT);

  printf("original:\t");
  hex_print(aes_input, sizeof(aes_input));

  printf("encrypt:\t");
  hex_print(enc_out, sizeof(enc_out));

  printf("decrypt:\t");
  hex_print(dec_out, sizeof(dec_out));
}
