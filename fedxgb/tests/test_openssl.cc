//
// Created by HqZhao on 2023/01/05.
//
#include <gtest/gtest.h>

#include "ssl_util.h"

using namespace std;

TEST(openssl, xor_) {
  string plain_text = "Hello World!";
  string key = generate_key();
  string cipher_text = xor_encrypt(plain_text, key);
  string decrypted_text = xor_decrypt(cipher_text, key);
  cout << "Plain Text: " << plain_text << endl;
  cout << "Key: " << key << endl;
  cout << "Cipher Text: " << cipher_text << endl;
  cout << "Decrypted Text: " << decrypted_text << endl;
}

TEST(openssl, evp_) {
  string plain_text = "Hello World!";
  // 随机生成 128 位密钥
  const int KEY_LEN = 16;
  unsigned char key[KEY_LEN];
  /*generate_key(key, KEY_LEN);
  string cipher_text = evp_encrypt(plain_text, key, KEY_LEN);
  string decrypted_text = evp_decrypt(cipher_text, key, KEY_LEN);
  cout << "Plain Text: " << plain_text << endl;
  cout << "Key: " << key << endl;
  cout << "Cipher Text: " << cipher_text << endl;
  cout << "Decrypted Text: " << decrypted_text << endl;*/
}
