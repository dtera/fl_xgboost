//
// Created by HqZhao on 2023/01/11.
//
#include <gtest/gtest.h>

#include "heu/library/phe/encoding/encoding.h"
#include "heu/library/phe/phe.h"

TEST(HEU, BatchEncoding) {
  auto scheme = heu::lib::phe::SchemaType::OU;
  auto he_kit_ = heu::lib::phe::HeKit(scheme, 2048);
  auto edr = he_kit_.GetEncoder<heu::lib::phe::PlainEncoder>(1);

  auto encryptor = he_kit_.GetEncryptor();
  auto evaluator = he_kit_.GetEvaluator();
  auto decryptor = he_kit_.GetDecryptor();
  heu::lib::phe::BatchEncoder batch_encoder(scheme);

  auto m0 = batch_encoder.Encode<int64_t>(-123, 123);
  auto ct0 = encryptor->Encrypt(m0);

  auto res = evaluator->Add(ct0, batch_encoder.Encode<int64_t>(23, 23));
  auto plain = decryptor->Decrypt(res);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 0>(plain)), -123 + 23);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 1>(plain)), 123 + 23);

  res = evaluator->Add(ct0, batch_encoder.Encode<int64_t>(-123, -456));
  decryptor->Decrypt(res, &plain);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 0>(plain)), -123 - 123);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 1>(plain)), 123 - 456);

  res = evaluator->Add(ct0, batch_encoder.Encode<int64_t>(std::numeric_limits<int64_t>::max(),
                                                          std::numeric_limits<int64_t>::max()));
  decryptor->Decrypt(res, &plain);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 0>(plain)),
            -123LL + std::numeric_limits<int64_t>::max());
  EXPECT_EQ((batch_encoder.Decode<int64_t, 1>(plain)),
            std::numeric_limits<int64_t>::lowest() + 122);  // overflow

  // test big number
  ct0 = encryptor->Encrypt(batch_encoder.Encode<int64_t>(std::numeric_limits<int64_t>::lowest(),
                                                         std::numeric_limits<int64_t>::max()));
  res = evaluator->Add(ct0, batch_encoder.Encode<int64_t>(std::numeric_limits<int64_t>::max(),
                                                          std::numeric_limits<int64_t>::max()));
  decryptor->Decrypt(res, &plain);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 0>(plain)), -1);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 1>(plain)), -2);

  res = evaluator->Add(ct0, batch_encoder.Encode<int64_t>(-1, 1));
  decryptor->Decrypt(res, &plain);
  EXPECT_EQ((batch_encoder.Decode<int64_t, 0>(plain)), std::numeric_limits<int64_t>::max());
  EXPECT_EQ((batch_encoder.Decode<int64_t, 1>(plain)), std::numeric_limits<int64_t>::lowest());
}
