#pragma once
#include "../common/i_engine.h"
#include "../common/order.h"
#include "../common/intrusive_list.h"
#include "../common/bitset.h"
#include <vector>
#include <algorithm>

namespace matching_engine {

template <typename Allocator>
class EngineBitsetIntrusive : public IMatchingEngine {
public:
    EngineBitsetIntrusive(uint32_t min_price, uint32_t max_price, uint32_t tick_size, size_t max_orders = 1000000)
        : min_price_(min_price), max_price_(max_price), tick_size_(tick_size),
          bid_bitset_((max_price_ - min_price_) / tick_size_ + 1),
          ask_bitset_((max_price_ - min_price_) / tick_size_ + 1) {
        
        uint32_t num_levels = (max_price_ - min_price_) / tick_size_ + 1;
        bids_.resize(num_levels);
        asks_.resize(num_levels);
        order_map_.resize(max_orders + 1, nullptr);
    }

    ~EngineBitsetIntrusive() override {
        for (Order* o : order_map_) {
            if (o) allocator_.deallocate(o);
        }
    }

    void add_order(uint32_t order_id, Side side, uint32_t price, uint32_t quantity, uint64_t timestamp) override {
        int32_t index = price_to_index(price);
        if (index == -1) return;

        Order* new_order = allocator_.allocate(order_id, side, price, quantity, timestamp);
        if (order_id < order_map_.size()) order_map_[order_id] = new_order;

        if (side == Side::BUY) {
            bids_[index].push_back(new_order);
            bid_bitset_.set(index);
        } else {
            asks_[index].push_back(new_order);
            ask_bitset_.set(index);
        }
        match();
    }

    void cancel_order(uint32_t order_id) override {
        if (order_id >= order_map_.size() || !order_map_[order_id]) return;
        Order* order = order_map_[order_id];
        int32_t index = price_to_index(order->price);

        if (order->side == Side::BUY) {
            bids_[index].erase(order);
            if (bids_[index].empty()) bid_bitset_.reset(index);
        } else {
            asks_[index].erase(order);
            if (asks_[index].empty()) ask_bitset_.reset(index);
        }

        order->status = OrderStatus::CANCELED;
        order_map_[order_id] = nullptr;
        allocator_.deallocate(order);
    }

private:
    inline int32_t price_to_index(uint32_t price) const {
        return static_cast<int32_t>((price - min_price_) / tick_size_);
    }

    void match() {
        while (true) {
            int32_t best_bid = bid_bitset_.find_highest();
            int32_t best_ask = ask_bitset_.find_lowest();

            if (best_bid == -1 || best_ask == -1 || best_bid < best_ask) break;

            auto &bid_list = bids_[best_bid];
            auto &ask_list = asks_[best_ask];

            Order* bid_order = bid_list.front();
            Order* ask_order = ask_list.front();
            uint32_t trade_qty = std::min(bid_order->leaves_qty, ask_order->leaves_qty);

            if (trade_cb_) {
                uint32_t maker_id = (bid_order->timestamp < ask_order->timestamp) ? bid_order->order_id : ask_order->order_id;
                uint32_t taker_id = (bid_order->timestamp < ask_order->timestamp) ? ask_order->order_id : bid_order->order_id;
                uint32_t match_price = (bid_order->timestamp < ask_order->timestamp) ? bid_order->price : ask_order->price;
                trade_cb_({maker_id, taker_id, match_price, trade_qty});
            }

            bid_order->leaves_qty -= trade_qty;
            ask_order->leaves_qty -= trade_qty;

            if (bid_order->leaves_qty == 0) {
                order_map_[bid_order->order_id] = nullptr;
                bid_list.pop_front();
                allocator_.deallocate(bid_order);
                if (bid_list.empty()) bid_bitset_.reset(best_bid);
            }
            if (ask_order->leaves_qty == 0) {
                order_map_[ask_order->order_id] = nullptr;
                ask_list.pop_front();
                allocator_.deallocate(ask_order);
                if (ask_list.empty()) ask_bitset_.reset(best_ask);
            }
        }
    }

    Allocator allocator_;
    uint32_t min_price_, max_price_, tick_size_;
    LevelBitset bid_bitset_, ask_bitset_;
    std::vector<Order*> order_map_;
    std::vector<IntrusiveOrderList> bids_, asks_;
};

}