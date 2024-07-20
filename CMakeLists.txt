# Ringbuffers

cmake_minimum_required(VERSION 3.20)
project(ringbuffers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(ringbuffers_lib INTERFACE)
target_include_directories(ringbuffers_lib INTERFACE "${CMAKE_SOURCE_DIR}/src")

# Test Binary
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
FetchContent_MakeAvailable(googletest)

set (TEST_SRC_CC
    "${CMAKE_SOURCE_DIR}/test/AtomicRingbufferTest.cpp"
    "${CMAKE_SOURCE_DIR}/test/MutexRingbufferTest.cpp"
    )

enable_testing()

add_executable(
  ringbuffers_test 
  ${TEST_SRC_CC}
)

target_link_libraries(ringbuffers_test PRIVATE
  ringbuffers_lib 
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(ringbuffers_test)

# Microbenchmark Binary
# Bring in google benchmark library as a dynamic library
find_package(benchmark REQUIRED)

set (BENCH_SRC_CC
    "${CMAKE_SOURCE_DIR}/bench/RingbufferBenchmark.cpp"
    )

add_executable(
  ringbuffers_bench 
  ${BENCH_SRC_CC}
)

target_link_libraries(ringbuffers_bench PRIVATE
  ringbuffers_lib
  benchmark::benchmark
)
