#pragma once
#include <vector>
#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_BitScanReverse64)
#pragma intrinsic(_BitScanForward64)
#endif

namespace matching_engine {

class LevelBitset {
    std::vector<uint64_t> words_;
public:
    LevelBitset(size_t num_bits) {
        words_.resize((num_bits + 63) / 64, 0);
    }

    inline void set(size_t index) { words_[index / 64] |= (1ULL << (index % 64)); }
    inline void reset(size_t index) { words_[index / 64] &= ~(1ULL << (index % 64)); }

    inline int32_t find_highest() const {
        for (int i = static_cast<int>(words_.size()) - 1; i >= 0; --i) {
            if (words_[i] != 0) {
#ifdef _MSC_VER
                unsigned long bit_pos;
                _BitScanReverse64(&bit_pos, words_[i]);
                return i * 64 + bit_pos;
#else
                return i * 64 + 63 - __builtin_clzll(words_[i]);
#endif
            }
        }
        return -1;
    }

    inline int32_t find_lowest() const {
        for (size_t i = 0; i < words_.size(); ++i) {
            if (words_[i] != 0) {
#ifdef _MSC_VER
                unsigned long bit_pos;
                _BitScanForward64(&bit_pos, words_[i]);
                return static_cast<int32_t>(i * 64 + bit_pos);
#else
                return static_cast<int32_t>(i * 64 + __builtin_ctzll(words_[i]));
#endif
            }
        }
        return -1;
    }
};

}