#pragma once
#include "order.h"

namespace matching_engine {

class IntrusiveOrderList {
public:
    Order* head = nullptr;
    Order* tail = nullptr;

    bool empty() const { return head == nullptr; }
    Order* front() const { return head; }

    void push_back(Order* order) {
        order->prev = tail;
        order->next = nullptr;
        if (tail) tail->next = order;
        else head = order;
        tail = order;
    }

    void pop_front() {
        if (!head) return;
        Order* old_head = head;
        head = head->next;
        if (head) head->prev = nullptr;
        else tail = nullptr;
        old_head->next = old_head->prev = nullptr;
    }

    void erase(Order* order) {
        if (order->prev) order->prev->next = order->next;
        else head = order->next;

        if (order->next) order->next->prev = order->prev;
        else tail = order->prev;
        
        order->next = order->prev = nullptr;
    }
};

}