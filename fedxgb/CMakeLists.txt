cmake_minimum_required(VERSION 3.14)

project(fedxgb VERSION 0.3 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH ${CMAKE_LOCAL}/modules)
#include(cmake/common.cmake)

# Options
option(USE_OPENMP "Build with OpenMP" ON)
option(GRPC_FETCHCONTENT "GRPC FETCH CONTENT" ON)
option(CMAKE_CROSSCOMPILING "CMAKE CROSS COMPILING" ON)

file(GLOB_RECURSE SOURCES *.cpp *.cc *.h *.hpp)
file(GLOB RM_SRCS src/comm/grpc/demo/*.cc ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/tests.cc)
list(REMOVE_ITEM SOURCES ${RM_SRCS})

# external project
#include(ExternalProject)
#set(TBB_PREFIX_DIR "${CMAKE_CURRENT_BINARY_DIR}/tbb")
#set(TBB_CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX=${TBB_PREFIX_DIR}")
#ExternalProject_Add(tbb
#        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
#        GIT_TAG v2021.8.0
#        GIT_SUBMODULES_RECURSE ON
#        GIT_REMOTE_UPDATE_STRATEGY CHECKOUT
#        PREFIX ${TBB_PREFIX_DIR}
#        CMAKE_ARGS ${TBB_CMAKE_ARGS}
#        )

# brew install grpc googletest helib libomp protobuf xgboost tbb
#if (APPLE)
#    execute_process(COMMAND brew install grpc googletest helib libomp protobuf xgboost tbb)
#endif ()
foreach (pkg Boost:COMPONENTS,system,filesystem,thread GRPC GTest:CONFIG helib OpenMP Opt@OpenSSL Protobuf TBB Threads)
    string(REGEX REPLACE "(:(.*)?)|(Opt@)" "" pkg_name ${pkg})
    string(REGEX MATCH ":(.*)?$" f1 ${pkg})
    string(FIND ${pkg} "Opt@" f2)
    set(c "")
    set(r "")
    if (NOT ${f1} STREQUAL "")
        string(REGEX REPLACE "(.*)?:" "" c ${pkg})
        string(REGEX REPLACE "," ";" c ${c})
    endif ()
    if (${f2} STREQUAL -1)
        set(r "REQUIRED")
    endif ()
    find_package(${pkg_name} ${c} ${r})
endforeach ()

if (APPLE AND (NOT CMAKE_COMPILER_IS_GNUCC))
    # Require CMake 3.16+ for Mac to ensure that OpenMP can be located
    # (Exception: it's okay if Homebrew GCC is used)
    cmake_minimum_required(VERSION 3.16)
endif ()
# For CMake < 3.9, we need to make target OpenMP::OpenMP_CXX ourselves
if (NOT TARGET OpenMP::OpenMP_CXX)
    add_library(OpenMP::OpenMP_CXX IMPORTED INTERFACE)
    set_property(TARGET OpenMP::OpenMP_CXX
            PROPERTY INTERFACE_COMPILE_OPTIONS ${OpenMP_CXX_FLAGS})
    set_property(TARGET OpenMP::OpenMP_CXX
            PROPERTY INTERFACE_LINK_LIBRARIES ${OpenMP_CXX_FLAGS} Threads::Threads)
endif ()

# link the related libraries for mac
set(LIB_SUFFIX so)
if (APPLE)
    set(LIB_SUFFIX dylib)
    foreach (l openssl:libcrypto,libssl grpc:libgpr)
        string(REGEX REPLACE "(.*)?:" "" libs ${l})
        string(REGEX REPLACE "," ";" libs ${libs})
        string(REGEX REPLACE ":(.*)?" "" ln ${l})
        execute_process(COMMAND brew --prefix ${ln} OUTPUT_VARIABLE BREW_${ln}_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
        include_directories(${BREW_${ln}_PREFIX}/include)
        link_directories(${BREW_${ln}_PREFIX}/lib)
        foreach (lib ${libs})
            link_libraries(${lib}.${LIB_SUFFIX})
        endforeach ()
    endforeach ()
endif ()

# config auto generated for protobuf
file(GLOB PROTOS proto/*.proto)
set(PROTO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto-generated)
file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_GENERATED_DIR} ${PROTOS})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_GENERATED_DIR} ${PROTOS})

# include directories and link libraries
include_directories(./include ./src ../src ${PROTO_GENERATED_DIR}  # ${TBB_PREFIX_DIR}/include
        ${xgboost_SOURCE_DIR}/include ${xgboost_SOURCE_DIR}/dmlc-core/include ${xgboost_SOURCE_DIR}/rabit/include)
#link_directories(${TBB_PREFIX_DIR}/lib)
#link_libraries(libtbb.${LIB_SUFFIX})

# link libraries for the target
add_library(${PROJECT_NAME}_lib)
target_sources(${PROJECT_NAME}_lib PRIVATE ${SOURCES} ${PROTO_SRCS} ${GRPC_SRCS})
target_link_libraries(${PROJECT_NAME}_lib ${Boost_LIBRARIES} protobuf::libprotobuf protobuf::libprotobuf-lite
        OpenMP::OpenMP_CXX gRPC::grpc gRPC::grpc++ gRPC::grpc++_reflection helib TBB::tbb)
#add_dependencies(${PROJECT_NAME}_lib tbb)

set(EXE_TARGETS)

# Building grpc server
file(GLOB GRPC_SERVER_FILES src/comm/grpc/demo/*_server*.cc)
foreach (grpc_server_file ${GRPC_SERVER_FILES})
    string(REPLACE ".cc" "" server_name ${grpc_server_file})
    string(REGEX REPLACE "(.*)?/" "" server_name ${server_name})
    add_executable(${PROJECT_NAME}_${server_name} ${grpc_server_file})
    target_link_libraries(${PROJECT_NAME}_${server_name} ${PROJECT_NAME}_lib)
    list(APPEND EXE_TARGETS ${PROJECT_NAME}_${server_name})
endforeach ()

# Building grpc client
file(GLOB GRPC_CLIENT_FILES src/comm/grpc/demo/*_client*.cc)
foreach (grpc_client_file ${GRPC_CLIENT_FILES})
    string(REPLACE ".cc" "" client_name ${grpc_client_file})
    string(REGEX REPLACE "(.*)?/" "" client_name ${client_name})
    add_executable(${PROJECT_NAME}_${client_name} ${grpc_client_file})
    target_link_libraries(${PROJECT_NAME}_${client_name} ${PROJECT_NAME}_lib)
    list(APPEND EXE_TARGETS ${PROJECT_NAME}_${client_name})
endforeach ()

# Building xgb client
file(GLOB XGB_CLIENT_FILES src/*main.cc)
foreach (xgb_client_file ${XGB_CLIENT_FILES})
    string(REPLACE ".cc" "" client_name ${xgb_client_file})
    string(REGEX REPLACE "(.*)?/" "" client_name ${xgb_client_file})
    add_executable(${PROJECT_NAME}_${client_name} ${xgb_client_file})
    target_link_libraries(${PROJECT_NAME}_${client_name} ${PROJECT_NAME}_lib objxgboost)
    list(APPEND EXE_TARGETS ${PROJECT_NAME}_${client_name})
endforeach ()

# Building tests
add_executable(${PROJECT_NAME}_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests.cc)
target_link_libraries(${PROJECT_NAME}_tests ${PROJECT_NAME}_lib
        objxgboost
        GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
        )
file(GLOB_RECURSE TEST_SOURCES "tests/test_*.cc")
target_sources(${PROJECT_NAME}_tests PRIVATE ${TEST_SOURCES})
list(APPEND EXE_TARGETS ${PROJECT_NAME}_tests)

foreach (target ${EXE_TARGETS})
    message("target: ${target}")
endforeach ()
set_target_properties(${EXE_TARGETS} PROPERTIES
        CXX_STANDARD ${CMAKE_CXX_STANDARD}
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
        )
