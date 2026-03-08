#pragma once
#include <cstdint>

namespace matching_engine {

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

enum class OrderStatus : uint8_t {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED
};

struct Trade {
    uint32_t maker_order_id;
    uint32_t taker_order_id;
    uint32_t price;
    uint32_t quantity;       
};

}