#include <gtest/gtest.h>
#include "../include/common/allocators.h"
#include "../include/engines/engine_naive.h"
#include "../include/engines/engine_array.h"
#include "../include/engines/engine_naive_intrusive.h"
#include "../include/engines/engine_array_intrusive.h"
#include <vector>
#include <random>
#include <memory>
#include <iostream>

using namespace matching_engine;

// ==========================================
// 1. 定义用于对比的“成交记录”快照 (名称已完全对齐真实的 Trade 结构体)
// ==========================================
struct TradeRecord {
    uint32_t maker_order_id;
    uint32_t taker_order_id;
    uint32_t price;
    uint32_t quantity;

    // 重载 == 运算符，让 GTest 知道如何判断两笔成交是否绝对一样
    bool operator==(const TradeRecord& other) const {
        return maker_order_id == other.maker_order_id &&
               taker_order_id == other.taker_order_id &&
               price == other.price &&
               quantity == other.quantity;
    }
};

// 模拟实盘订单数据结构
struct TestOrder {
    uint32_t id; Side side; uint32_t price; uint32_t quantity; uint64_t timestamp; bool is_cancel;
};

// ==========================================
// 2. 生成包含“报单”和“撤单”的确定性测试流
// ==========================================
std::vector<TestOrder> generate_deterministic_stream(size_t num_events) {
    std::vector<TestOrder> stream;
    stream.reserve(num_events);
    std::mt19937 gen(888); // 固定种子 888，保证每次生成的订单流绝对一致
    std::uniform_int_distribution<uint32_t> side_dist(0, 1);
    std::uniform_int_distribution<uint32_t> price_dist(900, 1100); 
    std::uniform_int_distribution<uint32_t> lot_dist(1, 100);
    std::uniform_int_distribution<uint32_t> action_dist(1, 10); // 10% 概率是撤单

    uint32_t current_id = 1;
    for (size_t i = 0; i < num_events; ++i) {
        bool is_cancel = (action_dist(gen) == 1) && (current_id > 1);
        if (is_cancel) {
            // 随机撤销之前的一个订单
            std::uniform_int_distribution<uint32_t> cancel_id_dist(1, current_id - 1);
            stream.push_back({cancel_id_dist(gen), Side::BUY, 0, 0, 0, true});
        } else {
            stream.push_back({
                current_id++, static_cast<Side>(side_dist(gen)),
                price_dist(gen) * 100, lot_dist(gen) * 100, static_cast<uint64_t>(1000000 + i), false
            });
        }
    }
    return stream;
}

// ==========================================
// 3. 核心收集函数：运行引擎并录制所有成交
// ==========================================
template <typename EngineType>
std::vector<TradeRecord> run_and_collect_trades(const std::vector<TestOrder>& stream) {
    std::vector<TradeRecord> trades;
    
    std::unique_ptr<EngineType> engine;
    if constexpr (std::is_constructible_v<EngineType, uint32_t, uint32_t, uint32_t>) {
        engine = std::make_unique<EngineType>(90000, 110000, 100);
    } else {
        engine = std::make_unique<EngineType>();
    }

    engine->set_trade_callback([&trades](const Trade& info) {
        trades.push_back({
            info.maker_order_id, 
            info.taker_order_id, 
            info.price, 
            info.quantity
        });
    });

    // 顺序回放所有订单
    for (const auto& event : stream) {
        if (event.is_cancel) {
            engine->cancel_order(event.id);
        } else {
            engine->add_order(event.id, event.side, event.price, event.quantity, event.timestamp);
        }
    }
    return trades;
}

// ==========================================
// 4. 定义 GTest 自动化对比测试用例
// ==========================================
TEST(EngineConsistencyTest, AblationEnginesMatchBaseline) {
    // 生成 5万 条确定的测试事件
    auto test_stream = generate_deterministic_stream(50000);

    // 1. 获取基准线引擎的真实成交结果
    auto baseline_trades = run_and_collect_trades<EngineNaive<SystemAllocator<Order>>>(test_stream);
    
    ASSERT_GT(baseline_trades.size(), 0) << "测试流没有产生任何成交，请调整随机范围！";
    std::cout << "[INFO] 基准线引擎产生了 " << baseline_trades.size() << " 笔成交记录。" << std::endl;

    // 2. 将另外 7 种“魔改”优化引擎的成交记录与基准线进行暴力对比！
    auto trades_map_pool = run_and_collect_trades<EngineNaive<PoolAllocator<Order>>>(test_stream);
    EXPECT_EQ(baseline_trades, trades_map_pool) << "Map+Pool 引擎的成交结果与基准不一致！";

    auto trades_map_intru_sys = run_and_collect_trades<EngineNaiveIntrusive<SystemAllocator<Order>>>(test_stream);
    EXPECT_EQ(baseline_trades, trades_map_intru_sys) << "Map+Intrusive+Sys 引擎出错！";

    auto trades_map_intru_pool = run_and_collect_trades<EngineNaiveIntrusive<PoolAllocator<Order>>>(test_stream);
    EXPECT_EQ(baseline_trades, trades_map_intru_pool) << "Map+Intrusive+Pool 引擎出错！";

    auto trades_arr_sys = run_and_collect_trades<EngineArray<SystemAllocator<Order>>>(test_stream);
    EXPECT_EQ(baseline_trades, trades_arr_sys) << "Array+StdLst+Sys 引擎出错！";

    auto trades_arr_pool = run_and_collect_trades<EngineArray<PoolAllocator<Order>>>(test_stream);
    EXPECT_EQ(baseline_trades, trades_arr_pool) << "Array+StdLst+Pool 引擎出错！";

    auto trades_arr_intru_sys = run_and_collect_trades<EngineArrayIntrusive<SystemAllocator<Order>>>(test_stream);
    EXPECT_EQ(baseline_trades, trades_arr_intru_sys) << "Array+Intrusive+Sys 引擎出错！";

    auto trades_arr_intru_pool = run_and_collect_trades<EngineArrayIntrusive<PoolAllocator<Order>>>(test_stream);
    EXPECT_EQ(baseline_trades, trades_arr_intru_pool) << "终极形态 Array+Intrusive+Pool 引擎出错！";
}