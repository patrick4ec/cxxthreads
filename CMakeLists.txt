cmake_minimum_required(VERSION 3.1)
project(cxxthreads)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -g -O0 -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3 -Wall")


include_directories(include)
include_directories(external)

include_directories(external/GSL/include)


# unit test
enable_testing()

add_subdirectory(external/googletest)

# The gtest/gtest_main targets carry header search path
# dependencies automatically when using CMake 2.8.11 or
# later. Otherwise we have to add them here ourselves.
#if (CMAKE_VERSION VERSION_LESS 2.8.11)
#  include_directories("${gtest_SOURCE_DIR}/include")
#endif()

set(BUILD_GMOCK OFF CACHE BOOL "Builds without the googlemock subproject")
set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of googletest")

add_subdirectory(test)
