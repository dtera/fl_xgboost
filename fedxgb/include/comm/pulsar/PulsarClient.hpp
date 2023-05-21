//
// Created by HqZhao on 2023/4/18.
//

#pragma once
#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedValue"

#include <gmp.h>
#include <google/protobuf/repeated_field.h>
#include <pulsar/Client.h>
#include <pulsar/Consumer.h>
#include <pulsar/Producer.h>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "comm/grpc/common.h"
#include "utils.h"
#include "xgbcomm.grpc.pb.h"

class PulsarClient {
 public:
  PulsarClient(const std::string& pulsar_url = "pulsar://localhost:6650",
               const std::string& topic_prefix = "federated_xgb_",
               const std::string& pulsar_token = "notoken",
               const std::string& pulsar_tenant = "fl-tenant",
               const std::string& pulsar_namespace = "fl-algorithm",
               const std::int32_t pulsar_topic_ttl = 60,
               const std::uint32_t pulsar_batch_size = 100,
               const std::uint32_t pulsar_batch_max_size = 1000000,
               const std::int32_t n_threads = omp_get_num_procs())
      : pulsar_url(pulsar_url),
        pulsar_topic_prefix("persistent://" + pulsar_tenant + "/" + pulsar_namespace + "/" +
                            topic_prefix),
        pulsar_token(pulsar_token),
        pulsar_tenant(pulsar_tenant),
        pulsar_namespace(pulsar_namespace),
        pulsar_topic_ttl(pulsar_topic_ttl),
        n_threads(n_threads),
        pulsar_batch_size(pulsar_batch_size) {
    client_config.setAuth(pulsar::AuthToken::createWithToken(pulsar_token));
    client_config.setMemoryLimit(0);
    client = std::make_unique<pulsar::Client>(pulsar_url, client_config);

    producer_config.setBatchingEnabled(false);
    producer_config.setChunkingEnabled(true);
    producer_config.setPartitionsRoutingMode(pulsar::ProducerConfiguration::RoundRobinDistribution);
    producer_config.setCompressionType(pulsar::CompressionLZ4);
    producer_config.setHashingScheme(pulsar::ProducerConfiguration::Murmur3_32Hash);
    producer_config.setLazyStartPartitionedProducers(true);
    producer_config.setProperty("retentionTime", std::to_string(pulsar_topic_ttl * 60 * 1000));

    // Setting the timeout to zero will set the timeout to infinity,
    // which can be useful when using Pulsar's message deduplication feature.
    producer_config.setSendTimeout(0);
    producer_config.setBlockIfQueueFull(true);
    producer_config.setBatchingMaxMessages(pulsar_batch_max_size);
    producer_config.setMaxPendingMessages(pulsar_batch_max_size);
    producer_config.setBatchingMaxAllowedSizeInBytes(0);
    // producer_config.setBatchingMaxPublishDelayMs(10);

    consumer_config.setSubscriptionInitialPosition(pulsar::InitialPositionEarliest);
    consumer_config.setConsumerType(pulsar::ConsumerType::ConsumerExclusive);
    consumer_config.setAutoAckOldestChunkedMessageOnQueueFull(true);
    consumer_config.setMaxPendingChunkedMessage(100);
  }

  ~PulsarClient() { client->close(); }

  template <typename T, typename M>
  void Send(const std::string& topic, const T& data,
            std::function<void(M*, const T&)> convertObj2PB) {
    M pbMsg;
    convertObj2PB(&pbMsg, data);
    Send(topic, pbMsg);
  }

  void Send(const std::string& topic, const google::protobuf::MessageLite& pbMsg) {
    std::string serialized;
    pbMsg.SerializeToString(&serialized);
    Send(topic, serialized);
  }

  void Send(const std::string& topic, const std::string& content) {
    try {
      producer_config.setBatchingEnabled(false);
      producer_config.setChunkingEnabled(true);
      producer_config.setPartitionsRoutingMode(pulsar::ProducerConfiguration::UseSinglePartition);
      pulsar::Producer producer;
      client->createProducer(pulsar_topic_prefix + topic, producer_config, producer);

      auto message = pulsar::MessageBuilder().setContent(std::move(content)).build();
      producer.send(message);

      producer.close();
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("Failed to send message: ") + ex.what());
    }
  }

  template <typename T, typename M>
  void Receive(const std::string& topic, T& data, std::function<void(T&, const M&)> convertPB2Obj,
               const std::string& subscriptionName = "federated_xgb_subscription") {
    M pbMsg;
    Receive(topic, pbMsg, subscriptionName);
    convertPB2Obj(data, pbMsg);
  }

  void Receive(const std::string& topic, google::protobuf::MessageLite& pbMsg,
               const std::string& subscriptionName = "federated_xgb_subscription") {
    std::string content;
    Receive(topic, content, subscriptionName);
    pbMsg.ParseFromString(std::move(content));
  }

  void Receive(const std::string& topic, std::string& content,
               const std::string& subscriptionName = "federated_xgb_subscription") {
    try {
      pulsar::Consumer consumer;
      client->subscribe(pulsar_topic_prefix + topic, subscriptionName, consumer_config, consumer);

      auto message = pulsar::Message();
      consumer.receive(message);
      content = std::move(message.getDataAsString());
      consumer.acknowledge(message);

      consumer.close();
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("Failed to receive message: ") + ex.what());
    }
  }

  template <typename M, typename BM = xgbcomm::Request>
  void BatchSend(const std::string& topic, const google::protobuf::RepeatedPtrField<M>& data,
                 const std::function<M*(BM&)> addBatch = nullptr, const bool waited = false) {
    BatchSend<M, M, BM, google::protobuf::RepeatedPtrField<M>>(
        topic, data, [&](M* m, const M& t) { m->CopyFrom(t); }, addBatch, waited,
        [&](std::size_t i,
            const std::function<void(std::size_t i, const google::protobuf::MessageLite& pbMsg)>
                doSendMsg) { doSendMsg(i, data[i]); });
  }

  template <typename T, typename M, typename BM = xgbcomm::Request, typename R = std::vector<T>>
  void BatchSend(
      const std::string& topic, const R& data,
      const std::function<void(M*, const T&)> convertObj2PB = nullptr,
      const std::function<M*(BM&)> addBatch = nullptr, const bool waited = false,
      const std::function<void(
          std::size_t,
          const std::function<void(std::size_t i, const google::protobuf::MessageLite& pbMsg)>)>
          sendMsg = nullptr) {
    try {
      pulsar::Producer producer;
      producer_config.setBatchingEnabled(true);
      producer_config.setChunkingEnabled(false);
      producer_config.setPartitionsRoutingMode(
          pulsar::ProducerConfiguration::RoundRobinDistribution);
      client->createProducer(pulsar_topic_prefix + topic, producer_config, producer);

      std::atomic<std::uint32_t> msgSize{0};
      auto n = data.size();
      if (sendMsg != nullptr) {
        producer.send(pulsar::MessageBuilder().setContent(std::to_string(n)).build());
      }
      auto doSendMsg = [&](std::size_t i, const google::protobuf::MessageLite& pbMsg) {
        std::string serializedContent;
        pbMsg.SerializeToString(&serializedContent);
        auto message = pulsar::MessageBuilder()
                           .setOrderingKey(std::to_string(i))
                           .setContent(std::move(serializedContent))
                           .build();
        producer.sendAsync(message, [&](pulsar::Result result, const pulsar::MessageId& messageId) {
          msgSize++;
          /*if (msgSize % 10000 == 0) {
            LOG(CONSOLE) << "Message Ack with result: " << result << ",
          messageId: " << messageId
                         << ", msgSize: " << msgSize << std::endl;
          }*/
          /*if (waited && msgSize == data.size()) {
            cv.notify_one();
          }*/
        });
      };
      if (addBatch == nullptr) {
        if (sendMsg == nullptr) {
          ParallelFor(n, n_threads, [&](std::size_t i) {
            M pbMsg;
            convertObj2PB(&pbMsg, data[i]);
            doSendMsg(i, pbMsg);
          });
        } else {
          // ParallelFor(n, n_threads, [&](std::size_t i) { sendMsg(i, doSendMsg); });
          for (std::size_t i = 0; i < n; ++i) {
            sendMsg(i, doSendMsg);
          }
        }
      } else {
        n = n / pulsar_batch_size + (n % pulsar_batch_size == 0 ? 0 : 1);
        if (sendMsg == nullptr) {
          ParallelFor(n, n_threads, [&](std::size_t i) {
            BM bm;
            for (std::size_t j = i * pulsar_batch_size;
                 j < std::min((i + 1) * pulsar_batch_size, (std::size_t)data.size()); ++j) {
              M* pbMsg = addBatch(bm);
              convertObj2PB(pbMsg, data[j]);
            }

            doSendMsg(i, bm);
          });
        } else {
          for (int i = 0; i < n; ++i) {
            BM bm;
            for (int j = i * pulsar_batch_size;
                 j < std::min((i + 1) * pulsar_batch_size, (int)data.size()); ++j) {
              M* pbMsg = addBatch(bm);
              // *pbMsg = data[j];
              convertObj2PB(pbMsg, data[j]);
            }

            doSendMsg(i, bm);
          }
        }
      }

      producer.flush();
      if (waited) {
        // std::unique_lock<std::mutex> lk(mtx);
        while (msgSize < data.size() - 1) {
          // cv.wait(lk);
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
      // LOG(CONSOLE) << "Sent " << msgSize.load() << " messages." << std::endl;
      // producer.close();
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("Failed to send message: ") + ex.what());
    }
  }

  template <typename M, typename BM = xgbcomm::Request>
  void BatchReceive(
      const std::string& topic, google::protobuf::RepeatedPtrField<M>* data,
      const std::function<google::protobuf::RepeatedPtrField<M>(const BM&)> getBatch = nullptr,
      const std::string& subscriptionName = "federated_xgb_batch_subscription") {
    try {
      std::uint32_t messageSize = 0;
      pulsar::Consumer consumer;
      client->subscribe(pulsar_topic_prefix + topic, subscriptionName, consumer_config, consumer);
      pulsar::Message msg;
      consumer.receive(msg);
      auto n = std::stoul(msg.getDataAsString());
      consumer.acknowledge(msg);

      if (getBatch == nullptr) {
        while (messageSize < n) {
          consumer.receive(msg);
          data->Add()->ParseFromString(msg.getDataAsString());
          consumer.acknowledge(msg);
          messageSize++;
        }
      } else {
        n = n / pulsar_batch_size + (n % pulsar_batch_size == 0 ? 0 : 1);
        while (messageSize < n) {
          BM bm;
          consumer.receive(msg);
          bm.ParseFromString(msg.getDataAsString());
          auto batch = getBatch(bm);
          for (int i = 0; i < batch.size(); ++i) {
            data->Add()->CopyFrom(batch[i]);
          }
          consumer.acknowledge(msg);
          messageSize++;
        }
      }
      LOG(CONSOLE) << "Receive " << messageSize << " messages." << std::endl;
      // consumer.close();
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("Failed to receive message: ") + ex.what());
    }
  }

  template <typename T, typename M, typename BM = xgbcomm::Request, typename R = std::vector<T>>
  void BatchReceive(
      const std::string& topic, R& data, const std::function<void(T&, const M&)> convertPB2Obj,
      const std::function<google::protobuf::RepeatedPtrField<M>(const BM&)> getBatch = nullptr,
      const bool waited = true, const bool listened = false,
      const std::string& subscriptionName = "federated_xgb_batch_subscription",
      const std::function<void((const std::string& content, std::uint32_t i))> receiveMsg =
          nullptr) {
    try {
      std::atomic<std::uint32_t> msgSize;
      std::uint32_t messageSize = 0;
      if (listened) {
        consumer_config.setMessageListener([&](pulsar::Consumer c, const pulsar::Message& msg) {
          M pbMsg;
          pbMsg.ParseFromString(msg.getDataAsString());
          T t;
          convertPB2Obj(t, pbMsg);
          data[std::stoul(msg.getOrderingKey())] = std::move(t);
          msgSize++;
          // data.emplace_back(std::move(t));
          c.acknowledgeAsync(msg.getMessageId(), [&](pulsar::Result result) {
            /*if (msgSize % 10000 == 0) {
              LOG(CONSOLE) << "Message Ack with result: " << result << ", msgSize: " << msgSize
                           << std::endl;
            }*/
            /*if (waited && msgSize == data.size()) {
              cv.notify_one();
            }*/
          });
        });
      }
      pulsar::Consumer consumer;
      client->subscribe(pulsar_topic_prefix + topic, subscriptionName, consumer_config, consumer);
      if (!listened) {
        /*pulsar::Messages msgs;
        do {
          consumer.batchReceive(msgs);
          msgSize += msgs.size();
          LOG(CONSOLE) << "Receive " << msgSize.load() << " messages." << std::endl;
          ParallelFor(msgs.size(), omp_get_num_procs(), [&](std::size_t i) {
            M pbMsg;
            pbMsg.ParseFromString(msgs[i].getDataAsString());
            T t;
            convertPB2Obj(t, pbMsg);
            data[std::stoul(msgs[i].getOrderingKey())] = std::move(t);
            consumer.acknowledge(msgs[i]);
          });
        } while (msgSize < data.size() - 1);*/
        /*do {
          consumer.receiveAsync([&](pulsar::Result, const pulsar::Message& msg) {
            M pbMsg;
            pbMsg.ParseFromString(msg.getDataAsString());
            T t;
            convertPB2Obj(t, pbMsg);
            data[std::stoul(msg.getOrderingKey())] = std::move(t);
            consumer.acknowledgeAsync(msg, [&](pulsar::Result result) { msgSize++; });
          });
        } while (msgSize < data.size() - 1);*/
        pulsar::Message msg;
        auto doReceiveMsg = [&](const M& pbMsg, std::uint32_t i) {
          T t;
          convertPB2Obj(t, pbMsg);
          data[i] = std::move(t);
          consumer.acknowledge(msg);
        };
        auto n = data.size();
        if (receiveMsg != nullptr) {
          consumer.receive(msg);
          n = std::stoul(msg.getDataAsString());
          consumer.acknowledge(msg);
        }
        if (getBatch == nullptr) {
          if (receiveMsg == nullptr) {
            while (messageSize < n) {
              consumer.receive(msg);
              M pbMsg;
              pbMsg.ParseFromString(msg.getDataAsString());
              doReceiveMsg(pbMsg, std::stoul(msg.getOrderingKey()));
              messageSize++;
            }
          } else {
            while (messageSize < n) {
              consumer.receive(msg);
              receiveMsg(msg.getDataAsString(), messageSize);
              consumer.acknowledge(msg);
              messageSize++;
            }
          }
        } else {
          n = n / pulsar_batch_size + (n % pulsar_batch_size == 0 ? 0 : 1);
          while (messageSize < n) {
            BM bm;
            consumer.receive(msg);
            bm.ParseFromString(msg.getDataAsString());
            auto batch = getBatch(bm);
            auto offset = std::stoul(msg.getOrderingKey()) * pulsar_batch_size;
            ParallelFor(batch.size(), n_threads, [&](const size_t i) {
              M pbMsg = batch[i];
              if (receiveMsg == nullptr) {
                doReceiveMsg(pbMsg, offset + i);
              } else {
                std::string serializedContent;
                pbMsg.SerializeToString(&serializedContent);
                receiveMsg(serializedContent, offset + i);
              }
            });
            consumer.acknowledge(msg);
            messageSize++;
          }
        }
      } else {
        if (waited) {
          // std::unique_lock<std::mutex> lk(mtx);
          while (msgSize < data.size() - 1) {
            // cv.wait(lk);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        }
        messageSize = msgSize.load();
      }
      LOG(CONSOLE) << "Receive " << messageSize << " messages." << std::endl;
      // consumer.close();
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("Failed to receive message: ") + ex.what());
    }
  }

 private:
  std::unique_ptr<pulsar::Client> client;
  pulsar::ClientConfiguration client_config;
  pulsar::ProducerConfiguration producer_config;
  pulsar::ConsumerConfiguration consumer_config;
  std::string pulsar_url;
  std::string pulsar_topic_prefix;
  std::string pulsar_token;
  std::string pulsar_tenant;
  std::string pulsar_namespace;
  std::int32_t pulsar_topic_ttl;
  int pulsar_batch_size;
  std::mutex mtx{};
  // std::condition_variable cv{};
  std::int32_t n_threads;
};
