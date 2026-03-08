/**
 * @file bench_engines.cpp
 * @brief 撮合引擎 2x2x2 终极消融实验 (Ablation Study)
 * * 引入稳态交织 (Steady-State Interleaved) 压测模型与尾部延迟 (Tail Latency) 统计
 */

#include <benchmark/benchmark.h>
#include "../include/common/allocators.h"
#include "../include/engines/engine_naive.h"
#include "../include/engines/engine_array.h"
#include "../include/engines/engine_naive_intrusive.h"
#include "../include/engines/engine_array_intrusive.h"
#include <vector>
#include <random>
#include <memory>
#include <type_traits>
#include <chrono>
#include <algorithm>

using namespace matching_engine;

// ==========================================
// 1. 实盘数据模拟器 (稳态交织模型)
// ==========================================
struct BenchEvent {
    bool is_cancel;
    uint32_t id; 
    Side side; 
    uint32_t price; 
    uint32_t quantity; 
    uint64_t timestamp;
};

struct BenchmarkStream {
    std::vector<BenchEvent> warmup_orders;   // 预热订单，建立盘口深度
    std::vector<BenchEvent> hot_loop_events; // 跑分核心事件流 (Add/Cancel交织)
};

/**
 * @brief 生成符合 HFT 稳态深度的交织事件流
 * @param steady_depth 盘口保持的存量订单数量
 * @param num_events 测试循环中的事件总数
 */
static BenchmarkStream generate_steady_state_stream(size_t steady_depth, size_t num_events) {
    BenchmarkStream stream;
    stream.warmup_orders.reserve(steady_depth);
    stream.hot_loop_events.reserve(num_events);
    
    std::mt19937 gen(42); 
    std::uniform_int_distribution<uint32_t> side_dist(0, 1);
    std::uniform_int_distribution<uint32_t> price_dist(900, 1100); 
    std::uniform_int_distribution<uint32_t> lot_dist(1, 100);
    std::uniform_int_distribution<uint32_t> action_dist(0, 1); // 0 = Add, 1 = Cancel

    uint32_t current_id = 1;
    std::vector<uint32_t> active_ids; // 记录盘口中的活跃订单，用于随机撤销
    active_ids.reserve(steady_depth + num_events);

    // 1. 建立初始深度 (预热阶段)
    for (size_t i = 0; i < steady_depth; ++i) {
        stream.warmup_orders.push_back({
            false, current_id, static_cast<Side>(side_dist(gen)),
            price_dist(gen) * 100, lot_dist(gen) * 100, static_cast<uint64_t>(1000000 + i)
        });
        active_ids.push_back(current_id);
        current_id++;
    }

    // 2. 生成交织操作 (跑分阶段)
    for (size_t i = 0; i < num_events; ++i) {
        // 为了维持盘口深度，如果 active_ids 不为空且随机数为 1，则生成 Cancel
        if (!active_ids.empty() && action_dist(gen) == 1) {
            // 随机选一个活跃订单撤销 (模拟真实撤单行为)
            std::uniform_int_distribution<size_t> idx_dist(0, active_ids.size() - 1);
            size_t target_idx = idx_dist(gen);
            uint32_t cancel_id = active_ids[target_idx];
            
            // 快速移除选中的 ID
            std::swap(active_ids[target_idx], active_ids.back());
            active_ids.pop_back();

            stream.hot_loop_events.push_back({
                true, cancel_id, Side::BUY, 0, 0, 0 
            });
        } else {
            // 生成 Add 订单
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
// 2. 通用消融实验模板 (包含 Tail Latency 统计)
// ==========================================
template <typename EngineType>
static void BM_Ablation_Study(benchmark::State& state) {
    const size_t steady_depth = 10000;       // 维持 1 万个存量盘口
    const size_t num_events = state.range(0); // 跑分事件数
    auto stream = generate_steady_state_stream(steady_depth, num_events);
    
    std::unique_ptr<EngineType> engine;
    if constexpr (std::is_constructible_v<EngineType, uint32_t, uint32_t, uint32_t>) {
        engine = std::make_unique<EngineType>(90000, 110000, 100);
    } else {
        engine = std::make_unique<EngineType>();
    }

    // 用于统计尾部延迟 (预先分配避免干扰测试)
    std::vector<uint64_t> latencies;
    latencies.resize(num_events);

    // 真正的计时循环
    for (auto _ : state) {
        // 【缓存预热】
        state.PauseTiming();
        // 清理上一次循环遗留的状态（针对连续跑分）
        for (const auto& ev : stream.warmup_orders) engine->cancel_order(ev.id);
        for (const auto& ev : stream.hot_loop_events) engine->cancel_order(ev.id);
        
        // 灌入初始盘口深度
        for (const auto& ev : stream.warmup_orders) {
            engine->add_order(ev.id, ev.side, ev.price, ev.quantity, ev.timestamp);
        }
        state.ResumeTiming();

        // 【极速核心循环】
        size_t l_idx = 0;
        for (const auto& ev : stream.hot_loop_events) {
            auto start = std::chrono::high_resolution_clock::now();
            
            if (ev.is_cancel) {
                engine->cancel_order(ev.id);
            } else {
                engine->add_order(ev.id, ev.side, ev.price, ev.quantity, ev.timestamp);
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            latencies[l_idx++] = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        }
    }
    
    // 吞吐量统计
    state.SetItemsProcessed(state.iterations() * num_events);

    // 计算延迟分布 (仅针对最后一次 Iteration，代表系统的最终稳态)
    std::sort(latencies.begin(), latencies.end());
    state.counters["p50_ns"] = benchmark::Counter(latencies[num_events * 0.50]);
    state.counters["p90_ns"] = benchmark::Counter(latencies[num_events * 0.90]);
    state.counters["p99_ns"] = benchmark::Counter(latencies[num_events * 0.99]);
    state.counters["p99.9_ns"] = benchmark::Counter(latencies[num_events * 0.999]);
}

// =========================================================================
// 3. 注册 2 x 2 x 2 终极消融实验矩阵
// 统一设定：100,000 条跑分交织事件
// =========================================================================

// --- 赛区 1：Map 组 (红黑树 O(logN) 寻址) ---
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineNaive<SystemAllocator<Order>>)->Name("BM_Map_StdLst_SysAlloc")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineNaive<PoolAllocator<Order>>)->Name("BM_Map_StdLst_PoolAlloc")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineNaiveIntrusive<SystemAllocator<Order>>)->Name("BM_Map_Intru_SysAlloc")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineNaiveIntrusive<PoolAllocator<Order>>)->Name("BM_Map_Intru_PoolAlloc")->Arg(100000);

// --- 赛区 2：Array 组 (连续数组 O(1) 寻址) ---
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineArray<SystemAllocator<Order>>)->Name("BM_Arr_StdLst_SysAlloc")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineArray<PoolAllocator<Order>>)->Name("BM_Arr_StdLst_PoolAlloc")->Arg(100000);
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineArrayIntrusive<SystemAllocator<Order>>)->Name("BM_Arr_Intru_SysAlloc")->Arg(100000);
// 👑 终极大满贯形态：绝对连续内存 + 零系统调用 + O(1)算法极限
BENCHMARK_TEMPLATE(BM_Ablation_Study, EngineArrayIntrusive<PoolAllocator<Order>>)->Name("BM_Arr_Intru_PoolAlloc")->Arg(100000);

BENCHMARK_MAIN();