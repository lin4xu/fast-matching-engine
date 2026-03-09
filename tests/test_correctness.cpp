#include <gtest/gtest.h>
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
#include <iostream>

using namespace matching_engine;

// ==========================================
// 1. 定义用于对比的“成交记录”快照
// ==========================================
struct TradeRecord {
    uint32_t maker_order_id;
    uint32_t taker_order_id;
    uint32_t price;
    uint32_t quantity;

    // 必须重载 == 运算符，让 GTest 知道如何判断两笔成交是否在所有属性上都绝对一样
    bool operator==(const TradeRecord& other) const {
        return maker_order_id == other.maker_order_id &&
               taker_order_id == other.taker_order_id &&
               price == other.price &&
               quantity == other.quantity;
    }
};

// 模拟实盘的原始订单数据结构
struct TestOrder {
    uint32_t id; Side side; uint32_t price; uint32_t quantity; uint64_t timestamp; bool is_cancel;
};

// ==========================================
// 2. 生成包含“报单”和交叉“撤单”的确定性测试流
// ==========================================
std::vector<TestOrder> generate_deterministic_stream(size_t num_events) {
    std::vector<TestOrder> stream;
    stream.reserve(num_events);
    
    // 采用固定种子 (888)，保证每次生成的订单流绝对一致，这对于回归测试非常关键
    std::mt19937 gen(888); 
    std::uniform_int_distribution<uint32_t> side_dist(0, 1);
    std::uniform_int_distribution<uint32_t> price_dist(900, 1100); 
    std::uniform_int_distribution<uint32_t> lot_dist(1, 100);
    std::uniform_int_distribution<uint32_t> action_dist(1, 10); // 10% 概率触发随机撤单

    uint32_t current_id = 1;
    for (size_t i = 0; i < num_events; ++i) {
        bool is_cancel = (action_dist(gen) == 1) && (current_id > 1);
        if (is_cancel) {
            // 随机撤销之前发出的某个订单
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
// 3. 核心收集函数：运行指定引擎并录制所有的 Trade
// ==========================================
template <typename EngineType>
std::vector<TradeRecord> run_and_collect_trades(const std::vector<TestOrder>& stream) {
    std::vector<TradeRecord> trades;
    
    std::unique_ptr<EngineType> engine;
    
    // 根据引擎类型选择不同的构造方式 (Array 和 Bitset 需要预设价格区间)
    if constexpr (std::is_constructible_v<EngineType, uint32_t, uint32_t, uint32_t>) {
        engine = std::make_unique<EngineType>(90000, 110000, 100);
    } else {
        engine = std::make_unique<EngineType>();
    }

    // 挂载回调钩子：拦截所有的成交事件并压入快照数组中
    engine->set_trade_callback([&trades](const Trade& info) {
        trades.push_back({
            info.maker_order_id, 
            info.taker_order_id, 
            info.price, 
            info.quantity
        });
    });

    // 严格按顺序回放所有订单流
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
// 4. 定义 GTest 自动化对比测试用例 (Shadow Testing)
// ==========================================
TEST(EngineConsistencyTest, AblationEnginesMatchBaseline) {
    // 生成 5万 条确定的复杂测试事件 (包含交叉撮合和撤单)
    auto test_stream = generate_deterministic_stream(50000);

    // 1. 获取基准线引擎 (最原始无优化的 std::map + std::list 版本) 的真实成交结果
    auto baseline_trades = run_and_collect_trades<EngineNaive<SystemAllocator<Order>>>(test_stream);
    
    // 防御性校验：必须保证流的随机范围确实引发了交叉撮合
    ASSERT_GT(baseline_trades.size(), 0) << "测试流没有产生任何成交，请调整随机范围！";
    std::cout << "[INFO] 稳态模拟完成。基准线引擎成功输出了 " << baseline_trades.size() << " 笔标准成交记录。" << std::endl;

    // =========================================================================
    // 2. 将剩余 8 个由于更换了底层数据结构(Bitset/Intrusive/Union)而导致内存布局面目全非
    //    的高频优化引擎的成交日志，与基准线进行逐字节暴力对比！
    // =========================================================================

    // Map 组校验
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineNaive<PoolAllocator<Order>>>(test_stream))) 
        << "[Error] Map+PoolAlloc 引擎出错！";
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineNaiveIntrusive<PoolAllocator<Order>>>(test_stream))) 
        << "[Error] Map+PoolAlloc+Intrusive 引擎出错！";

    // Array 组校验
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineArray<SystemAllocator<Order>>>(test_stream))) 
        << "[Error] Array+SysAlloc 引擎出错！";
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineArray<PoolAllocator<Order>>>(test_stream))) 
        << "[Error] Array+PoolAlloc 引擎出错！";
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineArrayIntrusive<PoolAllocator<Order>>>(test_stream))) 
        << "[Error] Array+PoolAlloc+Intrusive 引擎出错！";

    // Bitset (位图) 组校验
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineBitset<SystemAllocator<Order>>>(test_stream))) 
        << "[Error] Bitset+SysAlloc 引擎出错！";
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineBitset<PoolAllocator<Order>>>(test_stream))) 
        << "[Error] Bitset+PoolAlloc 引擎出错！";
    
    // 👑 终极大满贯校验：即使内存和寻址逻辑发生了天翻地覆的改变，其业务行为依然严丝合缝
    EXPECT_EQ(baseline_trades, (run_and_collect_trades<EngineBitsetIntrusive<PoolAllocator<Order>>>(test_stream))) 
        << "[Error] 终极大满贯形态 Bitset+PoolAlloc+Intrusive 引擎的撮合逻辑出现 Bug！";
}