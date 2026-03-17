cmake_minimum_required(VERSION 3.14)
project(fast-matching-engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 0. 编译优化与工程化设置 (CI & Sanitizers)
# 使用 Generator Expressions 完美解决 MSVC 冲突
# ==========================================
if(MSVC)
    add_compile_options(/utf-8)
    
    # MSVC Release 极致优化 (开启 AVX2, 内部函数展开, 代码速度优先)
    add_compile_options("$<$<CONFIG:Release>:/O2;/Oi;/Ot;/arch:AVX2;/fp:fast>")
    
    # 暂时关闭 ASan，避免 Git Bash 下找不到 DLL 导致闪退
    add_compile_options("$<$<CONFIG:Debug>:/Zi>")
    add_link_options("$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
else()
    # GCC/Clang Release 极致优化 (指令集全开，关闭严格混叠)
    add_compile_options("$<$<CONFIG:Release>:-O3;-g;-march=native;-fno-strict-aliasing;-ffast-math>")
    
    # GCC/Clang Debug 开启 Address & Undefined Behavior Sanitizer
    add_compile_options("$<$<CONFIG:Debug>:-fsanitize=address,undefined;-g;-O0;-fno-omit-frame-pointer>")
    add_link_options("$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
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
add_executable(run_benchmarks benchmarks/bench_engines.cpp)
target_link_libraries(run_benchmarks benchmark::benchmark_main)

add_executable(matching_tests tests/test_correctness.cpp)
target_link_libraries(matching_tests GTest::gtest_main)

# ==========================================
# 4. Release 链接时优化 (LTO / IPO)
# ==========================================
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    # 仅在 Release 模式下生效 LTO，避免拖慢 Debug 编译速度
    set_property(TARGET run_benchmarks PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET matching_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()