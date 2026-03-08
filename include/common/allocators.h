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
    struct alignas(alignof(T)) Element {
        std::byte raw_data[sizeof(T)];
    };

    std::vector<Element> raw_pool_;
    std::vector<T*> free_list_;

public:
    PoolAllocator() {
        raw_pool_.resize(PoolSize);
        free_list_.reserve(PoolSize);
        for (size_t i = 0; i < PoolSize; ++i) {
            free_list_.push_back(reinterpret_cast<T*>(&raw_pool_[i]));
        }
    }

    template <typename... Args>
    T* allocate(Args&&... args) {
        if (free_list_.empty()) return new T(std::forward<Args>(args)...);
        
        T* ptr = free_list_.back();
        free_list_.pop_back();
        
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }

    void deallocate(T* ptr) {
        if (!ptr) return;
        free_list_.push_back(ptr);
    }
};

}