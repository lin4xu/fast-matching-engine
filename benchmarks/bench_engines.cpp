/**
 * @file bench_engines.cpp
 * @brief 撮合引擎 2x2x2 终极消融实验 (Ablation Study)
 * * 本测试框架旨在通过严格控制变量，量化分析三大底层优化维度对高频撮合系统的性能影响：
 * 1. 寻址算法 (Algorithm): std::map (O(logN)) vs 定长数组 (O(1))
 * 2. 节点结构 (Container): std::list (隐式内存分配) vs 侵入式链表 (零内存分配)
 * 3. 内存管理 (Allocator): System new (系统调用) vs Memory Pool (预分配连续内存)
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

using namespace matching_engine;

// ==========================================
// 1. 实盘数据模拟器
// ==========================================
struct OrderData {
    uint32_t id; Side side; uint32_t price; uint32_t quantity; uint64_t timestamp;
};

/**
 * @brief 生成极度逼真的 A 股订单流
 * 采用固定随机种子保证公平性。价格严格限制在 90.00 到 110.00 之间，
 * 符合 A 股 10% 涨跌幅限制的真实物理规律。
 */
static std::vector<OrderData> generate_realistic_orders(size_t num_orders) {
    std::vector<OrderData> orders;
    orders.reserve(num_orders);
    std::mt19937 gen(42); 
    std::uniform_int_distribution<uint32_t> side_dist(0, 1);
    
    // 价格范围：900 到 1100，乘以 Tick Size 100，得到 90000 到 110000 的合法报价
    std::uniform_int_distribution<uint32_t> price_dist(900, 1100); 
    std::uniform_int_distribution<uint32_t> lot_dist(1, 100);

    for (size_t i = 0; i < num_orders; ++i) {
        orders.push_back({
            static_cast<uint32_t>(i + 1), static_cast<Side>(side_dist(gen)),
            price_dist(gen) * 100, lot_dist(gen) * 100, static_cast<uint64_t>(1000000 + i) 
        });
    }
    return orders;
}

// ==========================================
// 2. 通用消融实验模板 (核心跑分逻辑)
// ==========================================
template <typename EngineType>
static void BM_Ablation_Study(benchmark::State& state) {
    const size_t num_orders = state.range(0);
    auto orders = generate_realistic_orders(num_orders);
    
    // 【避坑指南】：引擎的初始化必须在计时循环外部进行！
    // 否则测出的就是 OS 分配大内存的时间，而不是真实撮合性能。
    std::unique_ptr<EngineType> engine;
    
    // 模板元编程：自动探测引擎是否需要价格区间参数 (Array需要，Map不需要)
    if constexpr (std::is_constructible_v<EngineType, uint32_t, uint32_t, uint32_t>) {
        engine = std::make_unique<EngineType>(90000, 110000, 100);
    } else {
        engine = std::make_unique<EngineType>();
    }

    // 【缓存预热 (Cache Warming)】：
    // 先跑一轮完整的报单撤单，把引擎的连续内存块全部“拉”进 CPU 的 L1/L2 缓存。
    for (const auto& o : orders) engine->add_order(o.id, o.side, o.price, o.quantity, o.timestamp);
    for (const auto& o : orders) engine->cancel_order(o.id);

    // 真正的计时循环开始
    for (auto _ : state) {
        // 批量报单
        for (const auto& o : orders) {
            engine->add_order(o.id, o.side, o.price, o.quantity, o.timestamp);
        }
        // 批量撤单 (模拟高频交易中极高的 Cancel/Order 比例，同时复位内存池状态)
        for (const auto& o : orders) {
            engine->cancel_order(o.id);
        }
    }
    
    // 吞吐量统计：每次循环执行了 N 次报单 + N 次撤单 = 2N 次操作
    state.SetItemsProcessed(state.iterations() * num_orders * 2);
}

// =========================================================================
// 3. 注册 2 x 2 x 2 终极消融实验矩阵
// 命名规范: BM_{Map|Arr}_{StdLst|Intru}_{SysAlloc|PoolAlloc}
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