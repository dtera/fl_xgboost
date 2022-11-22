//
// Created by HqZhao on 2022/11/22.
//

#ifndef FEDXGB_GRPCCOMM_HPP
#define FEDXGB_GRPCCOMM_HPP

#include <grpc++/grpc++.h>

#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>

#include "comm/BaseComm.h"
#include "grpccomm.grpc.pb.h"
#include "xgboost/logging.h"

using grpc::Channel;
using grpc::ClientAsyncReaderWriter;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using grpccomm::MessageRequest;
using grpccomm::MessageResponse;
using grpccomm::MessageService;
using grpccomm::MsgType;

using namespace dmlc;

enum class Type { READ = 1, WRITE = 2, CONNECT = 3, DONE = 4, FINISH = 5 };

template <class T>
class GRPCServer {
 private:
  MessageRequest request_;
  ServerContext context_;
  unique_ptr<ServerCompletionQueue> cq_;
  MessageService::AsyncService service_;
  unique_ptr<Server> server_;
  unique_ptr<ServerAsyncReaderWriter<MessageResponse, MessageRequest>> stream_;
  unique_ptr<thread> grpc_thread_;
  bool is_running_ = true;
  unordered_map<string, shared_ptr<queue<Message<T>>>> msg_buf_;
  function<void(grpccomm::ScalaValue*, const T)>* map_scala_val_;

  void AsyncWaitForRequest() {
    if (is_running_) {
      stream_->Read(&request_, reinterpret_cast<void*>(Type::READ));
    }
  }

  void AsyncSendResponse() {
    MessageResponse response;
    auto msgType = grpccomm::MsgType_Name(request_.msg_type());
    while (msg_buf_.count(msgType) == 0) {
    }  // wait for such message

    shared_ptr<queue<Message<T>>> q = msg_buf_[msgType];
    auto msg = response.mutable_msg();
    while (!q->empty()) {
      auto m = q->front();
      auto v = msg->mutable_list_msg()->add_list_value();
      *map_scala_val_(v->mutable_scala_msg(), m.content);
      q->pop();
    }

    stream_->Write(response, reinterpret_cast<void*>(Type::WRITE));
  }

  void GrpcThread() {
    while (true) {
      void* tag = nullptr;
      bool ok = false;
      if (!cq_->Next(&tag, &ok)) {
        LOG(FATAL) << "Server stream closed.";
        break;
      }
      if (ok) {
        LOG(DEBUG) << endl << "**** Processing completion queue tag " << tag << endl;
        switch (static_cast<Type>(reinterpret_cast<size_t>(tag))) {
          case Type::READ:
            LOG(DEBUG) << "Read a new message." << endl;
            AsyncSendResponse();
            break;
          case Type::WRITE:
            LOG(DEBUG) << "Sending message(async)." << endl;
            AsyncWaitForRequest();
            break;
          case Type::CONNECT:
            LOG(DEBUG) << "Client connected." << endl;
            AsyncWaitForRequest();
            break;
          case Type::DONE:
            LOG(DEBUG) << "Server disconnecting." << endl;
            is_running_ = false;
            break;
          case Type::FINISH:
            LOG(DEBUG) << "Server quit." << endl;
            break;
          default:
            LOG(FATAL) << "Unexpected tag." << tag << endl;
            GPR_ASSERT(false);
        }
      }
    }
  }

 public:
  GRPCServer(const uint32_t port = 50001, const string& host = "0.0.0.0") {
    string server_address = host + ":" + to_string(port);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    stream_.reset(new ServerAsyncReaderWriter<MessageResponse, MessageRequest>(&context_));
    service_.RequestSendMessage(&context_, stream_.get(), cq_.get(), cq_.get(),
                                reinterpret_cast<void*>(Type::CONNECT));

    // This is important as the server should know when the client is done.
    context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(Type::DONE));

    grpc_thread_.reset(new thread((bind(&GRPCServer::GrpcThread, this))));
    cout << "Server listening on " << server_address << endl;
  }

  void setMapScalaValFun(function<void(grpccomm::ScalaValue*, const T)>* f) { map_scala_val_ = f; }

  void close() {
    cout << "Shutting down server...." << endl;
    stream_->Finish(grpc::Status::CANCELLED, reinterpret_cast<void*>(Type::FINISH));
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
    grpc_thread_->join();
  }
};

class GRPCClient {
 public:
  explicit GRPCClient(shared_ptr<Channel> channel) : stub_(MessageService::NewStub(channel)) {
    grpc_thread_.reset(new thread(bind(&GRPCClient::GrpcThread, this)));
    stream_ = stub_->AsyncSendMessage(&context_, &cq_, reinterpret_cast<void*>(Type::CONNECT));
  }

  bool AsyncSendMessage(const string& msgType) {
    if (msgType == "quit") {
      stream_->WritesDone(reinterpret_cast<void*>(Type::DONE));
      return false;
    }

    // TODO: Data we are sending to the server.
    MessageRequest request;

    cout << " ** Sending request: " << msgType << endl;
    stream_->Write(request, reinterpret_cast<void*>(Type::WRITE));
    return true;
  }

  ~GRPCClient() {
    cout << "Shutting down client...." << endl;
    grpc::Status status;
    cq_.Shutdown();
    grpc_thread_->join();
  }

 private:
  void AsyncRequestNextMessage() {
    // TODO:
    stream_->Read(&response_, reinterpret_cast<void*>(Type::READ));
  }

  void GrpcThread() {
    while (true) {
      void* got_tag;
      bool ok = false;
      if (!cq_.Next(&got_tag, &ok)) {
        cerr << "Client stream closed. Quitting" << endl;
        break;
      }
      if (ok) {
        cout << endl << "**** Processing completion queue tag " << got_tag << endl;
        switch (static_cast<Type>(reinterpret_cast<long>(got_tag))) {
          case Type::READ:
            cout << "Read a new message." << endl;
            break;
          case Type::WRITE:
            cout << "Sending message (async)." << endl;
            AsyncRequestNextMessage();
            break;
          case Type::CONNECT:
            cout << "Server connected." << endl;
            break;
          case Type::DONE:
            cout << "Server disconnecting." << endl;
            break;
          case Type::FINISH:
            cout << "Client finish; status = " << (finish_status_.ok() ? "ok" : "cancelled")
                 << endl;
            context_.TryCancel();
            cq_.Shutdown();
            break;
          default:
            cerr << "Unexpected tag " << got_tag << endl;
            GPR_ASSERT(false);
        }
      }
    }
  }

  ClientContext context_;
  CompletionQueue cq_;
  unique_ptr<MessageService::Stub> stub_;
  unique_ptr<ClientAsyncReaderWriter<MessageRequest, MessageResponse>> stream_;
  MessageResponse response_;
  unique_ptr<thread> grpc_thread_;
  grpc::Status finish_status_ = grpc::Status::OK;
};

template <class T>
class GRPCComm : public BaseComm<T> {
 private:
  queue<Message<T>> msg_buf_;

 public:
  GRPCComm(const uint32_t port = 50001, const string& host = "0.0.0.0");
  void send(const Message<T>& msg);
  void reveive(Message<T>* msg);
  void close();
};

template <class T>
GRPCComm<T>::GRPCComm(const uint32_t port, const string& host) {}

template <class T>
void GRPCComm<T>::send(const Message<T>& msg) {
  msg_buf_.push(msg);
}

template <class T>
void GRPCComm<T>::reveive(Message<T>* msg) {}

template <class T>
void GRPCComm<T>::close() {}

#endif  // FEDXGB_GRPCCOMM_HPP
