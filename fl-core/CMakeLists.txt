cmake_minimum_required(VERSION 3.14)

project(flcore VERSION 0.3 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_LOCAL "${PROJECT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH ${CMAKE_LOCAL}/modules)

# Options
option(USE_OPENMP "Build with OpenMP" ON)

file(GLOB_RECURSE SOURCES *.cc *.h)
file(GLOB RM_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc/*.cc ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/tests.cc)
list(REMOVE_ITEM SOURCES ${RM_SRCS})

# brew install grpc googletest helib libomp protobuf xgboost
find_package(GRPC REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(helib REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Protobuf REQUIRED)
find_package(xgboost REQUIRED)

if (APPLE AND (NOT CMAKE_COMPILER_IS_GNUCC))
    # Require CMake 3.16+ for Mac to ensure that OpenMP can be located
    # (Exception: it's okay if Homebrew GCC is used)
    cmake_minimum_required(VERSION 3.16)
endif ()
find_package(OpenMP REQUIRED)
# For CMake < 3.9, we need to make target OpenMP::OpenMP_CXX ourselves
if (NOT TARGET OpenMP::OpenMP_CXX)
    find_package(Threads REQUIRED)
    add_library(OpenMP::OpenMP_CXX IMPORTED INTERFACE)
    set_property(TARGET OpenMP::OpenMP_CXX
            PROPERTY INTERFACE_COMPILE_OPTIONS ${OpenMP_CXX_FLAGS})
    set_property(TARGET OpenMP::OpenMP_CXX
            PROPERTY INTERFACE_LINK_LIBRARIES ${OpenMP_CXX_FLAGS} Threads::Threads)
endif ()

set(LIB_SUFFIX so)
if (APPLE)
    execute_process(COMMAND brew --prefix openssl
            OUTPUT_VARIABLE HOMEBREW_OPENSSL_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    include_directories(${HOMEBREW_OPENSSL_PREFIX}/include)
    link_directories(${HOMEBREW_OPENSSL_PREFIX}/lib)
    link_libraries(libcrypto.${LIB_SUFFIX} libssl.${LIB_SUFFIX})
endif ()


file(GLOB PROTOS proto/*.proto)
set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto-generated)
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_GENERATED_DIR} ${PROTOS})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_GENERATED_DIR} ${PROTOS})
include_directories(./include ../src ${PROTO_GENERATED_DIR})

add_library(${PROJECT_NAME}_lib)
target_sources(${PROJECT_NAME}_lib PRIVATE ${SOURCES} ${PROTO_SRCS} ${GRPC_SRCS})
target_link_libraries(${PROJECT_NAME}_lib gRPC::grpc++_reflection protobuf::libprotobuf OpenMP::OpenMP_CXX)

# Building grpc server
add_executable(${PROJECT_NAME}_server ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc/greeter_server.cc)
target_link_libraries(${PROJECT_NAME}_server ${PROJECT_NAME}_lib)

# Building grpc client
add_executable(${PROJECT_NAME}_client ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc/greeter_client.cc)
target_link_libraries(${PROJECT_NAME}_client ${PROJECT_NAME}_lib)

# Building tests
add_executable(${PROJECT_NAME}_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests.cc)
target_link_libraries(${PROJECT_NAME}_tests ${PROJECT_NAME}_lib
        helib
        GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
        )
file(GLOB_RECURSE TEST_SOURCES "tests/test_*.cc")
target_sources(${PROJECT_NAME}_tests PRIVATE ${TEST_SOURCES})

set_target_properties(${PROJECT_NAME}_server ${PROJECT_NAME}_client ${PROJECT_NAME}_tests PROPERTIES
        CXX_STANDARD ${CMAKE_CXX_STANDARD}
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
        )
