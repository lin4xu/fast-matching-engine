#pragma once
#include "../common/i_engine.h"
#include "../common/order.h"
#include <map>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <utility>

namespace matching_engine {

template <typename Allocator>
class EngineNaive : public IMatchingEngine {
public:
    EngineNaive() = default;

    ~EngineNaive() override {
        for (auto &kv : order_map_) {
            Order* o = kv.second.first;
            if (o) allocator_.deallocate(o);
        }
    }

    void add_order(uint32_t order_id, Side side, uint32_t price, uint32_t quantity, uint64_t timestamp) override {
        Order* new_order = allocator_.allocate(order_id, side, price, quantity, timestamp);
        if (side == Side::BUY) {
            auto &lst = bids_[price];
            lst.push_back(new_order);
            auto it = std::prev(lst.end());
            order_map_[order_id] = std::make_pair(new_order, it);
        } else {
            auto &lst = asks_[price];
            lst.push_back(new_order);
            auto it = std::prev(lst.end());
            order_map_[order_id] = std::make_pair(new_order, it);
        }
        match();
    }

    void cancel_order(uint32_t order_id) override {
        auto it_map = order_map_.find(order_id);
        if (it_map == order_map_.end()) return;

        Order* order = it_map->second.first;
        auto it_in_list = it_map->second.second;

        if (order->side == Side::BUY) {
            auto list_it = bids_.find(order->price);
            if (list_it != bids_.end()) {
                auto &lst = list_it->second;
                lst.erase(it_in_list);
                if (lst.empty()) bids_.erase(list_it);
            }
        } else {
            auto list_it = asks_.find(order->price);
            if (list_it != asks_.end()) {
                auto &lst = list_it->second;
                lst.erase(it_in_list);
                if (lst.empty()) asks_.erase(list_it);
            }
        }

        order->status = OrderStatus::CANCELED;
        order_map_.erase(it_map);
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
                order_map_.erase(bid_order->order_id);
                bid_list.pop_front();
                allocator_.deallocate(bid_order);
                if (bid_list.empty()) bids_.erase(best_bid_it);
            }
            if (ask_order->leaves_qty == 0) {
                order_map_.erase(ask_order->order_id);
                ask_list.pop_front();
                allocator_.deallocate(ask_order);
                if (ask_list.empty()) asks_.erase(best_ask_it);
            }
        }
    }

    Allocator allocator_;
    std::unordered_map<uint32_t, std::pair<Order*, std::list<Order*>::iterator>> order_map_;
    std::map<uint32_t, std::list<Order*>, std::greater<uint32_t>> bids_;
    std::map<uint32_t, std::list<Order*>> asks_;
};

}