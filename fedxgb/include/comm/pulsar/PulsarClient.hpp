//
// Created by HqZhao on 2023/4/18.
//

#pragma once

#include <gmp.h>
#include <google/protobuf/repeated_field.h>
#include <pulsar/Client.h>
#include <pulsar/Consumer.h>
#include <pulsar/Producer.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "comm/grpc/common.h"
#include "xgbcomm.grpc.pb.h"

class PulsarClient {
 public:
  PulsarClient(const std::string& pulsar_url = "pulsar://localhost:6650",
               const std::string& topic_prefix = "federated_xgb_",
               const std::string& pulsar_token = "notoken",
               const std::string& pulsar_tenant = "fl-tenant",
               const std::string& pulsar_namespace = "fl-algorithm")
      : pulsar_url(pulsar_url),
        pulsar_token(pulsar_token),
        pulsar_tenant(pulsar_tenant),
        pulsar_namespace(pulsar_namespace),
        pulsar_topic_prefix("persistent://" + pulsar_tenant + "/" + pulsar_namespace + "/" +
                            topic_prefix) {
    clientConfig.setAuth(pulsar::AuthToken::createWithToken(pulsar_token));
    client = std::make_unique<pulsar::Client>(pulsar_url, clientConfig);

    producerConfig.setBatchingEnabled(true);
    producerConfig.setBatchingMaxMessages(100);
    producerConfig.setBatchingMaxPublishDelayMs(10);
  }

  ~PulsarClient() { client->close(); }

  template <typename T, typename M>
  void send(const std::string& topic, const T& data,
            std::function<void(M*, const T&)> convertObj2PB) {
    try {
      pulsar::Producer producer;
      client->createProducer(pulsar_topic_prefix + topic, producerConfig, producer);

      M pb_msg;
      convertObj2PB(&pb_msg, data);
      std::string serialized;
      pb_msg.SerializeToString(&serialized);
      auto message = pulsar::MessageBuilder().setContent(std::move(serialized)).build();
      producer.send(message);

      producer.close();
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("Failed to send message: ") + ex.what());
    }
  }

  template <typename T, typename M>
  void receive(const std::string& topic, T& data, std::function<void(T&, const M&)> convertPB2Obj,
               const std::string& subscriptionName = "federated_xgb_subscription") {
    try {
      pulsar::Consumer consumer;
      auto consumerConfig = pulsar::ConsumerConfiguration();
      consumerConfig.setSubscriptionInitialPosition(pulsar::InitialPositionEarliest);

      client->subscribe(pulsar_topic_prefix + topic, subscriptionName, consumerConfig, consumer);

      auto message = pulsar::Message();
      consumer.receive(message);

      M pb_msg;
      pb_msg.ParseFromString(message.getDataAsString());
      convertPB2Obj(data, pb_msg);

      consumer.acknowledge(message);
      consumer.close();
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("Failed to receive message: ") + ex.what());
    }
  }

 private:
  std::unique_ptr<pulsar::Client> client;
  pulsar::ClientConfiguration clientConfig;
  pulsar::ProducerConfiguration producerConfig;
  std::string pulsar_url;
  std::string pulsar_topic_prefix;
  std::string pulsar_token;
  std::string pulsar_tenant;
  std::string pulsar_namespace;
};
