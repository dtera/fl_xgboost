//
// Created by HqZhao on 2023/2/26.
//
#pragma once

// #include <openssl/evp.h>
// #include <openssl/rand.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <random>
#include <string>

std::string generate_key(size_t key_len = 8);

std::string xor_encrypt(const std::string& plain_text, const std::string& key);

std::string xor_decrypt(const std::string& cipher_text, const std::string& key);

/*
void generate_key(unsigned char* key, int key_len);

std::string evp_encrypt(const std::string& plaintext, unsigned char* key, int key_len,
                        const int block_size = 16);

std::string evp_decrypt(const std::string& ciphertext, unsigned char* key, int key_len);
*/
