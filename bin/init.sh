#!/usr/bin/env bash

# =================================================================================================================
# ==============================================Prepare Begin======================================================
# =================================================================================================================
os_release=$(awk -F= '/^ID=/{print $2}' /etc/os-release)

# shellcheck disable=SC2059
printf "Detected os_release\nTrying to install necessary packages...\n"
if [ "$os_release" == "manjaro" ]; then
  pacman -Sy openssl cmake autoconf libtool pkg-config clang
elif [ "$os_release" == "ubuntu" ]; then
  apt-get update && apt-get install -y cmake libssl-dev build-essential autoconf libtool pkg-config libc-ares-dev
else
  yum -y install openssl cmake autoconf libtool pkg-config centos-release-scl devtoolset-7-gcc* \
    tbb tbb-devel \
    gtest-devel gmock-devel gmp-devel # boost boost-devel boost-build
fi
# =================================================================================================================
# ==============================================Prepare End========================================================
# =================================================================================================================

# =================================================================================================================
# ==============================================Install gRPC Begin=================================================
# =================================================================================================================
# grpc@version     protobuf@version
#   1.4.3              3.3.0
#   1.41.0             3.17.3
#   1.54.0             3.21.12
ver=1.41.0
if [ "$1" != "" ] && [ "$1" != "clone" ]; then
  ver="$1"
fi
echo "grpc version: v$ver"

if [ "$1" == "clone" ]; then
  rm -rf grpc && git clone --recurse-submodules -b "v$ver" --depth 1 --shallow-submodules https://github.com/grpc/grpc
fi
cd grpc || exit
#git submodule update --init
prefix=/usr/local/grpc
install_prefix=$prefix-$ver
#rm -rf "$install_prefix"
min_ver=${ver#*\.}
#if [ "$ver" == "1.35.0" ]; then
if [ "${min_ver%\.*}" -gt 35 ]; then
  # wget -q -O cmake-linux.sh https://github.com/Kitware/CMake/releases/download/v3.19.6/cmake-3.19.6-Linux-x86_64.sh
  # sh cmake-linux.sh -- --skip-license --prefix=/usr/local/cmake && rm cmake-linux.sh
  # cmk=/usr/local/cmake/bin/cmake
  # git clone -b "$(curl -L https://grpc.io/release)" https://github.com/grpc/grpc
  rm -rf cmake/build && mkdir -p cmake/build && pushd cmake/build || exit
  cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DgRPC_INSTALL=ON \
    -DBUILD_SHARED_LIBS=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_TESTS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" ../..
  make -j 4 && make install
  popd || exit
  cd third_party/protobuf || exit
  make && make install
  cd - && cd ..
else
  # Install absl
  printf "Trying to install abseil...\n"
  mkdir -p third_party/abseil-cpp/cmake/build && cd third_party/abseil-cpp/cmake/build || exit
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" ../.. && make -j4 install && cd - || exit

  # Install c-ares
  # If the distribution provides a new-enough version of c-ares, this section can be replaced with:
  # apt-get install -y libc-ares-dev
  printf "Trying to install cares...\n"
  mkdir -p third_party/cares/cares/cmake/build && cd third_party/cares/cares/cmake/build || exit
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix" ../.. && make -j4 install && cd - || exit

  # Install protobuf
  printf "Trying to install protobuf...\n"
  mkdir -p third_party/protobuf/cmake/build && cd third_party/protobuf/cmake/build || exit
  cmake -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" .. && make -j4 install && cd - || exit

  # Install re2
  printf "Trying to install re2...\n"
  mkdir -p third_party/re2/cmake/build && cd third_party/re2/cmake/build || exit
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" ../.. && make -j4 install && cd - || exit

  # Install zlib
  printf "Trying to install zlib...\n"
  mkdir -p third_party/zlib/cmake/build && cd third_party/zlib/cmake/build || exit
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix" ../.. && make -j4 install && cd - || exit

  # Install gRPC
  printf "Trying to install gRPC...\n"
  mkdir -p cmake/build && cd cmake/build || exit
  cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_CARES_PROVIDER=package \
    -DgRPC_ABSL_PROVIDER=package \
    -DgRPC_PROTOBUF_PROVIDER=package \
    -DgRPC_RE2_PROVIDER=package \
    -DgRPC_SSL_PROVIDER=package \
    -DgRPC_ZLIB_PROVIDER=package \
    -DCMAKE_INSTALL_PREFIX="$install_prefix"
  ../.. && make -j4 install && cd - || exit
fi
rm -rf $prefix && ln -s "$install_prefix" $prefix
grep -q 'export LD_LIBRARY_PATH' ~/.bashrc || export LD_LIBRARY_PATH="/usr/local/grpc/lib:/usr/local/grpc/lib64:$LD_LIBRARY_PATH"
grep -q 'export LDFLAGS' ~/.bashrc || export LDFLAGS="-Wl,--copy-dt-needed-entries"
grep -q 'export LD_LIBRARY_PATH' ~/.bashrc || echo "export LD_LIBRARY_PATH=/usr/local/grpc/lib:/usr/local/grpc/lib64:$LD_LIBRARY_PATH" >>~/.bashrc
grep -q 'export LDFLAGS' ~/.bashrc || echo "export LDFLAGS=-Wl,--copy-dt-needed-entries" >>~/.bashrc
# =================================================================================================================
# ==============================================Install gRPC End===================================================
# =================================================================================================================

# =================================================================================================================
# ============================================Install Boost Begin==================================================
# =================================================================================================================
#boost_ver=1.53.0
#wget https://github.com/boostorg/boost/archive/refs/tags/boost-${boost_ver}.tar.gz
#tar xvf boost-${boost_ver}.tar.gz && rm -f boost-${boost_ver}.tar.gz
#cd boost-boost-${boost_ver} || exit

#boost_ver=1.81.0
#rm -rf /usr/local/include/boost /usr/local/lib/libboost_* /usr/local/lib/cmake/boost* /usr/local/lib/cmake/Boost*
#wget https://boostorg.jfrog.io/artifactory/main/release/${boost_ver}/source/boost_${boost_ver//./_}.tar.bz2
#tar --bzip2 -xvf boost_${boost_ver//./_}.tar.bz2 && rm -f boost_${boost_ver//./_}.tar.bz2
#cd boost_${boost_ver//./_} || exit
#./bootstrap.sh # --prefix=/usr/local/boost-$boost_ver
#./b2 --build-type=complete --layout=tagged install
#for mt in /usr/local/lib/libboost_*-mt-x64.so; do
#  name=${mt#*libboost_}
#  name=${name%-mt-x64.so}
#  ln -s "${mt}" /usr/lib64/libboost_"${name}"-mt.so
#done
#rm -rf /usr/local/boost && ln -s /usr/local/boost-$boost_ver /usr/local/boost
# =================================================================================================================
# ============================================Install Boost End====================================================
# =================================================================================================================

# =================================================================================================================
# =============================================Install GTest Begin=================================================
# =================================================================================================================
#curl -O -L https://github.com/google/googletest/archive/refs/tags/release-1.10.0.tar.gz &&
#  tar zxf release-1.10.0.tar.gz &&
#  cd googletest-release-1.10.0 &&
#  mkdir build && cd build &&
#  cmake .. && make install && cd ../.. && rm -rf googletest-release-1.10.0
# =================================================================================================================
# =============================================Install GTest End===================================================
# =================================================================================================================

# =================================================================================================================
# ============================================Install Pulsar Begin=================================================
# =================================================================================================================
#pulsar_ver=3.1.2
#for pkg in apache-pulsar-client-${pulsar_ver}-1.x86_64.rpm apache-pulsar-client-devel-${pulsar_ver}-1.x86_64.rpm; do
#  wget https://archive.apache.org/dist/pulsar/pulsar-client-cpp-${pulsar_ver}/rpm-x86_64/x86_64/${pkg}
#  rpm -ivh ${pkg} && rm -f ${pkg}
#done
#pulsar_ver=2.6.3
#pulsar_ver=2.10.3
#for pkg in apache-pulsar-client-${pulsar_ver}-1.x86_64.rpm apache-pulsar-client-devel-${pulsar_ver}-1.x86_64.rpm; do
#  wget https://archive.apache.org/dist/pulsar/pulsar-${pulsar_ver}/RPMS/${pkg};
#  rpm -ivh ${pkg} && rm -f ${pkg};
#done
#pulsar_ver=2.10.4
#git clone -b v$pulsar_ver https://github.com/apache/pulsar.git
#mkdir pulsar/pulsar-client-cpp/build && cd pulsar/pulsar-client-cpp/build || exit
#cmake .. -DBUILD_PYTHON_WRAPPER=OFF -DBUILD_TESTS=ON && make -j8 && make install
#cd ../.. && rm -rf pulsar
# =================================================================================================================
# =============================================Install Pulsar End==================================================
# =================================================================================================================
