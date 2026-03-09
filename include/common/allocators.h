#pragma once
#include <vector>
#include <stdexcept>
#include <new>
#include <utility>
#include <cstddef>

namespace matching_engine {

template <typename T>
class SystemAllocator {
public:
    template <typename... Args>
    T* allocate(Args&&... args) {
        return new T(std::forward<Args>(args)...);
    }
    void deallocate(T* ptr) {
        delete ptr;
    }
};

template <typename T, size_t PoolSize = 1000000>
class PoolAllocator {
private:
    union Element {
        std::byte raw_data[sizeof(T)];
        Element* next_free;
    };

    std::vector<Element> raw_pool_;
    Element* free_head_ = nullptr;

public:
    PoolAllocator() {
        raw_pool_.resize(PoolSize);
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            raw_pool_[i].next_free = &raw_pool_[i + 1];
        }
        raw_pool_[PoolSize - 1].next_free = nullptr;
        free_head_ = &raw_pool_[0];
    }

    template <typename... Args>
    T* allocate(Args&&... args) {
        if (!free_head_) return new T(std::forward<Args>(args)...);
        
        Element* chunk = free_head_;
        free_head_ = free_head_->next_free;
        
        return new (chunk->raw_data) T(std::forward<Args>(args)...);
    }

    void deallocate(T* ptr) {
        if (!ptr) return;
        ptr->~T();
        auto* byte_ptr = reinterpret_cast<std::byte*>(ptr);
        auto* pool_start = reinterpret_cast<std::byte*>(raw_pool_.data());
        auto* pool_end = pool_start + raw_pool_.size() * sizeof(Element);

        if (byte_ptr >= pool_start && byte_ptr < pool_end) {
            Element* chunk = reinterpret_cast<Element*>(ptr);
            chunk->next_free = free_head_;
            free_head_ = chunk;
        } else {
            ::operator delete(ptr);
        }
    }
};

}