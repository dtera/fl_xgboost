//
// Created by HqZhao on 2023/2/26.
//

#include "ssl_util.h"

std::string generate_key(size_t key_len) {
  srand(time(nullptr));
  std::string key = "";
  for (int i = 0; i < key_len; ++i) {
    char ch = rand() % 26 + (rand() % 2 == 0 ? 'a' : 'A');
    key += ch;
  }
  return key;
}

std::string xor_encrypt(const std::string& plain_text, const std::string& key) {
  std::string cipher_text = "";
  int n = plain_text.size();
  int m = key.size();
  for (int i = 0; i < n; ++i) {
    char ch = plain_text[i] ^ key[i % m];
    cipher_text += ch;
  }
  return cipher_text;
}

std::string xor_decrypt(const std::string& cipher_text, const std::string& key) {
  std::string plain_text = "";
  int n = cipher_text.size();
  int m = key.size();
  for (int i = 0; i < n; ++i) {
    char ch = cipher_text[i] ^ key[i % m];
    plain_text += ch;
  }
  return plain_text;
}

/*
void generate_key(unsigned char* key, int key_len) { RAND_bytes(key, key_len); }

std::string evp_encrypt(const std::string& plaintext, unsigned char* key, int key_len,
                        const int block_size) {
  // 初始化加密上下文
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);

  // 分配加密后的密文缓冲区
  unsigned char* ciphertext = new unsigned char[plaintext.size() + block_size];
  int ciphertext_len = 0;

  // 执行加密操作
  EVP_EncryptUpdate(ctx, ciphertext, &ciphertext_len, (unsigned char*)plaintext.c_str(),
                    plaintext.size());

  // 完成加密操作
  int final_len = 0;
  EVP_EncryptFinal_ex(ctx, ciphertext + ciphertext_len, &final_len);

  // 将密文转换为十六进制字符串
  std::string result;
  for (int i = 0; i < ciphertext_len + final_len; i++) {
    char buf[3];
    sprintf(buf, "%02x", ciphertext[i]);
    result += buf;
  }

  // 释放资源
  delete[] ciphertext;
  EVP_CIPHER_CTX_free(ctx);

  return result;
}

std::string evp_decrypt(const std::string& ciphertext, unsigned char* key, int key_len) {
  // 将十六进制字符串转换为密文
  unsigned char* raw_ciphertext = new unsigned char[ciphertext.size() / 2];
  for (int i = 0; i < ciphertext.size() / 2; i++) {
    int val;
    sscanf(ciphertext.substr(i * 2, 2).c_str(), "%02x", &val);
    raw_ciphertext[i] = (unsigned char)val;
  }

  // 初始化解密上下文
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);

  // 分配解密后的明文缓冲区
  unsigned char* plaintext = new unsigned char[ciphertext.size() / 2];
  int plaintext_len = 0;

  // 执行解密操作
  EVP_DecryptUpdate(ctx, plaintext, &plaintext_len, raw_ciphertext, ciphertext.size() / 2);

  // 完成解密操作
  int final_len = 0;
  EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &final_len);

  // 将明文转换为字符串
  std::string result((char*)plaintext, plaintext_len + final_len);

  // 释放资源
  delete[] raw_ciphertext;
  delete[] plaintext;
  EVP_CIPHER_CTX_free(ctx);

  return result;
}
*/
