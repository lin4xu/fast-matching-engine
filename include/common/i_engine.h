#pragma once
#include "types.h"
#include <cstdint>
#include <functional>

namespace matching_engine {

class IMatchingEngine {
public:
    virtual ~IMatchingEngine() = default;

    using TradeCallback = std::function<void(const Trade&)>;
    virtual void set_trade_callback(TradeCallback cb) { trade_cb_ = cb; }
    virtual void add_order(uint32_t order_id, Side side, uint32_t price, uint32_t quantity, uint64_t timestamp) = 0;
    virtual void cancel_order(uint32_t order_id) = 0;

protected:
    TradeCallback trade_cb_ = nullptr;
};

}