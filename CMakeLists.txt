cmake_minimum_required(VERSION 3.14)
project(fast-matching-engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

include(FetchContent)

# ==========================================
# 1. 引入 Google Benchmark (跑分用)
# ==========================================
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# ==========================================
# 2. 引入 Google Test (测试用)
# ==========================================
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.12.1
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ==========================================
# 3. 编译目标定义
# ==========================================
# 目标 A：极速跑分程序
add_executable(run_benchmarks benchmarks/bench_engines.cpp)
target_link_libraries(run_benchmarks benchmark::benchmark_main)

# 目标 B：正确性测试程序
add_executable(matching_tests tests/test_correctness.cpp)
target_link_libraries(matching_tests GTest::gtest_main)

# ==========================================
# 4. Release 极限优化 (仅对跑分生效)
# ==========================================
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_CONFIGURATION_TYPES MATCHES "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported)
    if(ipo_supported)
        set_property(TARGET run_benchmarks PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()