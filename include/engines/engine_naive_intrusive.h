#pragma once
#include "../common/i_engine.h"
#include "../common/order.h"
#include "../common/intrusive_list.h"
#include <map>
#include <vector>
#include <algorithm>

namespace matching_engine {

template <typename Allocator>
class EngineNaiveIntrusive : public IMatchingEngine {
public:
    EngineNaiveIntrusive(size_t max_orders = 1000000) {
        order_map_.resize(max_orders + 1, nullptr);
    }

    ~EngineNaiveIntrusive() override {
        for (Order* o : order_map_) {
            if (o) allocator_.deallocate(o);
        }
    }

    void add_order(uint32_t order_id, Side side, uint32_t price, uint32_t quantity, uint64_t timestamp) override {
        Order* new_order = allocator_.allocate(order_id, side, price, quantity, timestamp);
        if (order_id < order_map_.size()) order_map_[order_id] = new_order;

        if (side == Side::BUY) bids_[price].push_back(new_order);
        else asks_[price].push_back(new_order);
        match();
    }

    void cancel_order(uint32_t order_id) override {
        if (order_id >= order_map_.size() || !order_map_[order_id]) return;
        Order* order = order_map_[order_id];

        if (order->side == Side::BUY) {
            bids_[order->price].erase(order);
            if (bids_[order->price].empty()) bids_.erase(order->price);
        } else {
            asks_[order->price].erase(order);
            if (asks_[order->price].empty()) asks_.erase(order->price);
        }

        order->status = OrderStatus::CANCELED;
        order_map_[order_id] = nullptr;
        allocator_.deallocate(order);
    }

private:
    void match() {
        while (!bids_.empty() && !asks_.empty()) {
            auto best_bid_it = bids_.begin();
            auto best_ask_it = asks_.begin();
            if (best_bid_it->first < best_ask_it->first) break;

            auto &bid_list = best_bid_it->second;
            auto &ask_list = best_ask_it->second;

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
                if (bid_list.empty()) bids_.erase(best_bid_it);
            }
            if (ask_order->leaves_qty == 0) {
                order_map_[ask_order->order_id] = nullptr;
                ask_list.pop_front();
                allocator_.deallocate(ask_order);
                if (ask_list.empty()) asks_.erase(best_ask_it);
            }
        }
    }

    Allocator allocator_;
    std::vector<Order*> order_map_;
    std::map<uint32_t, IntrusiveOrderList, std::greater<uint32_t>> bids_;
    std::map<uint32_t, IntrusiveOrderList> asks_;
};

}