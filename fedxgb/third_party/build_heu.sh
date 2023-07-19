#!/usr/bin/env bash

CD=$(cd "$(dirname "$0")" || exit && pwd)
cd "$CD" || exit
echo "Current Directory: $CD"

# build heu
# [ -d heu ] || https://github.com/dtera/heu.git
pkg=heu
rm -rf "$pkg" && tar xvf "$pkg".tar.gz && "$pkg"/third_party/build.sh
[ -d "$CD"/build/lib ] || mkdir -p "$CD"/build/lib
[ -d "$CD"/build/include ] || mkdir -p "$CD"/build/include
if [ "$(uname)" == "Darwin" ]; then
  cp -R "$CD"/$pkg/third_party/lib/* "$CD"/build/lib/
else
  cp -dR "$CD"/$pkg/third_party/lib/* "$CD"/build/lib/
fi
cp -R "$CD"/$pkg/third_party/include/* "$CD"/build/include/
#cp -R "$CD"/$pkg/include/* "$CD"/build/include/
cd "$CD"/$pkg || exit 0
# shellcheck disable=SC2044
for path in $(find $pkg -name "*.h"); do
  #head_file=${path##*/}
  head_path=${path%/*}
  head_save_path="$CD"/build/include/$head_path
  mkdir -p "$head_save_path" && cp "$path" "$head_save_path"
done

cd "$CD"/$pkg && mkdir build && cd build || exit
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DCMAKE_CXX_STANDARD=17 -DCMAKE_INSTALL_PREFIX="$CD/build/" ..
make -j8 heu

cp "$CD"/$pkg/build/libheu.* "$CD"/build/lib/
cd "$CD" || exit
rm -rf "$pkg"
