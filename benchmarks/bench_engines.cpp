/**
 * @file bench_engines.cpp
 * @brief 撮合引擎 3x3 终极消融实验 (Ablation Study) - 修正了硬件 TSC 计时器
 */

#include <benchmark/benchmark.h>
#include "../include/common/allocators.h"
#include "../include/engines/engine_naive.h"
#include "../include/engines/engine_naive_intrusive.h"
#include "../include/engines/engine_array.h"
#include "../include/engines/engine_array_intrusive.h"
#include "../include/engines/engine_bitset.h"
#include "../include/engines/engine_bitset_intrusive.h"
#include <vector>
#include <random>
#include <memory>
#include <type_traits>
#include <chrono>
#include <algorithm>

// ==========================================
// 硬件 TSC 计时器 (解决高精度测量墙问题)
// ==========================================
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

// 获取带有管线序列化屏障的 CPU 时钟周期
inline uint64_t get_cpu_cycles() {
    unsigned int aux;
    return __rdtscp(&aux);
}

// 动态估算当前 CPU 的每周期纳秒数 (ns per cycle)
static double get_nanoseconds_per_cycle() {
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t start_cycles = get_cpu_cycles();
    
    // Busy wait 约 10 毫秒用于采样校准
    volatile uint64_t sum = 0;
    for(int i = 0; i < 5000000; ++i) sum += i; 
    
    uint64_t end_cycles = get_cpu_cycles();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    return static_cast<double>(elapsed_ns) / (end_cycles - start_cycles);
}

using namespace matching_engine;

// ==========================================
// 1. 实盘数据模拟器 (保持原样)
// ==========================================
struct BenchEvent {
    bool is_cancel; uint32_t id; Side side; uint32_t price; uint32_t quantity; uint64_t timestamp;
};

struct BenchmarkStream {
    std::vector<BenchEvent> warmup_orders;
    std::vector<BenchEvent> hot_loop_events;
};

static BenchmarkStream generate_steady_state_stream(size_t steady_depth, size_t num_events) {
    BenchmarkStream stream;
    stream.warmup_orders.reserve(steady_depth);
    stream.hot_loop_events.reserve(num_events);
    
    std::mt19937 gen(42); 
    std::uniform_int_distribution<uint32_t> side_dist(0, 1);
    std::uniform_int_distribution<uint32_t> price_dist(900, 1100); 
    std::uniform_int_distribution<uint32_t> lot_dist(1, 100);
    std::uniform_int_distribution<uint32_t> action_dist(0, 1);

    uint32_t current_id = 1;
    std::vector<uint32_t> active_ids; 
    active_ids.reserve(steady_depth + num_events);

    for (size_t i = 0; i < steady_depth; ++i) {
        stream.warmup_orders.push_back({
            false, current_id, static_cast<Side>(side_dist(gen)),
            price_dist(gen) * 100, lot_dist(gen) * 100, static_cast<uint64_t>(1000000 + i)
        });
        active_ids.push_back(current_id);
        current_id++;
    }

    for (size_t i = 0; i < num_events; ++i) {
        if (!active_ids.empty() && action_dist(gen) == 1) {
            std::uniform_int_distribution<size_t> idx_dist(0, active_ids.size() - 1);
            size_t target_idx = idx_dist(gen);
            uint32_t cancel_id = active_ids[target_idx];
            
            std::swap(active_ids[target_idx], active_ids.back());
            active_ids.pop_back();

            stream.hot_loop_events.push_back({ true, cancel_id, Side::BUY, 0, 0, 0 });
        } else {
            stream.hot_loop_events.push_back({
                false, current_id, static_cast<Side>(side_dist(gen)),
                price_dist(gen) * 100, lot_dist(gen) * 100, static_cast<uint64_t>(1000000 + steady_depth + i)
            });
            active_ids.push_back(current_id);
            current_id++;
        }
    }
    return stream;
}

// ==========================================
// 2. 通用消融实验模板 (修正延迟测量机制)
// ==========================================
template <typename EngineType>
static void BM_Ablation_Study(benchmark::State& state) {
    // 预先计算转换率，避免在跑分中引入性能波动
    static double ns_per_cycle = get_nanoseconds_per_cycle();

    const size_t steady_depth = 10000;
    const size_t num_events = state.range(0);
    auto stream = generate_steady_state_stream(steady_depth, num_events);
    
    std::unique_ptr<EngineType> engine;
    
    if constexpr (std::is_constructible_v<EngineType, uint32_t, uint32_t, uint32_t>) {
        engine = std::make_unique<EngineType>(90000, 110000, 100);
    } else {
        engine = std::make_unique<EngineType>();
    }

    // 记录的是 Cycles 而不是 ns，消除浮点转换开销
    std::vector<uint64_t> latencies_cycles;
    latencies_cycles.resize(num_events);

    for (auto _ : state) {
        state.PauseTiming();
        for (const auto& ev : stream.warmup_orders) engine->cancel_order(ev.id);
        for (const auto& ev : stream.hot_loop_events) engine->cancel_order(ev.id);
        
        for (const auto& ev : stream.warmup_orders) {
            engine->add_order(ev.id, ev.side, ev.price, ev.quantity, ev.timestamp);
        }
        state.ResumeTiming();

        size_t l_idx = 0;
        for (const auto& ev : stream.hot_loop_events) {
            // 【核心修正】：使用硬件周期获取最纯净的操作开销
            uint64_t start_cycle = get_cpu_cycles();
            
            if (ev.is_cancel) {
                engine->cancel_order(ev.id);
            } else {
                engine->add_order(ev.id, ev.side, ev.price, ev.quantity, ev.timestamp);
            }
            
            uint64_t end_cycle = get_cpu_cycles();
            latencies_cycles[l_idx++] = (end_cycle - start_cycle);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * num_events);

    // 计算延迟分布 (将 Cycle 转换为 ns)
    std::sort(latencies_cycles.begin(), latencies_cycles.end());
    state.counters["p50_ns"]   = benchmark::Counter(latencies_cycles[static_cast<size_t>(num_events * 0.50)] * ns_per_cycle);
    state.counters["p90_ns"]   = benchmark::Counter(latencies_cycles[static_cast<size_t>(num_events * 0.90)] * ns_per_cycle);
    state.counters["p99_ns"]   = benchmark::Counter(latencies_cycles[static_cast<size_t>(num_events * 0.99)] * ns_per_cycle);
    state.counters["p99.9_ns"] = benchmark::Counter(latencies_cycles[static_cast<size_t>(num_events * 0.999)] * ns_per_cycle);
}

// =========================================================================
// 3. 注册矩阵
// =========================================================================
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineNaive<SystemAllocator<Order>>)->Name("BM_Map_SysAlloc_StdLst")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineNaive<PoolAllocator<Order>>)->Name("BM_Map_PoolAlloc_StdLst")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineNaiveIntrusive<PoolAllocator<Order>>)->Name("BM_Map_PoolAlloc_Intru")->Arg(100000);

BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineArray<SystemAllocator<Order>>)->Name("BM_Arr_SysAlloc_StdLst")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineArray<PoolAllocator<Order>>)->Name("BM_Arr_PoolAlloc_StdLst")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineArrayIntrusive<PoolAllocator<Order>>)->Name("BM_Arr_PoolAlloc_Intru")->Arg(100000);

BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineBitset<SystemAllocator<Order>>)->Name("BM_Bit_SysAlloc_StdLst")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineBitset<PoolAllocator<Order>>)->Name("BM_Bit_PoolAlloc_StdLst")->Arg(100000);

BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineBitsetIntrusive<PoolAllocator<Order>>)->Name("BM_Bit_PoolAlloc_Intru")->Arg(100000);

BENCHMARK_MAIN();