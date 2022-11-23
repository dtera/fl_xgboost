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

enum class CommType { READ = 1, WRITE = 2, CONNECT = 3, DONE = 4, FINISH = 5 };

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
  unordered_map<uint32_t, Message<T>> msg_buf_;
  function<void(grpccomm::ListValue*, const T)> map_list_val_;

  void AsyncWaitForRequest() {
    if (is_running_) {
      stream_->Read(&request_, reinterpret_cast<void*>(CommType::READ));
    }
  }

  void AsyncSendResponse() {
    MessageResponse response;
    while (msg_buf_.count(request_.msg_type()) == 0) {
    }  // wait for such message

    Message<T> m = msg_buf_[request_.msg_type()];
    auto msg = response.mutable_msg();
    // map_list_val_(msg->mutable_list_msg(), m.content);
    msg->mutable_scala_msg()->set_float_value(m.content);

    stream_->Write(response, reinterpret_cast<void*>(CommType::WRITE));
  }

  void GrpcThread() {
    while (true) {
      void* tag = nullptr;
      bool ok = false;
      if (!cq_->Next(&tag, &ok)) {
        // LOG(FATAL) << "Server stream closed.";
        break;
      }
      if (ok) {
        LOG(DEBUG) << endl << "**** Processing completion queue tag " << tag << endl;
        switch (static_cast<CommType>(reinterpret_cast<size_t>(tag))) {
          case CommType::READ:
            LOG(DEBUG) << "Read a new message." << endl;
            AsyncSendResponse();
            break;
          case CommType::WRITE:
            LOG(DEBUG) << "Sending message(async)." << endl;
            AsyncWaitForRequest();
            break;
          case CommType::CONNECT:
            LOG(DEBUG) << "Client connected." << endl;
            AsyncWaitForRequest();
            break;
          case CommType::DONE:
            LOG(DEBUG) << "Server disconnecting." << endl;
            is_running_ = false;
            break;
          case CommType::FINISH:
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
                                reinterpret_cast<void*>(CommType::CONNECT));

    // This is important as the server should know when the client is done.
    context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(CommType::DONE));

    grpc_thread_.reset(new thread((bind(&GRPCServer::GrpcThread, this))));
    cout << "Server listening on " << server_address << endl;
  }

  void send(Message<T>& msg) { msg_buf_.insert({msg.msg_type, msg}); }

  void setMapScalaValFun(function<void(grpccomm::ListValue*, const T)>& f) { map_list_val_ = f; }

  bool IsRunning() { return is_running_; }

  void close() {
    cout << "Shutting down server...." << endl;
    // stream_->Finish(grpc::Status::CANCELLED, reinterpret_cast<void*>(CommType::FINISH));
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
    grpc_thread_->join();
  }
};

class GRPCClient {
 private:
  void AsyncRequestNextMessage() {
    stream_->Read(&response_, reinterpret_cast<void*>(CommType::READ));
  }

  void GrpcThread() {
    while (true) {
      void* got_tag;
      bool ok = false;
      if (!cq_.Next(&got_tag, &ok)) {
        // LOG(FATAL) << "Client stream closed." << endl;
        break;
      }
      if (ok) {
        LOG(DEBUG) << endl << "**** Processing completion queue tag " << got_tag << endl;
        switch (static_cast<CommType>(reinterpret_cast<long>(got_tag))) {
          case CommType::READ:
            LOG(DEBUG) << "Read a new message." << endl;
            break;
          case CommType::WRITE:
            LOG(DEBUG) << "Sending message(async)." << endl;
            AsyncRequestNextMessage();
            break;
          case CommType::CONNECT:
            LOG(DEBUG) << "Server connected." << endl;
            break;
          case CommType::DONE:
            LOG(DEBUG) << "Server disconnecting." << endl;
            break;
          case CommType::FINISH:
            LOG(DEBUG) << "Client finish; status = " << (finish_status_.ok() ? "ok" : "cancelled")
                       << endl;
            context_.TryCancel();
            cq_.Shutdown();
            break;
          default:
            LOG(FATAL) << "Unexpected tag " << got_tag << endl;
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

 public:
  explicit GRPCClient(const uint32_t port = 50001, const string& host = "0.0.0.0")
      : stub_(MessageService::NewStub(grpc::CreateChannel(host + ":" + to_string(port),
                                                          grpc::InsecureChannelCredentials()))) {
    grpc_thread_.reset(new thread(bind(&GRPCClient::GrpcThread, this)));
    stream_ = stub_->AsyncSendMessage(&context_, &cq_, reinterpret_cast<void*>(CommType::CONNECT));
  }

  template <class T>
  void receive(Message<T>* msg, function<void(T&, const grpccomm::ListValue&)> map_list_val_) {
    auto list = response_.msg().list_msg();
    auto sm = make_shared<Message<T>>();
    msg = sm.get();
    map_list_val_(msg->content, list);
  }

  bool AsyncSendMessage(const MsgType& msgType) {
    // Data we are sending to the server.
    MessageRequest request;
    request.set_msg_type(msgType);

    LOG(DEBUG) << " ** Sending request: " << grpccomm::MsgType_Name(msgType) << endl;
    stream_->Write(request, reinterpret_cast<void*>(CommType::WRITE));
    return true;
  }

  void close() {
    LOG(DEBUG) << "Shutting down client...." << endl;
    stream_->WritesDone(reinterpret_cast<void*>(CommType::DONE));
    grpc::Status status;
    cq_.Shutdown();
    grpc_thread_->join();
  }
};

template <class T>
class GRPCComm : public BaseComm<T> {
 private:
 public:
  GRPCComm(const uint32_t port = 50001, const string& host = "0.0.0.0");
  void send(const Message<T>& msg);
  void receive(Message<T>* msg);
  void close();
};

template <class T>
GRPCComm<T>::GRPCComm(const uint32_t port, const string& host) {}

template <class T>
void GRPCComm<T>::send(const Message<T>& msg) {}

template <class T>
void GRPCComm<T>::receive(Message<T>* msg) {}

template <class T>
void GRPCComm<T>::close() {}

#endif  // FEDXGB_GRPCCOMM_HPP
