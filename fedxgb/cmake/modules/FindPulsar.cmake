# Locate Pulsar library and headers
# This module defines the following variables:
#  PULSAR_FOUND        - System has Pulsar libraries and headers
#  PULSAR_INCLUDE_DIRS - The Pulsar include directories
#  PULSAR_LIBRARIES    - The libraries to link against
#  PULSAR_VERSION      - The version of Pulsar found
#
# This module requires the following variables to be set:
#  PULSAR_ROOT_DIR     - Path to Pulsar installation root
#
# Example usage:
#  find_package(Pulsar REQUIRED)
#  include_directories(${PULSAR_INCLUDE_DIRS})
#  target_link_libraries(MyApp ${PULSAR_LIBRARIES})

# Check for required inputs
#[[
include(FetchContent)
FetchContent_Declare(
        Pulsar
        GIT_REPOSITORY https://github.com/apache/pulsar-client-cpp
        GIT_TAG v3.1.2
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(Pulsar)]]
if (NOT PULSAR_ROOT_DIR)
    message(FATAL_ERROR "PULSAR_ROOT_DIR must be set to the Pulsar installation directory")
endif ()

# Check for pulsar library
find_library(PULSAR_LIBRARY NAMES pulsar PATHS ${PULSAR_ROOT_DIR}/lib)

if (PULSAR_LIBRARY)
    set(PULSAR_LIBRARIES ${PULSAR_LIBRARY})
    set(PULSAR_FOUND TRUE)
else ()
    message(FATAL_ERROR "Could not find Pulsar library")
endif ()

# Check for pulsar headers
find_path(PULSAR_INCLUDE_DIRS pulsar/Client.h PATHS ${PULSAR_ROOT_DIR}/include)

if (PULSAR_INCLUDE_DIRS)
    set(PULSAR_FOUND TRUE)
else ()
    message(FATAL_ERROR "Could not find Pulsar headers")
endif ()

# Determine Pulsar version
#[[file(STRINGS ${PULSAR_INCLUDE_DIRS}/pulsar/version.h PULSAR_VERSION_MAJOR REGEX "#define PULSAR_VERSION_MAJOR ([0-9]+)")
file(STRINGS ${PULSAR_INCLUDE_DIRS}/pulsar/version.h PULSAR_VERSION_MINOR REGEX "#define PULSAR_VERSION_MINOR ([0-9]+)")
file(STRINGS ${PULSAR_INCLUDE_DIRS}/pulsar/version.h PULSAR_VERSION_PATCH REGEX "#define PULSAR_VERSION_PATCH ([0-9]+)")

set(PULSAR_VERSION ${PULSAR_VERSION_MAJOR}.${PULSAR_VERSION_MINOR}.${PULSAR_VERSION_PATCH})]]

# Report status
if (PULSAR_FOUND)
    message(STATUS "Found Pulsar library: ${PULSAR_LIBRARY}")
    message(STATUS "Found Pulsar headers: ${PULSAR_INCLUDE_DIRS}")
    message(STATUS "Pulsar version: ${PULSAR_VERSION}")
else ()
    message(FATAL_ERROR "Could not find Pulsar library and headers")
endif ()

# Set output variables
set(PULSAR_LIBRARIES ${PULSAR_LIBRARY})
set(PULSAR_INCLUDE_DIRS ${PULSAR_INCLUDE_DIRS})
set(PULSAR_VERSION ${PULSAR_VERSION})