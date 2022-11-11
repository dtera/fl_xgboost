# Copyright 2018 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# cmake build file for C++ helloworld example.
# Assumes protobuf and gRPC have been installed using cmake.
# See cmake_externalproject/CMakeLists.txt for all-in-one cmake build
# that automatically builds all the dependencies before building helloworld.

cmake_minimum_required(VERSION 3.18)

project(demo C CXX)

set(CMAKE_CXX_STANDARD 17)

# GRPC and Protocol Buffers libraries location
#list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/grpc" "/opt/homebrew/opt/protobuf")

# Cmake find modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Protobuf REQUIRED)
find_package(GRPC REQUIRED)

set(PROTOS
        ${CMAKE_CURRENT_SOURCE_DIR}/helloworld.proto
        )

set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto-src)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})
include_directories(${PROTO_SRC_DIR})

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_SRC_DIR} ${PROTOS})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_SRC_DIR} ${PROTOS})

# Building server
add_executable(${PROJECT_NAME}_server
        ${CMAKE_CURRENT_SOURCE_DIR}/greeter_server.cc
        ${PROTO_SRCS}
        ${GRPC_SRCS}
        )
target_link_libraries(${PROJECT_NAME}_server
        gRPC::grpc++_reflection
        protobuf::libprotobuf
        )

# Building client
add_executable(${PROJECT_NAME}_client
        ${CMAKE_CURRENT_SOURCE_DIR}/greeter_client.cc
        ${PROTO_SRCS}
        ${GRPC_SRCS}
        )
target_link_libraries(${PROJECT_NAME}_client
        gRPC::grpc++_reflection
        protobuf::libprotobuf
        )

set_target_properties(${PROJECT_NAME}_server ${PROJECT_NAME}_client PROPERTIES
        CXX_STANDARD ${CMAKE_CXX_STANDARD}
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
        )