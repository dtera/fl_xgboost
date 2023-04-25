# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[[
include(FetchContent)
FetchContent_Declare(
        helib
        GIT_REPOSITORY https://github.com/homenc/HElib
        GIT_TAG v2.1.0
        CMAKE_ARGS "-DNTL_GMP_LIP=on -DSHARED=on -DNTL_THREADS=on NTL_THREAD_BOOST=on"
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(helib)]]
