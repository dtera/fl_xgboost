# Copyright 2022 fl_xgboost authors.
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

cmake_minimum_required(VERSION 3.18)

project(demo C CXX)

set(CMAKE_CXX_STANDARD 17)

set(LIB_SUFFIX so)
set(LOCAL_OPT_DIR /usr/local/opt)
set(BREW_OPT_DIR /opt/homebrew/opt)
set(INC_DIR . ./include ../../include ../../src /usr/include /usr/local/include /opt/homebrew/include
        ${LOCAL_OPT_DIR}/openssl/include ${BREW_OPT_DIR}/openssl/include)
set(LINK_DIR ./lib /usr/lib /usr/local/lib /opt/homebrew/lib ${LOCAL_OPT_DIR}/openssl/lib ${BREW_OPT_DIR}/openssl/lib)
set(LINK_LIB libcrypto.${LIB_SUFFIX} libssl.${LIB_SUFFIX} libomp.${LIB_SUFFIX})

include_directories(${INC_DIR})
link_directories(${LINK_DIR})
link_libraries(${LINK_LIB})

# GRPC and Protocol Buffers libraries location
#list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/grpc" "/opt/homebrew/opt/protobuf")

# Cmake find modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# brew install grpc googletest helib libomp protobuf xgboost
find_package(GRPC REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(helib REQUIRED)
find_package(OpenMP REQUIRED)
if (OPENMP_FOUND)
    message("OpenMP Found")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    add_definitions(-DWITH_OPENMP)
endif ()
find_package(Protobuf REQUIRED)
find_package(xgboost REQUIRED)

file(GLOB PROTOS *.proto)

set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto-src)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})
include_directories(${PROTO_SRC_DIR})

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_SRC_DIR} ${PROTOS})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_SRC_DIR} ${PROTOS})

file(GLOB ${PROJECT_NAME}-src src/*.cpp src/*.cc ../../src/fl/*.cc ${PROTO_SRCS} ${GRPC_SRCS})

# Building grpc server
add_executable(${PROJECT_NAME}_server
        ${CMAKE_CURRENT_SOURCE_DIR}/greeter_server.cc
        ${${PROJECT_NAME}-src}
        )
target_link_libraries(${PROJECT_NAME}_server
        gRPC::grpc++_reflection
        protobuf::libprotobuf
        )

# Building grpc client
add_executable(${PROJECT_NAME}_client
        ${CMAKE_CURRENT_SOURCE_DIR}/greeter_client.cc
        ${${PROJECT_NAME}-src})
target_link_libraries(${PROJECT_NAME}_client
        gRPC::grpc++_reflection
        protobuf::libprotobuf
        )

# Building tests
add_executable(${PROJECT_NAME}_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests.cc
        ${${PROJECT_NAME}-src})
target_link_libraries(${PROJECT_NAME}_tests
        gRPC::grpc++_reflection
        protobuf::libprotobuf
        helib
        xgboost
        GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
        )
file(GLOB_RECURSE TEST_SOURCES "tests/test_*.cc")
target_sources(${PROJECT_NAME}_tests PRIVATE ${TEST_SOURCES})

set_target_properties(${PROJECT_NAME}_server ${PROJECT_NAME}_client ${PROJECT_NAME}_tests PROPERTIES
        CXX_STANDARD ${CMAKE_CXX_STANDARD}
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
        )
