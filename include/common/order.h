#pragma once
#include "types.h"

namespace matching_engine {

struct Order {
    uint32_t order_id;
    uint32_t price;
    uint32_t quantity;
    uint32_t leaves_qty;
    Side side;
    OrderStatus status;
    uint64_t timestamp;

    Order* prev;
    Order* next;

    Order() : order_id(0), price(0), quantity(0), leaves_qty(0), 
              side(Side::BUY), status(OrderStatus::NEW), timestamp(0),
              prev(nullptr), next(nullptr) {}

    Order(uint32_t id, Side s, uint32_t p, uint32_t q, uint64_t ts)
        : order_id(id), price(p), quantity(q), leaves_qty(q), 
          side(s), status(OrderStatus::NEW), timestamp(ts),
          prev(nullptr), next(nullptr) {}
};

}